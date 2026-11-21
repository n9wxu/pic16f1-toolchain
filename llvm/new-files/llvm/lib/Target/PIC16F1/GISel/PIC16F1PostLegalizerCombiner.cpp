//===-- PIC16F1PostLegalizerCombiner.cpp - Combines after legalization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs the target-independent GlobalISel combines after legalization. The
// rules are chosen in PIC16F1Combine.td.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"

#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

#define GET_GICOMBINER_DEPS
#include "PIC16F1GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "pic16f1-postlegalizer-combiner"

using namespace llvm;

// Switches for the steps this pass adds to the generated combiner, for
// measuring what each one is worth.
static cl::opt<bool> SinkLoads("pic16f1-sink-loads", cl::Hidden, cl::init(true),
                               cl::desc("Move loads below runtime-library calls"));
static cl::opt<bool> LocalizeAddrs(
    "pic16f1-localize-addresses", cl::Hidden, cl::init(true),
    cl::desc("Give each user of an address its own copy"));
static cl::opt<bool> PhiConsts(
    "pic16f1-phi-constants", cl::Hidden, cl::init(true),
    cl::desc("Load a PHI's constant input at the end of the predecessor"));
static cl::opt<bool> SinkConsts(
    "pic16f1-sink-constants", cl::Hidden, cl::init(true),
    cl::desc("Move constants down to their first user"));

/// G_TRUNC to a byte of a 16-bit add, subtract, and, or or xor that has no
/// other user.
static bool matchNarrowTruncBinop(MachineInstr &Trunc,
                                  const MachineRegisterInfo &MRI) {
  Register Src = Trunc.getOperand(1).getReg();
  if (MRI.getType(Trunc.getOperand(0).getReg()) != LLT::scalar(8) ||
      MRI.getType(Src) != LLT::scalar(16) || !MRI.hasOneNonDBGUse(Src))
    return false;
  const MachineInstr *Def = MRI.getVRegDef(Src);
  if (!Def)
    return false;
  switch (Def->getOpcode()) {
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR:
    return true;
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_ASHR: {
    // A shift by eight or more takes the low byte from the high byte.
    auto C = getIConstantVRegValWithLookThrough(Def->getOperand(2).getReg(), MRI);
    return C && C->Value.uge(8) && C->Value.ult(16);
  }
  default:
    return false;
  }
}

static void applyNarrowTruncBinop(MachineInstr &Trunc, MachineRegisterInfo &MRI,
                                  MachineIRBuilder &B) {
  const LLT S8 = LLT::scalar(8);
  MachineInstr *Op = MRI.getVRegDef(Trunc.getOperand(1).getReg());
  B.setInstrAndDebugLoc(*Op);
  // The low byte of an operand, without a truncation where it is already
  // at hand.
  auto Low = [&](Register R) -> Register {
    if (auto C = getIConstantVRegValWithLookThrough(R, MRI))
      return B.buildConstant(S8, C->Value.trunc(8)).getReg(0);
    MachineInstr *Def = MRI.getVRegDef(R);
    switch (Def->getOpcode()) {
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_ANYEXT:
    case TargetOpcode::G_MERGE_VALUES:
      if (MRI.getType(Def->getOperand(1).getReg()) == S8)
        return Def->getOperand(1).getReg();
      break;
    default:
      break;
    }
    return B.buildTrunc(S8, R).getReg(0);
  };
  Register Dst = Trunc.getOperand(0).getReg();
  if (Op->getOpcode() == TargetOpcode::G_LSHR ||
      Op->getOpcode() == TargetOpcode::G_ASHR) {
    uint64_t N =
        getIConstantVRegValWithLookThrough(Op->getOperand(2).getReg(), MRI)
            ->Value.getZExtValue();
    Register Hi = B.buildUnmerge(S8, Op->getOperand(1).getReg()).getReg(1);
    if (N == 8)
      B.buildCopy(Dst, Hi);
    else
      B.buildInstr(Op->getOpcode(), {Dst}, {Hi, B.buildConstant(S8, N - 8)});
    Trunc.eraseFromParent();
    return;
  }
  Register L = Low(Op->getOperand(1).getReg());
  Register R = Low(Op->getOperand(2).getReg());
  B.buildInstr(Op->getOpcode(), {Dst}, {L, R});
  Trunc.eraseFromParent();
}

/// What is known about a 16-bit operand whose high byte is zero.
struct ByteOperand {
  bool IsByte = false;          // the high byte is known to be zero
  Register Byte;                // the byte it extends, if it is a G_ZEXT
  std::optional<APInt> Const;   // its value, if it is a constant
};

static ByteOperand classifyByte(Register R, const MachineRegisterInfo &MRI,
                                GISelValueTracking &VT) {
  ByteOperand Op;
  if (auto C = getIConstantVRegValWithLookThrough(R, MRI)) {
    Op.Const = C->Value;
    Op.IsByte = C->Value.isIntN(8);
    return Op;
  }
  const MachineInstr *Def = MRI.getVRegDef(R);
  if (Def && Def->getOpcode() == TargetOpcode::G_ZEXT &&
      MRI.getType(Def->getOperand(1).getReg()) == LLT::scalar(8)) {
    Op.Byte = Def->getOperand(1).getReg();
    Op.IsByte = true;
    return Op;
  }
  Op.IsByte = VT.getKnownBits(R).countMinLeadingZeros() >= 8;
  return Op;
}

/// A 16-bit shift right or logic operation whose result has a zero high
/// byte for a reason other than being a constant.
static bool matchNarrowBinopOfZExt(MachineInstr &MI,
                                   const MachineRegisterInfo &MRI,
                                   GISelValueTracking &VT) {
  if (MRI.getType(MI.getOperand(0).getReg()) != LLT::scalar(16))
    return false;
  ByteOperand L = classifyByte(MI.getOperand(1).getReg(), MRI, VT);
  if (MI.getOpcode() == TargetOpcode::G_LSHR)
    return L.IsByte && !L.Const &&
           getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI)
               .has_value();
  ByteOperand R = classifyByte(MI.getOperand(2).getReg(), MRI, VT);
  if (L.Const && R.Const)
    return false;
  // An AND needs only one side to be a byte; OR and XOR need both.
  if (MI.getOpcode() == TargetOpcode::G_AND)
    return (L.IsByte && !L.Const) || (R.IsByte && !R.Const) ||
           (L.IsByte && R.IsByte);
  return L.IsByte && R.IsByte;
}

static void applyNarrowBinopOfZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                                   MachineIRBuilder &B,
                                   GISelValueTracking &VT) {
  const LLT S8 = LLT::scalar(8);
  B.setInstrAndDebugLoc(MI);
  auto Low = [&](Register R) -> Register {
    ByteOperand Op = classifyByte(R, MRI, VT);
    if (Op.Const)
      return B.buildConstant(S8, Op.Const->trunc(8)).getReg(0);
    if (Op.Byte.isValid())
      return Op.Byte;
    return B.buildTrunc(S8, R).getReg(0);
  };
  Register Narrow;
  if (MI.getOpcode() == TargetOpcode::G_LSHR) {
    uint64_t N = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI)
                     ->Value.getZExtValue();
    if (N >= 8) {
      B.buildConstant(MI.getOperand(0).getReg(), 0);
      MI.eraseFromParent();
      return;
    }
    Narrow = B.buildLShr(S8, Low(MI.getOperand(1).getReg()),
                         B.buildConstant(S8, N))
                 .getReg(0);
  } else {
    Register L = Low(MI.getOperand(1).getReg());
    Register R = Low(MI.getOperand(2).getReg());
    Narrow = B.buildInstr(MI.getOpcode(), {S8}, {L, R}).getReg(0);
  }
  B.buildZExt(MI.getOperand(0).getReg(), Narrow);
  MI.eraseFromParent();
}

/// A 16-bit shift by a constant of eight to fifteen.
static bool matchWideShiftToBytes(MachineInstr &MI,
                                  const MachineRegisterInfo &MRI) {
  if (MRI.getType(MI.getOperand(0).getReg()) != LLT::scalar(16))
    return false;
  auto C = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
  return C && C->Value.uge(8) && C->Value.ult(16);
}

static void applyWideShiftToBytes(MachineInstr &MI, MachineRegisterInfo &MRI,
                                  MachineIRBuilder &B) {
  const LLT S8 = LLT::scalar(8);
  B.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  uint64_t N = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI)
                   ->Value.getZExtValue() - 8;
  unsigned Opc = MI.getOpcode();
  Register Byte = Opc == TargetOpcode::G_SHL
                      ? B.buildTrunc(S8, Src).getReg(0)
                      : B.buildUnmerge(S8, Src).getReg(1);
  if (N != 0)
    Byte = B.buildInstr(Opc, {S8}, {Byte, B.buildConstant(S8, N)}).getReg(0);
  if (Opc == TargetOpcode::G_SHL)
    B.buildMergeLikeInstr(Dst, {B.buildConstant(S8, 0).getReg(0), Byte});
  else if (Opc == TargetOpcode::G_LSHR)
    B.buildZExt(Dst, Byte);
  else
    B.buildSExt(Dst, Byte);
  MI.eraseFromParent();
}

/// Which byte of \p R is known to be zero: 0 for the low byte, 1 for the
/// high byte, -1 for neither. Constants are left to the instruction
/// expansion, which already skips their zero bytes.
static int zeroByte(Register R, const MachineRegisterInfo &MRI,
                    GISelValueTracking &VT) {
  if (getIConstantVRegValWithLookThrough(R, MRI))
    return -1;
  KnownBits Known = VT.getKnownBits(R);
  if (Known.countMinTrailingZeros() >= 8)
    return 0;
  if (Known.countMinLeadingZeros() >= 8)
    return 1;
  return -1;
}

/// The operand (1 or 2) with a zero byte, or 0.
static unsigned bytewiseOperand(const MachineInstr &MI,
                                const MachineRegisterInfo &MRI,
                                GISelValueTracking &VT) {
  if (MRI.getType(MI.getOperand(0).getReg()) != LLT::scalar(16))
    return 0;
  for (unsigned Idx : {2u, 1u}) {
    int Z = zeroByte(MI.getOperand(Idx).getReg(), MRI, VT);
    // An add carries from the low byte into the high one, so only a zero
    // low byte separates it.
    if (Z == 0 || (Z == 1 && MI.getOpcode() != TargetOpcode::G_ADD))
      return Idx;
  }
  return 0;
}

static bool matchBytewiseBinop(MachineInstr &MI, const MachineRegisterInfo &MRI,
                               GISelValueTracking &VT) {
  return bytewiseOperand(MI, MRI, VT) != 0;
}

static void applyBytewiseBinop(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MachineIRBuilder &B, GISelValueTracking &VT) {
  const LLT S8 = LLT::scalar(8);
  unsigned Idx = bytewiseOperand(MI, MRI, VT);
  Register Y = MI.getOperand(Idx).getReg();
  Register X = MI.getOperand(3 - Idx).getReg();
  bool LowIsZero = zeroByte(Y, MRI, VT) == 0;
  B.setInstrAndDebugLoc(MI);
  auto XB = B.buildUnmerge(S8, X);
  auto YB = B.buildUnmerge(S8, Y);
  unsigned Part = LowIsZero ? 1 : 0;
  Register Changed =
      B.buildInstr(MI.getOpcode(), {S8}, {XB.getReg(Part), YB.getReg(Part)})
          .getReg(0);
  if (LowIsZero)
    B.buildMergeLikeInstr(MI.getOperand(0).getReg(), {XB.getReg(0), Changed});
  else
    B.buildMergeLikeInstr(MI.getOperand(0).getReg(), {Changed, XB.getReg(1)});
  MI.eraseFromParent();
}

/// A value that is zero unless a condition holds: a G_SELECT with a zero
/// arm, or a comparison result (0 or 1), possibly extended.
struct ConditionalValue {
  Register Cond;                 // the condition, a byte holding 0 or 1
  Register Value;                // the value when it is not zero, or
  bool IsOne = false;            // ... the constant 1
  bool WhenTrue = true;          // non-zero when Cond is 1 (else when 0)
};

static std::optional<ConditionalValue>
matchConditionalValue(Register R, const MachineInstr &User,
                      const MachineRegisterInfo &MRI) {
  const MachineInstr *Def = MRI.getVRegDef(R);
  if (!Def || Def->getParent() != User.getParent())
    return std::nullopt;
  // A select or an extension would stay behind for its other users. A
  // comparison may have them: the condition is only read.
  if (Def->getOpcode() != TargetOpcode::G_ICMP && !MRI.hasOneNonDBGUse(R))
    return std::nullopt;
  auto IsZero = [&](Register V) {
    auto C = getIConstantVRegValWithLookThrough(V, MRI);
    return C && C->Value.isZero();
  };
  ConditionalValue CV;
  if (Def->getOpcode() == TargetOpcode::G_SELECT) {
    CV.Cond = Def->getOperand(1).getReg();
    if (IsZero(Def->getOperand(3).getReg())) {
      CV.Value = Def->getOperand(2).getReg();
    } else if (IsZero(Def->getOperand(2).getReg())) {
      CV.Value = Def->getOperand(3).getReg();
      CV.WhenTrue = false;
    } else {
      return std::nullopt;
    }
    return CV;
  }
  // A comparison, as a byte or zero-extended to 16 bits.
  if (Def->getOpcode() == TargetOpcode::G_ZEXT) {
    Register Src = Def->getOperand(1).getReg();
    Def = MRI.getVRegDef(Src);
    if (!Def || Def->getParent() != User.getParent())
      return std::nullopt;
  }
  if (Def->getOpcode() != TargetOpcode::G_ICMP)
    return std::nullopt;
  CV.Cond = Def->getOperand(0).getReg();
  CV.IsOne = true;
  return CV;
}

/// The operand index (1 or 2) of \p MI that is zero unless a condition
/// holds, or 0. A subtraction only qualifies on its right side.
static unsigned conditionalOperand(const MachineInstr &MI,
                                   const MachineRegisterInfo &MRI) {
  for (unsigned Idx : {2u, 1u}) {
    if (Idx == 1 && MI.getOpcode() == TargetOpcode::G_SUB)
      continue;
    if (matchConditionalValue(MI.getOperand(Idx).getReg(), MI, MRI))
      return Idx;
  }
  return 0;
}

static bool matchBinopOfSelectIdentity(MachineInstr &MI,
                                       const MachineRegisterInfo &MRI) {
  // Without optimization nothing merges the branch this makes with its
  // surroundings, and it only adds code.
  if (MI.getMF()->getTarget().getOptLevel() == CodeGenOptLevel::None)
    return false;
  return conditionalOperand(MI, MRI) != 0;
}

static void applyBinopOfSelectIdentity(MachineInstr &MI,
                                       MachineRegisterInfo &MRI,
                                       MachineIRBuilder &B) {
  unsigned Idx = conditionalOperand(MI, MRI);
  ConditionalValue CV =
      *matchConditionalValue(MI.getOperand(Idx).getReg(), MI, MRI);
  Register X = MI.getOperand(3 - Idx).getReg();
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  B.setInstrAndDebugLoc(MI);
  Register A = CV.IsOne ? B.buildConstant(Ty, 1).getReg(0) : CV.Value;
  // Keep the operand order: it matters for a subtraction.
  auto Op = Idx == 2 ? B.buildInstr(MI.getOpcode(), {Ty}, {X, A})
                     : B.buildInstr(MI.getOpcode(), {Ty}, {A, X});
  if (CV.WhenTrue)
    B.buildSelect(Dst, CV.Cond, Op.getReg(0), X);
  else
    B.buildSelect(Dst, CV.Cond, X, Op.getReg(0));
  MI.eraseFromParent();
}

/// True for a call to one of the arithmetic routines of the runtime library
/// (multiply, divide, shift, floating point). They read and write only
/// their arguments and their own stack frame.
static bool isArithmeticLibcall(const MachineInstr &MI) {
  if (MI.getOpcode() != PIC16F1::CALLsym || !MI.getOperand(0).isSymbol())
    return false;
  StringRef Name = MI.getOperand(0).getSymbolName();
  for (const char *Prefix :
       {"__mul", "__div", "__udiv", "__mod", "__umod", "__ashl", "__lshr",
        "__ashr", "__add", "__sub", "__neg", "__fix", "__float", "__eq",
        "__ne", "__lt", "__le", "__gt", "__ge", "__unord", "__cmp",
        "__ucmp"})
    if (Name.starts_with(Prefix))
      return true;
  return false;
}

/// A value loaded before a call and used after it must live in a saved
/// register across the call, or be spilled. When the call is to an
/// arithmetic routine, which cannot change the memory the load reads, move
/// the load down to its user instead. Instruction order here follows the
/// source, so "x += a * b" reads x before it multiplies.
static bool sinkLoadsBelowLibcalls(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 8> Loads;
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == TargetOpcode::G_LOAD)
        Loads.push_back(&MI);
    for (MachineInstr *Load : Loads) {
      auto &L = cast<GLoad>(*Load);
      if (!L.isSimple() || !MRI.hasOneNonDBGUse(L.getDstReg()))
        continue;
      MachineInstr &User = *MRI.use_instr_nodbg_begin(L.getDstReg());
      if (User.getParent() != &MBB || User.isPHI())
        continue;
      bool CrossesCall = false, Safe = true;
      for (auto It = std::next(Load->getIterator()); &*It != &User; ++It) {
        if (It->isCall()) {
          if (!isArithmeticLibcall(*It)) {
            Safe = false;
            break;
          }
          CrossesCall = true;
          continue;
        }
        // Outgoing stack arguments are stored with ST8fi and ST16fi, into
        // an area nothing else addresses.
        if (It->getOpcode() == PIC16F1::ST8fi ||
            It->getOpcode() == PIC16F1::ST16fi)
          continue;
        if (It->mayStore() || It->hasUnmodeledSideEffects()) {
          Safe = false;
          break;
        }
      }
      if (!Safe || !CrossesCall)
        continue;
      // The address must still be available: it is, being a virtual
      // register in SSA form defined above the load.
      MBB.splice(User.getIterator(), &MBB, Load->getIterator());
      Changed = true;
    }
  }
  return Changed;
}

/// Give each user of a stack-slot or global address its own copy of the
/// address, placed right before it. The translator creates one definition
/// per function, which is then live across calls and loops and has to sit in
/// a saved register or be spilled. Forming the address again costs no more
/// than the register copy most users need anyway, because arithmetic
/// overwrites its first operand.
static bool localizeAddresses(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  SmallVector<MachineInstr *, 16> Defs;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == TargetOpcode::G_FRAME_INDEX ||
          MI.getOpcode() == TargetOpcode::G_GLOBAL_VALUE)
        Defs.push_back(&MI);
  for (MachineInstr *Def : Defs) {
    Register Reg = Def->getOperand(0).getReg();
    SmallVector<MachineOperand *, 8> Uses;
    for (MachineOperand &MO : MRI.use_nodbg_operands(Reg))
      Uses.push_back(&MO);
    // The user right after the definition keeps it.
    for (MachineOperand *MO : Uses) {
      MachineInstr &User = *MO->getParent();
      if (User.isPHI())
        continue;
      if (User.getParent() == Def->getParent() &&
          std::next(Def->getIterator()) == User.getIterator())
        continue;
      MachineInstr *Clone = MF.CloneMachineInstr(Def);
      Register NewReg = MRI.cloneVirtualRegister(Reg);
      Clone->getOperand(0).setReg(NewReg);
      User.getParent()->insert(User.getIterator(), Clone);
      MO->setReg(NewReg);
      Changed = true;
    }
    if (MRI.use_nodbg_empty(Reg)) {
      Def->eraseFromParent();
    }
  }
  return Changed;
}

/// The translator defines each constant once, at the top of the function.
/// A constant that is only the starting value of a loop variable, or is
/// first needed further down, would be loaded there and then kept in a
/// register past everything in between, calls included. Move each such
/// definition below the calls that stand between it and its first user in
/// the block, or all calls in the block when the users are elsewhere.
static bool sinkConstants(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 16> Consts;
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == TargetOpcode::G_CONSTANT ||
          Opc == TargetOpcode::G_GLOBAL_VALUE ||
          Opc == TargetOpcode::G_FRAME_INDEX)
        Consts.push_back(&MI);
    }
    for (MachineInstr *Def : Consts) {
      Register Reg = Def->getOperand(0).getReg();
      // The first user in this block that is not a PHI.
      MachineBasicBlock::iterator Target = MBB.getFirstTerminator();
      for (MachineBasicBlock::iterator It = std::next(Def->getIterator());
           It != MBB.end(); ++It) {
        if (It == Target)
          break;
        if (!It->isPHI() && It->readsRegister(Reg, /*TRI=*/nullptr)) {
          Target = It;
          break;
        }
      }
      // Only a call in between makes the early definition expensive, and
      // only past the last such call does it need to go: further down it
      // would be out of reach of the passes that place it on one branch.
      MachineBasicBlock::iterator AfterCall = MBB.end();
      for (MachineBasicBlock::iterator It = std::next(Def->getIterator());
           It != Target; ++It)
        if (It->getOpcode() == PIC16F1::ADJCALLSTACKUP)
          AfterCall = std::next(It);
      if (AfterCall == MBB.end())
        continue;
      MBB.splice(AfterCall, &MBB, Def->getIterator());
      Changed = true;
    }
  }
  return Changed;
}

/// A constant that is the value a PHI takes along one edge belongs at the
/// end of the block the edge leaves, when that block goes nowhere else: it
/// is then loaded exactly when it is needed, and occupies no register
/// before. The translator's single definition at the top of the function
/// would be live from there, through any loops in between.
static bool placePhiConstants(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &Phi : MBB.phis()) {
      for (unsigned I = 1, E = Phi.getNumOperands(); I + 1 < E; I += 2) {
        MachineOperand &In = Phi.getOperand(I);
        MachineBasicBlock *Pred = Phi.getOperand(I + 1).getMBB();
        MachineInstr *Def = MRI.getVRegDef(In.getReg());
        if (!Def || Pred->succ_size() != 1)
          continue;
        unsigned Opc = Def->getOpcode();
        if (Opc != TargetOpcode::G_CONSTANT &&
            Opc != TargetOpcode::G_GLOBAL_VALUE &&
            Opc != TargetOpcode::G_FRAME_INDEX)
          continue;
        // A constant that instructions use as well stays in one place:
        // the loads those instructions need can then share it.
        if (any_of(MRI.use_nodbg_instructions(In.getReg()),
                   [](const MachineInstr &U) { return !U.isPHI(); }))
          continue;
        // Already the last thing its block does.
        if (Def->getParent() == Pred &&
            MachineBasicBlock::iterator(std::next(Def->getIterator())) ==
                Pred->getFirstTerminator())
          continue;
        MachineInstr *Clone = MF.CloneMachineInstr(Def);
        Register NewReg = MRI.cloneVirtualRegister(In.getReg());
        Clone->getOperand(0).setReg(NewReg);
        Pred->insert(Pred->getFirstTerminator(), Clone);
        In.setReg(NewReg);
        if (MRI.use_nodbg_empty(Def->getOperand(0).getReg()))
          Def->eraseFromParent();
        Changed = true;
      }
    }
  }
  return Changed;
}

/// The carry between two links of a wide add, subtract or shift stays in the
/// carry flag, so the selector needs the links next to each other, with at
/// most moves and loads in between. Other passes work without regard to
/// that; this moves whatever has come to stand between two links above the
/// first one or below the second.
static bool packCarryChains(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 8> Links;
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == TargetOpcode::G_UADDE || Opc == TargetOpcode::G_USUBE ||
          Opc == PIC16F1::G_RRC)
        Links.push_back(&MI);
    }
    for (MachineInstr *User : Links) {
      Register Carry = User->getOperand(User->getNumOperands() - 1).getReg();
      MachineInstr *Def = MRI.getVRegDef(Carry);
      if (!Def || Def->getParent() != &MBB || Def->isPHI())
        continue;
      SmallVector<MachineInstr *, 8> Between;
      bool Movable = true;
      for (auto It = std::next(Def->getIterator()); &*It != User; ++It) {
        // Only generic instructions without side effects other than a
        // plain store move; a call sequence stays where it is.
        if (It->isDebugInstr())
          continue;
        if (!isPreISelGenericOpcode(It->getOpcode()) || It->isCall() ||
            It->isTerminator() || It->isPHI() ||
            (It->hasUnmodeledSideEffects() &&
             It->getOpcode() != TargetOpcode::G_STORE)) {
          Movable = false;
          break;
        }
        Between.push_back(&*It);
      }
      if (!Movable || Between.empty())
        continue;

      // Registers defined by the first link and by instructions that stay
      // below it.
      SmallSet<Register, 8> Late;
      for (const MachineOperand &MO : Def->all_defs())
        Late.insert(MO.getReg());
      SmallVector<MachineInstr *, 8> Stuck;
      bool StoreStuck = false, LoadStuck = false;
      for (MachineInstr *MI : Between) {
        bool DependsOnLate = any_of(MI->all_uses(), [&](const MachineOperand &MO) {
          return Late.count(MO.getReg());
        });
        // Memory accesses keep their order: a load does not pass a store,
        // nor a store a load or another store.
        bool Reads = MI->mayLoad(), Writes = MI->mayStore();
        if (DependsOnLate || (Reads && StoreStuck) ||
            (Writes && (StoreStuck || LoadStuck))) {
          Stuck.push_back(MI);
          StoreStuck |= Writes;
          LoadStuck |= Reads;
          for (const MachineOperand &MO : MI->all_defs())
            Late.insert(MO.getReg());
          continue;
        }
        MBB.splice(Def->getIterator(), &MBB, MI->getIterator());
        Changed = true;
      }
      // What could not go up goes below the second link, unless that link
      // needs its result.
      SmallSet<Register, 8> StuckDefs;
      for (MachineInstr *MI : Stuck)
        for (const MachineOperand &MO : MI->all_defs())
          StuckDefs.insert(MO.getReg());
      if (any_of(User->all_uses(), [&](const MachineOperand &MO) {
            return StuckDefs.count(MO.getReg());
          }))
        continue;
      auto After = std::next(User->getIterator());
      for (MachineInstr *MI : Stuck) {
        MBB.splice(After, &MBB, MI->getIterator());
        Changed = true;
      }
    }
  }
  return Changed;
}

/// Splitting a wide value into pieces leaves a debug value that names it
/// pointing at a register with no definition, wider than any register
/// bank. Debug information for such a variable is dropped: there is no
/// single location to describe.
static bool dropWideDebugValues(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (!MI.isDebugInstr())
        continue;
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.getReg().isVirtual())
          continue;
        LLT Ty = MRI.getType(MO.getReg());
        if (MRI.def_empty(MO.getReg()) ||
            (Ty.isValid() && Ty.getSizeInBits() > 16)) {
          MO.setReg(Register());
          Changed = true;
        }
      }
    }
  return Changed;
}

namespace {

#define GET_GICOMBINER_TYPES
#include "PIC16F1GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

class PIC16F1PostLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const PIC16F1PostLegalizerCombinerImplRuleConfig &RuleConfig;
  const PIC16F1Subtarget &STI;

public:
  PIC16F1PostLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, GISelValueTracking &VT,
      GISelCSEInfo *CSEInfo,
      const PIC16F1PostLegalizerCombinerImplRuleConfig &RuleConfig,
      const PIC16F1Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "PIC16F1PostLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "PIC16F1GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "PIC16F1GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

PIC16F1PostLegalizerCombinerImpl::PIC16F1PostLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, GISelValueTracking &VT,
    GISelCSEInfo *CSEInfo,
    const PIC16F1PostLegalizerCombinerImplRuleConfig &RuleConfig,
    const PIC16F1Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize=*/false, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "PIC16F1GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

static cl::opt<unsigned> PostBudget(
    "pic16f1-postlegalizer-combine-budget", cl::Hidden, cl::init(1000),
    cl::desc("Rule applications allowed per function, on top of 100 per "
             "instruction"));

/// The combiner works until no rule applies, and nothing in it notices two
/// rules that undo each other: it would then run for ever and allocate
/// instructions until the machine is out of memory. This stops applying
/// rules after far more of them than any real function needs. The code is
/// correct at every step, so stopping early only leaves it less optimized.
class BoundedPostCombiner : public PIC16F1PostLegalizerCombinerImpl {
  mutable uint64_t Budget;

public:
  template <typename... Ts>
  BoundedPostCombiner(MachineFunction &MF, Ts &&...Args)
      : PIC16F1PostLegalizerCombinerImpl(MF, std::forward<Ts>(Args)...),
        Budget(PostBudget + 100 * uint64_t(MF.getInstructionCount())) {}

  bool tryCombineAll(MachineInstr &I) const override {
    if (Budget == 0)
      return false;
    if (!PIC16F1PostLegalizerCombinerImpl::tryCombineAll(I))
      return false;
    if (--Budget == 0)
      I.getMF()->getFunction().getContext().diagnose(DiagnosticInfoGeneric(
          Twine("PIC16F1 combiner (") + getName() + ") stopped in function '" +
              I.getMF()->getName() +
              "': rules keep applying without end; please report this",
          DS_Warning));
    return true;
  }
};

class PIC16F1PostLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  PIC16F1PostLegalizerCombiner();

  StringRef getPassName() const override {
    return "PIC16F1PostLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  PIC16F1PostLegalizerCombinerImplRuleConfig RuleConfig;
};

} // end anonymous namespace

void PIC16F1PostLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelValueTrackingAnalysisLegacy>();
  AU.addPreserved<GISelValueTrackingAnalysisLegacy>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  AU.addPreserved<GISelCSEAnalysisWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

PIC16F1PostLegalizerCombiner::PIC16F1PostLegalizerCombiner()
    : MachineFunctionPass(ID) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool PIC16F1PostLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);
  const PIC16F1Subtarget &ST = MF.getSubtarget<PIC16F1Subtarget>();
  const LegalizerInfo *LI = ST.getLegalizerInfo();
  GISelValueTracking *VT =
      &getAnalysis<GISelValueTrackingAnalysisLegacy>().get(MF);
  MachineDominatorTree *MDT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  auto *CSEInfo = &Wrapper.get(TPC->getCSEConfig());

  CombinerInfo CInfo(/*AllowIllegalOps=*/true, /*ShouldLegalizeIllegal=*/false,
                     /*LegalizerInfo=*/nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  BoundedPostCombiner Impl(MF, CInfo, *VT, CSEInfo, RuleConfig,
                                          ST, MDT, LI);
  bool Changed = Impl.combineMachineInstrs();
  if (EnableOpt && SinkLoads)
    Changed |= sinkLoadsBelowLibcalls(MF);
  if (LocalizeAddrs)
    Changed |= localizeAddresses(MF);
  if (EnableOpt && PhiConsts)
    Changed |= placePhiConstants(MF);
  if (EnableOpt && SinkConsts)
    Changed |= sinkConstants(MF);
  Changed |= packCarryChains(MF);
  Changed |= dropWideDebugValues(MF);
  return Changed;
}

char PIC16F1PostLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(PIC16F1PostLegalizerCombiner, DEBUG_TYPE,
                      "Combine PIC16F1 machine instructions after legalization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(PIC16F1PostLegalizerCombiner, DEBUG_TYPE,
                    "Combine PIC16F1 machine instructions after legalization",
                    false, false)

FunctionPass *llvm::createPIC16F1PostLegalizerCombiner() {
  return new PIC16F1PostLegalizerCombiner();
}
