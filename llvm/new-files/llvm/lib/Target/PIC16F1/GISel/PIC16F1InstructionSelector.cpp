//===-- PIC16F1InstructionSelector.cpp - GlobalISel instruction selection -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Selects legal generic machine IR into the code-generator pseudo
// instructions of PIC16F1InstrInfo.td. Selection is written out by hand: one
// case per generic opcode.
//
// Instructions are visited from the bottom of each block upward, so when an
// instruction is selected its operands' definitions are still generic and
// can be folded into it: a comparison into the branch that uses it, an
// address computation into a load or store, a constant into an immediate
// operand. A definition left without users is deleted before it is visited.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "PIC16F1.h"
#include "PIC16F1RegisterBankInfo.h"
#include "PIC16F1Subtarget.h"
#include "PIC16F1TargetMachine.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "pic16f1-isel"

using namespace llvm;
// The target's opcode enumeration includes the generic opcodes.
using namespace llvm::PIC16F1;

namespace {

/// Where a load or store goes.
struct Address {
  enum KindTy { Pointer, Frame, Global, Constant } Kind = Pointer;
  Register Base;                  // Pointer
  int FrameIndex = 0;             // Frame
  const GlobalValue *GV = nullptr; // Global
  int64_t Offset = 0;             // all kinds; the address itself for Constant
};

class PIC16F1InstructionSelector : public InstructionSelector {
public:
  PIC16F1InstructionSelector(const PIC16F1TargetMachine &TM,
                             const PIC16F1Subtarget &STI,
                             const PIC16F1RegisterBankInfo &RBI)
      : STI(STI), TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()),
        RBI(RBI) {}

  bool select(MachineInstr &I) override;
  void setupGeneratedPerFunctionState(MachineFunction &MF) override {}
  static const char *getName() { return DEBUG_TYPE; }

private:
  const PIC16F1Subtarget &STI;
  const PIC16F1InstrInfo &TII;
  const PIC16F1RegisterInfo &TRI;
  const PIC16F1RegisterBankInfo &RBI;
  MachineRegisterInfo *MRI = nullptr;

  // --- Registers -----------------------------------------------------------
  unsigned sizeOf(Register R) const { return MRI->getType(R).getSizeInBits(); }
  bool is16(Register R) const { return sizeOf(R) == 16; }
  const TargetRegisterClass *classFor(unsigned Bits) const {
    return Bits <= 8 ? &PIC16F1::GPR8RegClass : &PIC16F1::GPR16RegClass;
  }
  /// Give a generic virtual register its register class.
  bool constrain(Register R) const {
    if (!R.isVirtual())
      return true;
    return RBI.constrainGenericRegister(R, *classFor(sizeOf(R)), *MRI) != nullptr;
  }
  Register newReg(unsigned Bits) const {
    return MRI->createVirtualRegister(classFor(Bits));
  }
  /// Finish a selected instruction: constrain its operands, drop the generic
  /// one it replaces.
  bool done(MachineInstr &Old, MachineInstrBuilder New) const {
    Old.eraseFromParent();
    constrainSelectedInstRegOperands(*New, TII, TRI, RBI);
    return true;
  }
  bool fix(MachineInstrBuilder New) const {
    constrainSelectedInstRegOperands(*New, TII, TRI, RBI);
    return true;
  }
  std::optional<int64_t> constantOf(Register R) const {
    if (auto C = getIConstantVRegValWithLookThrough(R, *MRI))
      return C->Value.getSExtValue();
    return std::nullopt;
  }
  /// The carry flag and the scratch byte an instruction changes are unused
  /// afterwards, except between the links of a carry chain. Saying so lets
  /// later passes move and remove the instruction.
  static MachineInstrBuilder markDefsDead(MachineInstrBuilder MIB) {
    for (MachineOperand &MO : MIB->operands())
      if (MO.isReg() && MO.isImplicit() && MO.isDef() &&
          (MO.getReg() == PIC16F1::CARRY || MO.getReg() == PIC16F1::TMP0))
        MO.setIsDead();
    return MIB;
  }
  static void carryIsLive(MachineInstrBuilder MIB) {
    for (MachineOperand &MO : MIB->operands())
      if (MO.isReg() && MO.isImplicit() && MO.isDef() &&
          MO.getReg() == PIC16F1::CARRY)
        MO.setIsDead(false);
  }
  MachineInstrBuilder build(MachineInstr &Before, unsigned Opc) const {
    return markDefsDead(BuildMI(*Before.getParent(), Before,
                                Before.getDebugLoc(), TII.get(Opc)));
  }
  MachineInstrBuilder build(MachineInstr &Before, unsigned Opc,
                            Register Dst) const {
    return markDefsDead(BuildMI(*Before.getParent(), Before,
                                Before.getDebugLoc(), TII.get(Opc), Dst));
  }
  /// A register holding the constant \p V.
  Register materialize(MachineInstr &Before, int64_t V, unsigned Bits) const {
    Register R = newReg(Bits);
    build(Before, Bits == 8 ? PIC16F1::LDI8 : PIC16F1::LDI16, R)
        .addImm(V & (Bits == 8 ? 0xFF : 0xFFFF));
    return R;
  }

  // --- Comparisons ---------------------------------------------------------
  struct Compare {
    unsigned CC;
    Register LHS, RHS;        // RHS is invalid when RHSImm is used
    std::optional<int64_t> RHSImm;
    bool Wide;                // 16-bit operands
  };
  /// If \p Cond is (possibly through extensions) the result of a G_ICMP,
  /// return it in the form the branch pseudos take.
  std::optional<Compare> matchCompare(Register Cond, MachineInstr &Before) const;
  std::optional<Compare> matchBitTest(CmpInst::Predicate P, Register LHS,
                                      Register RHS, MachineInstr &Before) const;
  Compare normalize(CmpInst::Predicate P, Register LHS, Register RHS) const;
  /// Compare \p C as an operand list: condition, left, right register.
  void addCompareOperands(MachineInstr &Before, MachineInstrBuilder &MIB,
                          const Compare &C, bool AllowImm) const;

  // --- Addresses -----------------------------------------------------------
  Address matchAddress(Register Ptr, unsigned AccessBytes) const;
  bool isDirectlyAddressable(const GlobalValue *GV) const {
    // A writable global is in RAM, where banked addressing reaches it.
    // Constants are in flash and are read through a pointer.
    const auto *Var = dyn_cast<GlobalVariable>(GV);
    return Var && !Var->isConstant();
  }
  void addAddressOperands(MachineInstrBuilder &MIB, const Address &A) const;
  static unsigned memOpcode(bool IsLoad, bool Wide, Address::KindTy K);

  // --- Opcodes -------------------------------------------------------------
  bool selectCopy(MachineInstr &I) const;
  bool selectConstant(MachineInstr &I) const;
  bool selectBinary(MachineInstr &I) const;
  bool selectPtrAdd(MachineInstr &I) const;
  bool selectExt(MachineInstr &I) const;
  bool selectMerge(MachineInstr &I) const;
  bool selectUnmerge(MachineInstr &I) const;
  bool selectLoad(MachineInstr &I) const;
  bool selectStore(MachineInstr &I) const;
  bool selectReadModifyWrite(MachineInstr &Store, const Address &A) const;
  bool selectICmp(MachineInstr &I) const;
  bool selectBrCond(MachineInstr &I) const;
  bool selectSelect(MachineInstr &I) const;
  bool selectShift(MachineInstr &I) const;
  bool selectCarryOp(MachineInstr &I) const;
  bool selectRightShiftLink(MachineInstr &I) const;
  bool takeCarryMarkers(Register CarryOut, bool &FlagUsed) const;
  bool carryIsInFlag(const MachineInstr &User, Register Carry) const;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Comparisons
//===----------------------------------------------------------------------===//

PIC16F1InstructionSelector::Compare
PIC16F1InstructionSelector::normalize(CmpInst::Predicate P, Register LHS,
                                      Register RHS) const {
  // The pseudos take EQ, NE, ULT, UGE, LT and GE. For the others either
  // swap the operands, or use that x > C is x >= C+1 and x <= C is x < C+1
  // when C+1 does not overflow.
  unsigned Bits = sizeOf(LHS);
  Compare C;
  C.Wide = Bits == 16;
  C.LHS = LHS;
  C.RHS = RHS;
  std::optional<int64_t> K = constantOf(RHS);
  auto Bump = [&](bool Signed) {
    if (!K)
      return false;
    APInt V(Bits, uint64_t(*K), /*isSigned=*/false, /*implicitTrunc=*/true);
    if (Signed ? V.isMaxSignedValue() : V.isMaxValue())
      return false;
    K = (V + 1).getSExtValue();
    return true;
  };
  bool Swap = false;
  switch (P) {
  case CmpInst::ICMP_EQ:
    C.CC = PIC16F1CC::COND_EQ;
    break;
  case CmpInst::ICMP_NE:
    C.CC = PIC16F1CC::COND_NE;
    break;
  case CmpInst::ICMP_ULT:
    C.CC = PIC16F1CC::COND_ULT;
    break;
  case CmpInst::ICMP_UGE:
    C.CC = PIC16F1CC::COND_UGE;
    break;
  case CmpInst::ICMP_SLT:
    C.CC = PIC16F1CC::COND_LT;
    break;
  case CmpInst::ICMP_SGE:
    C.CC = PIC16F1CC::COND_GE;
    break;
  case CmpInst::ICMP_UGT:
    C.CC = Bump(false) ? PIC16F1CC::COND_UGE : PIC16F1CC::COND_ULT;
    Swap = C.CC == PIC16F1CC::COND_ULT;
    break;
  case CmpInst::ICMP_ULE:
    C.CC = Bump(false) ? PIC16F1CC::COND_ULT : PIC16F1CC::COND_UGE;
    Swap = C.CC == PIC16F1CC::COND_UGE;
    break;
  case CmpInst::ICMP_SGT:
    C.CC = Bump(true) ? PIC16F1CC::COND_GE : PIC16F1CC::COND_LT;
    Swap = C.CC == PIC16F1CC::COND_LT;
    break;
  case CmpInst::ICMP_SLE:
    C.CC = Bump(true) ? PIC16F1CC::COND_LT : PIC16F1CC::COND_GE;
    Swap = C.CC == PIC16F1CC::COND_GE;
    break;
  default:
    llvm_unreachable("not an integer predicate");
  }
  if (Swap) {
    std::swap(C.LHS, C.RHS);
    // The constant, if any, is now on the left and must be in a register.
  } else if (K) {
    C.RHSImm = K;
    C.RHS = Register();
  }
  return C;
}

/// "(x & bit) == 0" and "!= 0" test one bit of one byte, which BTFSC and
/// BTFSS do directly.
std::optional<PIC16F1InstructionSelector::Compare>
PIC16F1InstructionSelector::matchBitTest(CmpInst::Predicate P, Register LHS,
                                         Register RHS,
                                         MachineInstr &Before) const {
  if (!CmpInst::isEquality(P) || constantOf(RHS) != 0)
    return std::nullopt;
  MachineInstr *And = getDefIgnoringCopies(LHS, *MRI);
  if (!And || And->getOpcode() != G_AND)
    return std::nullopt;
  auto Mask = constantOf(And->getOperand(2).getReg());
  if (!Mask)
    return std::nullopt;
  unsigned Bits = sizeOf(LHS);
  uint64_t M = uint64_t(*Mask) & (Bits == 16 ? 0xFFFF : 0xFF);
  if (!isPowerOf2_64(M))
    return std::nullopt;
  unsigned Bit = Log2_64(M);
  Register Src = And->getOperand(1).getReg();
  // The byte holding the bit. A 16-bit value may itself be an extended
  // byte.
  Register Byte = Src;
  if (Bits == 16) {
    MachineInstr *SrcDef = getDefIgnoringCopies(Src, *MRI);
    if (Bit < 8 && SrcDef &&
        (SrcDef->getOpcode() == G_ZEXT || SrcDef->getOpcode() == G_SEXT ||
         SrcDef->getOpcode() == G_ANYEXT) &&
        sizeOf(SrcDef->getOperand(1).getReg()) == 8) {
      Byte = SrcDef->getOperand(1).getReg();
    } else {
      if (!constrain(Src))
        return std::nullopt;
      Byte = newReg(8);
      build(Before, COPY, Byte)
          .addUse(Src, {}, Bit < 8 ? PIC16F1::sub_lo : PIC16F1::sub_hi);
    }
    Bit &= 7;
  }
  Compare C;
  C.CC = P == CmpInst::ICMP_NE ? PIC16F1CC::COND_BS : PIC16F1CC::COND_BC;
  C.LHS = Byte;
  C.RHSImm = Bit;
  C.Wide = false;
  return C;
}

std::optional<PIC16F1InstructionSelector::Compare>
PIC16F1InstructionSelector::matchCompare(Register Cond,
                                         MachineInstr &Before) const {
  // A comparison result is 0 or 1, so extending it, truncating it or masking
  // it with 1 changes nothing.
  // "xor 1" inverts it.
  bool Invert = false;
  for (unsigned Depth = 0; Depth != 6; ++Depth) {
    MachineInstr *Def = getDefIgnoringCopies(Cond, *MRI);
    if (!Def)
      return std::nullopt;
    switch (Def->getOpcode()) {
    case G_ICMP: {
      Register L = Def->getOperand(2).getReg();
      unsigned Bits = sizeOf(L);
      if (Bits != 8 && Bits != 16)
        return std::nullopt;
      auto P =
          static_cast<CmpInst::Predicate>(Def->getOperand(1).getPredicate());
      if (Invert)
        P = CmpInst::getInversePredicate(P);
      if (auto BT = matchBitTest(P, L, Def->getOperand(3).getReg(), Before))
        return BT;
      return normalize(P, L, Def->getOperand(3).getReg());
    }
    case G_ZEXT:
    case G_ANYEXT:
    case G_TRUNC:
    case G_FREEZE:
      Cond = Def->getOperand(1).getReg();
      continue;
    case G_XOR:
      if (constantOf(Def->getOperand(2).getReg()) != 1)
        return std::nullopt;
      Invert = !Invert;
      Cond = Def->getOperand(1).getReg();
      continue;
    case G_AND:
      if (constantOf(Def->getOperand(2).getReg()) != 1)
        return std::nullopt;
      Cond = Def->getOperand(1).getReg();
      continue;
    default:
      return std::nullopt;
    }
  }
  return std::nullopt;
}

void PIC16F1InstructionSelector::addCompareOperands(MachineInstr &Before,
                                                    MachineInstrBuilder &MIB,
                                                    const Compare &C,
                                                    bool AllowImm) const {
  MIB.addImm(C.CC);
  // Operands must be registers of the compared width; a constant on the
  // left, or anywhere when the pseudo has no immediate form, is loaded
  // into one. The load goes above the instruction being built.
  MachineInstr &Here = *MIB.getInstr();
  auto Operand = [&](Register R) {
    if (auto K = constantOf(R))
      return materialize(Here, *K, C.Wide ? 16 : 8);
    return R;
  };
  MIB.addUse(Operand(C.LHS));
  if (C.RHSImm && AllowImm)
    MIB.addImm(*C.RHSImm & (C.Wide ? 0xFFFF : 0xFF));
  else if (C.RHSImm)
    MIB.addUse(materialize(Here, *C.RHSImm, C.Wide ? 16 : 8));
  else
    MIB.addUse(Operand(C.RHS));
}

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

Address PIC16F1InstructionSelector::matchAddress(Register Ptr,
                                                 unsigned AccessBytes) const {
  Address A;
  A.Base = Ptr;
  int64_t Offset = 0;
  Register Cur = Ptr;
  // Walk down through "pointer + constant" to the thing being offset.
  for (unsigned Depth = 0; Depth != 8; ++Depth) {
    MachineInstr *Def = getDefIgnoringCopies(Cur, *MRI);
    if (!Def)
      break;
    unsigned Opc = Def->getOpcode();
    if (Opc == G_PTR_ADD) {
      auto K = constantOf(Def->getOperand(2).getReg());
      if (!K)
        break;
      Offset += *K;
      Cur = Def->getOperand(1).getReg();
      continue;
    }
    if (Opc == G_FRAME_INDEX && Offset >= 0) {
      A.Kind = Address::Frame;
      A.FrameIndex = Def->getOperand(1).getIndex();
      A.Offset = Offset;
      return A;
    }
    if (Opc == G_GLOBAL_VALUE &&
        isDirectlyAddressable(Def->getOperand(1).getGlobal())) {
      A.Kind = Address::Global;
      A.GV = Def->getOperand(1).getGlobal();
      A.Offset = Offset + Def->getOperand(1).getOffset();
      return A;
    }
    if (Opc == G_INTTOPTR || Opc == G_CONSTANT) {
      // A constant banked address, such as a special function register.
      Register Int = Opc == G_INTTOPTR ? Def->getOperand(1).getReg() : Cur;
      if (auto K = constantOf(Int)) {
        uint64_t Addr = uint64_t(*K + Offset) & 0xFFFF;
        if (Addr + AccessBytes <= STI.bankedEnd()) {
          A.Kind = Address::Constant;
          A.Offset = Addr;
          return A;
        }
      }
    }
    break;
  }
  // A pointer in a register pair, with a small offset if the pointer is
  // "something + constant" and the whole access stays within reach. Of a
  // chain of such additions, take the innermost base that is in reach: the
  // pieces of a split wide access then share one base.
  A.Kind = Address::Pointer;
  A.Base = Ptr;
  A.Offset = 0;
  Cur = Ptr;
  Offset = 0;
  for (unsigned Depth = 0; Depth != 8; ++Depth) {
    MachineInstr *Def = getDefIgnoringCopies(Cur, *MRI);
    if (!Def || Def->getOpcode() != G_PTR_ADD)
      break;
    auto K = constantOf(Def->getOperand(2).getReg());
    if (!K)
      break;
    // A pointer that steps through a loop is wanted in a register after
    // the step anyway. Addressing from it, and not from the pointer before
    // the step, lets the step happen in place (in FSR0, when it is free).
    if (any_of(MRI->use_nodbg_instructions(Def->getOperand(0).getReg()),
               [](const MachineInstr &U) { return U.isPHI(); }))
      break;
    Offset += *K;
    Cur = Def->getOperand(1).getReg();
    if (Offset >= -32 && Offset + int64_t(AccessBytes) - 1 <= 31) {
      A.Base = Cur;
      A.Offset = Offset;
    }
  }
  return A;
}

unsigned PIC16F1InstructionSelector::memOpcode(bool IsLoad, bool Wide,
                                               Address::KindTy K) {
  switch (K) {
  case Address::Frame:
    return IsLoad ? (Wide ? LD16fi : LD8fi) : (Wide ? ST16fi : ST8fi);
  case Address::Global:
  case Address::Constant:
    return IsLoad ? (Wide ? LD16abs : LD8abs) : (Wide ? ST16abs : ST8abs);
  case Address::Pointer:
    return IsLoad ? (Wide ? LD16ptr : LD8ptr) : (Wide ? ST16ptr : ST8ptr);
  }
  llvm_unreachable("unknown address kind");
}

/// The store-a-constant opcode for an address kind.
static unsigned storeImmOpcode(bool Wide, Address::KindTy K) {
  switch (K) {
  case Address::Frame:
    return Wide ? STI16fi : STI8fi;
  case Address::Global:
  case Address::Constant:
    return Wide ? STI16abs : STI8abs;
  case Address::Pointer:
    return Wide ? STI16ptr : STI8ptr;
  }
  llvm_unreachable("unknown address kind");
}

void PIC16F1InstructionSelector::addAddressOperands(MachineInstrBuilder &MIB,
                                                    const Address &A) const {
  switch (A.Kind) {
  case Address::Frame:
    MIB.addFrameIndex(A.FrameIndex).addImm(A.Offset);
    break;
  case Address::Global:
    MIB.addGlobalAddress(A.GV, A.Offset);
    break;
  case Address::Constant:
    MIB.addImm(A.Offset);
    break;
  case Address::Pointer:
    MIB.addUse(A.Base).addImm(A.Offset);
    break;
  }
}

//===----------------------------------------------------------------------===//
// Selection
//===----------------------------------------------------------------------===//

bool PIC16F1InstructionSelector::selectCopy(MachineInstr &I) const {
  // Copies to and from physical registers, and between virtual registers
  // that have no class yet.
  for (MachineOperand &MO : I.operands())
    if (MO.isReg() && MO.getReg().isVirtual() &&
        !MRI->getRegClassOrNull(MO.getReg()) && !constrain(MO.getReg()))
      return false;
  return true;
}

bool PIC16F1InstructionSelector::selectConstant(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  switch (I.getOpcode()) {
  case G_CONSTANT: {
    int64_t V = I.getOperand(1).isCImm()
                    ? I.getOperand(1).getCImm()->getSExtValue()
                    : I.getOperand(1).getImm();
    bool Wide = is16(Dst);
    return done(I, build(I, Wide ? PIC16F1::LDI16 : PIC16F1::LDI8, Dst)
                       .addImm(V & (Wide ? 0xFFFF : 0xFF)));
  }
  case G_GLOBAL_VALUE: {
    const GlobalValue *GV = I.getOperand(1).getGlobal();
    const GlobalObject *Obj = GV->getAliaseeObject();
    // A function's address is its word address.
    bool Code = isa<Function>(Obj ? cast<GlobalValue>(Obj) : GV);
    return done(I, build(I, PIC16F1::LDI16, Dst)
                       .addGlobalAddress(GV, I.getOperand(1).getOffset(),
                                         Code ? PIC16F1II::MO_PM
                                              : PIC16F1II::MO_NONE));
  }
  case G_BLOCK_ADDR:
    return done(I, build(I, PIC16F1::LDI16, Dst)
                       .addBlockAddress(I.getOperand(1).getBlockAddress(), 0,
                                        PIC16F1II::MO_PM));
  case G_FRAME_INDEX:
    return done(I, build(I, PIC16F1::LEAfi, Dst)
                       .addFrameIndex(I.getOperand(1).getIndex())
                       .addImm(0));
  default:
    return false;
  }
}

bool PIC16F1InstructionSelector::selectBinary(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  Register L = I.getOperand(1).getReg();
  Register R = I.getOperand(2).getReg();
  bool Wide = is16(Dst);
  unsigned RR, RI = 0;
  switch (I.getOpcode()) {
  case G_ADD:
    RR = Wide ? ADD16rr : ADD8rr;
    RI = Wide ? ADD16ri : ADD8ri;
    break;
  case G_SUB:
    RR = Wide ? SUB16rr : SUB8rr;
    break;
  case G_AND:
    RR = Wide ? AND16rr : AND8rr;
    RI = Wide ? AND16ri : AND8ri;
    break;
  case G_OR:
    RR = Wide ? OR16rr : OR8rr;
    RI = Wide ? OR16ri : OR8ri;
    break;
  default:
    RR = Wide ? XOR16rr : XOR8rr;
    RI = Wide ? XOR16ri : XOR8ri;
    break;
  }
  int64_t Mask = Wide ? 0xFFFF : 0xFF;
  std::optional<int64_t> K = constantOf(R);
  if (I.getOpcode() == G_SUB) {
    // 0 - x is a negation; x - C is x + (-C).
    if (constantOf(L) == 0)
      return done(I, build(I, Wide ? NEG16 : NEG8, Dst).addUse(R));
    if (K)
      return done(I, build(I, Wide ? ADD16ri : ADD8ri, Dst)
                         .addUse(L)
                         .addImm(-*K & Mask));
  } else if (K) {
    if (I.getOpcode() == G_XOR && (*K & Mask) == Mask)
      return done(I, build(I, Wide ? NOT16 : NOT8, Dst).addUse(L));
    return done(I, build(I, RI, Dst).addUse(L).addImm(*K & Mask));
  }
  return done(I, build(I, RR, Dst).addUse(L).addUse(R));
}

bool PIC16F1InstructionSelector::selectPtrAdd(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  Register Base = I.getOperand(1).getReg();
  Register Off = I.getOperand(2).getReg();
  MachineInstr *BaseDef = getDefIgnoringCopies(Base, *MRI);
  // A data symbol is a constant: fold a constant offset into it, and add it
  // to an index as an immediate.
  bool BaseIsData = BaseDef && BaseDef->getOpcode() == G_GLOBAL_VALUE &&
                    isa<GlobalVariable>(BaseDef->getOperand(1).getGlobal());
  if (auto K = constantOf(Off)) {
    if (BaseIsData)
      return done(I, build(I, PIC16F1::LDI16, Dst)
                         .addGlobalAddress(BaseDef->getOperand(1).getGlobal(),
                                           BaseDef->getOperand(1).getOffset() +
                                               *K));
    // The address of a stack slot plus a constant is one instruction.
    if (BaseDef && BaseDef->getOpcode() == G_FRAME_INDEX && *K >= 0)
      return done(I, build(I, PIC16F1::LEAfi, Dst)
                         .addFrameIndex(BaseDef->getOperand(1).getIndex())
                         .addImm(*K));
    return done(I, build(I, PIC16F1::ADD16ri, Dst)
                       .addUse(Base)
                       .addImm(*K & 0xFFFF));
  }
  if (BaseIsData)
    return done(I, build(I, PIC16F1::ADD16ri, Dst)
                       .addUse(Off)
                       .addGlobalAddress(BaseDef->getOperand(1).getGlobal(),
                                         BaseDef->getOperand(1).getOffset()));
  return done(I, build(I, PIC16F1::ADD16rr, Dst).addUse(Base).addUse(Off));
}

bool PIC16F1InstructionSelector::selectExt(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  Register Src = I.getOperand(1).getReg();
  switch (I.getOpcode()) {
  case G_ZEXT:
    return done(I, build(I, PIC16F1::ZEXT16, Dst).addUse(Src));
  case G_SEXT:
    return done(I, build(I, PIC16F1::SEXT16, Dst).addUse(Src));
  case G_ANYEXT: {
    // The high byte is undefined: place the source in the low half.
    Register Undef = newReg(16);
    build(I, IMPLICIT_DEF, Undef);
    if (!constrain(Src) || !constrain(Dst))
      return false;
    build(I, INSERT_SUBREG, Dst)
        .addUse(Undef)
        .addUse(Src)
        .addImm(PIC16F1::sub_lo);
    I.eraseFromParent();
    return true;
  }
  case G_TRUNC: {
    if (!constrain(Src) || !constrain(Dst))
      return false;
    build(I, COPY, Dst).addUse(Src, {}, PIC16F1::sub_lo);
    I.eraseFromParent();
    return true;
  }
  default:
    return false;
  }
}

bool PIC16F1InstructionSelector::selectMerge(MachineInstr &I) const {
  // 16 bits from a low and a high byte.
  Register Dst = I.getOperand(0).getReg();
  Register Lo = I.getOperand(1).getReg();
  Register Hi = I.getOperand(2).getReg();
  if (!constrain(Dst) || !constrain(Lo) || !constrain(Hi))
    return false;
  build(I, REG_SEQUENCE, Dst)
      .addUse(Lo)
      .addImm(PIC16F1::sub_lo)
      .addUse(Hi)
      .addImm(PIC16F1::sub_hi);
  I.eraseFromParent();
  return true;
}

bool PIC16F1InstructionSelector::selectUnmerge(MachineInstr &I) const {
  Register Lo = I.getOperand(0).getReg();
  Register Hi = I.getOperand(1).getReg();
  Register Src = I.getOperand(2).getReg();
  if (!constrain(Src) || !constrain(Lo) || !constrain(Hi))
    return false;
  build(I, COPY, Lo).addUse(Src, {}, PIC16F1::sub_lo);
  build(I, COPY, Hi).addUse(Src, {}, PIC16F1::sub_hi);
  I.eraseFromParent();
  return true;
}

bool PIC16F1InstructionSelector::selectLoad(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  bool Wide = is16(Dst);
  Address A = matchAddress(I.getOperand(1).getReg(), Wide ? 2 : 1);
  MachineInstrBuilder MIB = build(I, memOpcode(true, Wide, A.Kind), Dst);
  addAddressOperands(MIB, A);
  MIB.cloneMemRefs(I);
  return done(I, MIB);
}

/// *p = *p OP x as one instruction on the file register, when p is a direct
/// address. The byte is read once and written once, as the separate load and
/// store would, so this also holds for volatile registers.
bool PIC16F1InstructionSelector::selectReadModifyWrite(MachineInstr &Store,
                                                       const Address &A) const {
  Register Val = Store.getOperand(0).getReg();
  unsigned Bits = sizeOf(Val);
  if ((Bits != 8 && Bits != 16) || !MRI->hasOneNonDBGUse(Val))
    return false;
  bool Wide = Bits == 16;
  MachineInstr *Op = MRI->getVRegDef(Val);
  if (!Op || Op->getParent() != Store.getParent())
    return false;
  // MI is 0 where there is no form with a constant.
  unsigned MI, MR;
  switch (Op->getOpcode()) {
  case G_AND:
    MI = Wide ? AND16mi : AND8mi;
    MR = Wide ? AND16mr : AND8mr;
    break;
  case G_OR:
    MI = Wide ? OR16mi : OR8mi;
    MR = Wide ? OR16mr : OR8mr;
    break;
  case G_XOR:
    MI = Wide ? XOR16mi : XOR8mi;
    MR = Wide ? XOR16mr : XOR8mr;
    break;
  case G_ADD:
    MI = Wide ? ADD16mi : ADD8mi;
    MR = Wide ? ADD16mr : ADD8mr;
    break;
  case G_SUB:
    MI = 0;
    MR = Wide ? SUB16mr : SUB8mr;
    break;
  default:
    return false;
  }
  // One operand must be a load of the same address, used only here.
  for (unsigned LoadIdx : {1u, 2u}) {
    // Only memory - x, not x - memory.
    if (LoadIdx == 2 && Op->getOpcode() == G_SUB)
      break;
    Register LoadReg = Op->getOperand(LoadIdx).getReg();
    Register Other = Op->getOperand(3 - LoadIdx).getReg();
    MachineInstr *Load = MRI->getVRegDef(LoadReg);
    if (!Load || Load->getOpcode() != G_LOAD ||
        Load->getParent() != Store.getParent() ||
        !MRI->hasOneNonDBGUse(LoadReg))
      continue;
    Address LA = matchAddress(Load->getOperand(1).getReg(), Wide ? 2 : 1);
    if (LA.Kind != A.Kind || LA.GV != A.GV || LA.Offset != A.Offset)
      continue;
    // The read moves down to the store. Nothing in between may write
    // memory or have other effects that the move could reorder.
    bool Safe = true;
    for (auto It = std::next(Load->getIterator()); &*It != &Store; ++It)
      if (It->mayStore() || It->isCall() || It->hasUnmodeledSideEffects() ||
          (It->mayLoad() && It->getOpcode() != G_LOAD)) {
        Safe = false;
        break;
      }
    if (!Safe)
      continue;
    MachineInstrBuilder MIB;
    if (auto K = constantOf(Other); K && MI) {
      MIB = build(Store, MI);
      addAddressOperands(MIB, A);
      MIB.addImm(*K & (Wide ? 0xFFFF : 0xFF));
    } else {
      MIB = build(Store, MR);
      addAddressOperands(MIB, A);
      MIB.addUse(Other);
    }
    MIB.cloneMergedMemRefs({Load, &Store});
    Store.eraseFromParent();
    Op->eraseFromParent();
    Load->eraseFromParent();
    return fix(MIB);
  }
  return false;
}

bool PIC16F1InstructionSelector::selectStore(MachineInstr &I) const {
  Register Val = I.getOperand(0).getReg();
  bool Wide = is16(Val);
  Address A = matchAddress(I.getOperand(1).getReg(), Wide ? 2 : 1);
  if ((A.Kind == Address::Global || A.Kind == Address::Constant) &&
      selectReadModifyWrite(I, A))
    return true;
  // A constant goes through W and needs no register.
  if (auto K = constantOf(Val)) {
    MachineInstrBuilder MIB = build(I, storeImmOpcode(Wide, A.Kind))
                                  .addImm(*K & (Wide ? 0xFFFF : 0xFF));
    addAddressOperands(MIB, A);
    MIB.cloneMemRefs(I);
    return done(I, MIB);
  }
  MachineInstrBuilder MIB =
      build(I, memOpcode(false, Wide, A.Kind)).addUse(Val);
  addAddressOperands(MIB, A);
  MIB.cloneMemRefs(I);
  return done(I, MIB);
}

bool PIC16F1InstructionSelector::selectICmp(MachineInstr &I) const {
  // A comparison whose result is needed as a value: select 1 or 0.
  Register Dst = I.getOperand(0).getReg();
  auto Pred = static_cast<CmpInst::Predicate>(I.getOperand(1).getPredicate());
  Compare C;
  if (auto BT = matchBitTest(Pred, I.getOperand(2).getReg(),
                             I.getOperand(3).getReg(), I))
    C = *BT;
  else
    C = normalize(Pred, I.getOperand(2).getReg(), I.getOperand(3).getReg());
  Register One = materialize(I, 1, 8);
  Register Zero = materialize(I, 0, 8);
  MachineInstrBuilder MIB =
      build(I, C.Wide ? PIC16F1::SELECT8c16 : PIC16F1::SELECT8c8, Dst);
  addCompareOperands(I, MIB, C, /*AllowImm=*/false);
  MIB.addUse(One).addUse(Zero);
  return done(I, MIB);
}

bool PIC16F1InstructionSelector::selectBrCond(MachineInstr &I) const {
  Register Cond = I.getOperand(0).getReg();
  MachineBasicBlock *Target = I.getOperand(1).getMBB();
  if (auto C = matchCompare(Cond, I)) {
    bool Imm = C->RHSImm.has_value();
    unsigned Opc = C->Wide ? (Imm ? BRCC16ri : BRCC16rr)
                           : (Imm ? BRCC8ri : BRCC8rr);
    MachineInstrBuilder MIB = build(I, Opc);
    addCompareOperands(I, MIB, *C, /*AllowImm=*/true);
    MIB.addMBB(Target);
    return done(I, MIB);
  }
  return done(I, build(I, BRCC8ri)
                     .addImm(PIC16F1CC::COND_NE)
                     .addUse(Cond)
                     .addImm(0)
                     .addMBB(Target));
}

bool PIC16F1InstructionSelector::selectSelect(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  Register Cond = I.getOperand(1).getReg();
  Register T = I.getOperand(2).getReg();
  Register F = I.getOperand(3).getReg();
  bool Wide = is16(Dst);
  Compare C;
  if (auto M = matchCompare(Cond, I)) {
    C = *M;
  } else {
    C.CC = PIC16F1CC::COND_NE;
    C.LHS = Cond;
    C.RHSImm = 0;
    C.Wide = false;
  }
  unsigned Opc = Wide ? (C.Wide ? SELECT16c16 : SELECT16c8)
                      : (C.Wide ? SELECT8c16 : SELECT8c8);
  // The value operands must be registers.
  auto Value = [&](Register R) {
    if (auto K = constantOf(R))
      return materialize(I, *K, Wide ? 16 : 8);
    return R;
  };
  Register TV = Value(T), FV = Value(F);
  MachineInstrBuilder MIB = build(I, Opc, Dst);
  addCompareOperands(I, MIB, C, /*AllowImm=*/false);
  MIB.addUse(TV).addUse(FV);
  return done(I, MIB);
}

bool PIC16F1InstructionSelector::selectShift(MachineInstr &I) const {
  Register Dst = I.getOperand(0).getReg();
  Register Src = I.getOperand(1).getReg();
  Register Amt = I.getOperand(2).getReg();
  bool Wide = is16(Dst);
  unsigned Opc = I.getOpcode();
  unsigned One8 = Opc == G_SHL ? SHL8 : Opc == G_LSHR ? LSR8 : ASR8;
  unsigned One16 = Opc == G_SHL ? SHL16 : Opc == G_LSHR ? LSR16 : ASR16;

  std::optional<int64_t> K = constantOf(Amt);
  if (!K) {
    unsigned Var = Wide ? (Opc == G_SHL ? SHLV16 : Opc == G_LSHR ? LSRV16 : ASRV16)
                        : (Opc == G_SHL ? SHLV8 : Opc == G_LSHR ? LSRV8 : ASRV8);
    return done(I, build(I, Var, Dst).addUse(Src).addUse(Amt));
  }
  uint64_t N = uint64_t(*K);
  if (N >= (Wide ? 16u : 8u)) {
    // The result is undefined.
    build(I, IMPLICIT_DEF, Dst);
    I.eraseFromParent();
    return constrain(Dst);
  }
  if (!constrain(Src) || !constrain(Dst))
    return false;
  if (N == 0) {
    build(I, COPY, Dst).addUse(Src);
    I.eraseFromParent();
    return true;
  }
  // The top bit of a byte as 0 or 1: a test of the sign.
  auto TopBit = [&](Register Byte, Register To) {
    Register One = materialize(I, 1, 8);
    Register Zero = materialize(I, 0, 8);
    build(I, SELECT8c8, To)
        .addImm(PIC16F1CC::COND_LT)
        .addUse(Byte)
        .addUse(Zero)
        .addUse(One)
        .addUse(Zero);
  };
  if (!Wide && N == 7 && Opc == G_LSHR) {
    TopBit(Src, Dst);
    I.eraseFromParent();
    return true;
  }
  // Apply \p Count one-bit shifts to \p From, ending in \p To.
  auto Chain = [&](unsigned ShiftOpc, unsigned Bits, Register From,
                   unsigned Count, Register To) {
    for (unsigned S = 0; S != Count; ++S) {
      Register Next = S + 1 == Count ? To : newReg(Bits);
      build(I, ShiftOpc, Next).addUse(From);
      From = Next;
    }
  };
  // An 8-bit shift. Four places at once is an exchange of the halves and
  // a mask.
  auto Shift8 = [&](Register From, unsigned Count, Register To) {
    if (Count >= 4 && Opc != G_ASHR) {
      Register Swapped = newReg(8);
      build(I, SWAP8, Swapped).addUse(From);
      Register Masked = Count == 4 ? To : newReg(8);
      build(I, AND8ri, Masked)
          .addUse(Swapped)
          .addImm(Opc == G_SHL ? 0xF0 : 0x0F);
      From = Masked;
      Count -= 4;
    }
    Chain(One8, 8, From, Count, To);
  };
  if (Wide && N >= 8) {
    // A 16-bit shift by eight or more is a byte move and an 8-bit shift.
    Register Byte = newReg(8);
    build(I, COPY, Byte)
        .addUse(Src, {}, Opc == G_SHL ? PIC16F1::sub_lo : PIC16F1::sub_hi);
    Register Shifted = Byte;
    if (N > 8) {
      Shifted = newReg(8);
      if (N == 15 && Opc == G_LSHR)
        TopBit(Byte, Shifted);
      else
        Shift8(Byte, N - 8, Shifted);
    }
    if (Opc == G_SHL) {
      Register Zero = materialize(I, 0, 8);
      build(I, REG_SEQUENCE, Dst)
          .addUse(Zero)
          .addImm(PIC16F1::sub_lo)
          .addUse(Shifted)
          .addImm(PIC16F1::sub_hi);
    } else {
      build(I, Opc == G_LSHR ? ZEXT16 : SEXT16, Dst).addUse(Shifted);
    }
    I.eraseFromParent();
    return true;
  }
  if (Wide)
    Chain(One16, 16, Src, N, Dst);
  else
    Shift8(Src, N, Dst);
  I.eraseFromParent();
  return true;
}

/// True if the carry value \p Carry, consumed by \p User, can stay in the
/// carry flag: it is produced by an add or subtract with carry earlier in
/// the same block, and everything between the two leaves the flag alone.
bool PIC16F1InstructionSelector::carryIsInFlag(const MachineInstr &User,
                                               Register Carry) const {
  const MachineInstr *Def = MRI->getVRegDef(Carry);
  if (!Def || Def->getParent() != User.getParent())
    return false;
  switch (Def->getOpcode()) {
  case G_UADDO:
  case G_UADDE:
    if (User.getOpcode() != G_UADDE)
      return false;
    break;
  case G_USUBO:
  case G_USUBE:
    if (User.getOpcode() != G_USUBE)
      return false;
    break;
  case PIC16F1::G_LSR1:
  case PIC16F1::G_ASR1:
  case PIC16F1::G_RRC:
    if (User.getOpcode() != PIC16F1::G_RRC)
      return false;
    break;
  default:
    return false;
  }
  // These select to moves, loads and stores, none of which changes carry.
  for (auto It = std::next(Def->getIterator()); &*It != &User; ++It) {
    switch (It->getOpcode()) {
    case COPY:
    case G_CONSTANT:
    case G_IMPLICIT_DEF:
    case G_MERGE_VALUES:
    case G_UNMERGE_VALUES:
    case G_TRUNC:
    case G_ANYEXT:
    case G_ZEXT:
    case G_SEXT:
    case G_LOAD:
    case G_STORE:
    case G_FREEZE:
      continue;
    default:
      if (It->isDebugInstr())
        continue;
      return false;
    }
  }
  return true;
}

/// Removes the markers later links left on \p CarryOut. Returns true if
/// anything else uses it, in which case it is needed as a value.
bool PIC16F1InstructionSelector::takeCarryMarkers(Register CarryOut,
                                                  bool &FlagUsed) const {
  bool NeedValue = false;
  SmallVector<MachineInstr *, 2> Markers;
  for (MachineInstr &Use : MRI->use_nodbg_instructions(CarryOut)) {
    if (Use.getOpcode() == CARRYUSE)
      Markers.push_back(&Use);
    else
      NeedValue = true;
  }
  for (MachineInstr *M : Markers)
    M->eraseFromParent();
  FlagUsed = NeedValue || !Markers.empty();
  return NeedValue;
}

/// One 16-bit link of a one-bit right shift of a wide value. The bit that
/// leaves stays in the carry flag for the next link down.
bool PIC16F1InstructionSelector::selectRightShiftLink(MachineInstr &I) const {
  unsigned Opc = I.getOpcode();
  Register Dst = I.getOperand(0).getReg();
  Register CarryOut = I.getOperand(1).getReg();
  Register Src = I.getOperand(2).getReg();
  bool HasCarryIn = Opc == PIC16F1::G_RRC;

  bool FlagUsed = false;
  bool NeedValue = takeCarryMarkers(CarryOut, FlagUsed);
  if (HasCarryIn && !carryIsInFlag(I, I.getOperand(3).getReg()))
    return false;
  if (auto K = constantOf(Src))
    Src = materialize(I, *K, 16);
  unsigned NewOpc = Opc == PIC16F1::G_LSR1   ? LSR16
                    : Opc == PIC16F1::G_ASR1 ? ASR16
                                             : ROR16;
  MachineInstrBuilder Op = build(I, NewOpc, Dst).addUse(Src);
  if (FlagUsed)
    carryIsLive(Op);
  if (HasCarryIn)
    build(I, CARRYUSE).addUse(I.getOperand(3).getReg());
  if (NeedValue) {
    if (!constrain(CarryOut))
      return false;
    build(I, CARRY8, CarryOut);
  }
  return done(I, Op);
}

bool PIC16F1InstructionSelector::selectCarryOp(MachineInstr &I) const {
  unsigned Opc = I.getOpcode();
  Register Dst = I.getOperand(0).getReg();
  Register CarryOut = I.getOperand(1).getReg();
  Register L = I.getOperand(2).getReg();
  Register R = I.getOperand(3).getReg();
  bool HasCarryIn = Opc == G_UADDE || Opc == G_USUBE;

  // Between the links of a chain the carry lives only in the flag. The next
  // link was selected first (selection runs bottom-up) and no longer
  // mentions the carry register. If anything else still uses the carry out,
  // it is copied from the flag into a byte right after this instruction.
  // Later links left a CARRYUSE marker so that this instruction would
  // survive until now.
  bool FlagUsed = false;
  bool NeedValue = takeCarryMarkers(CarryOut, FlagUsed);
  if (HasCarryIn && !carryIsInFlag(I, I.getOperand(4).getReg()))
    return false;

  auto Operand = [&](Register Reg) {
    if (auto K = constantOf(Reg))
      return materialize(I, *K, 16);
    return Reg;
  };
  unsigned NewOpc = Opc == G_UADDO   ? ADD16rr
                    : Opc == G_UADDE ? ADC16rr
                    : Opc == G_USUBO ? SUB16rr
                                     : SBC16rr;
  MachineInstrBuilder Op;
  if (L == R && (Opc == G_UADDO || Opc == G_UADDE)) {
    // x + x is a shift left by one; with carry in, a rotate.
    Op = build(I, Opc == G_UADDO ? SHL16 : ROL16, Dst).addUse(L);
  } else {
    Register LV = Operand(L), RV = Operand(R);
    Op = build(I, NewOpc, Dst).addUse(LV).addUse(RV);
  }
  if (FlagUsed)
    carryIsLive(Op);
  if (HasCarryIn)
    build(I, CARRYUSE).addUse(I.getOperand(4).getReg());
  if (NeedValue) {
    bool IsSub = Opc == G_USUBO || Opc == G_USUBE;
    if (!constrain(CarryOut))
      return false;
    build(I, IsSub ? BORROW8 : CARRY8, CarryOut);
  }
  return done(I, Op);
}

bool PIC16F1InstructionSelector::select(MachineInstr &I) {
  MRI = &I.getMF()->getRegInfo();
  unsigned Opc = I.getOpcode();

  if (Opc == PIC16F1::G_LSR1 || Opc == PIC16F1::G_ASR1 ||
      Opc == PIC16F1::G_RRC)
    return selectRightShiftLink(I);

  if (Opc == PIC16F1::G_ADD_IF_NEG) {
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    if (auto K = constantOf(Src))
      Src = materialize(I, *K, sizeOf(Dst));
    if (!constrain(Dst) || !constrain(Src))
      return false;
    return done(I, build(I, is16(Dst) ? ADDNEG16 : ADDNEG8, Dst)
                       .addUse(Src)
                       .addImm(I.getOperand(2).getImm()));
  }

  if (!isPreISelGenericOpcode(Opc)) {
    if (I.isCopy())
      return selectCopy(I);
    // A call argument stored on the stack by call lowering: a constant
    // needs no register.
    if (Opc == ST8fi || Opc == ST16fi) {
      Register Val = I.getOperand(0).getReg();
      if (auto K = Val.isVirtual() ? constantOf(Val) : std::nullopt) {
        bool Wide = Opc == ST16fi;
        MachineInstrBuilder MIB = build(I, Wide ? STI16fi : STI8fi)
                                      .addImm(*K & (Wide ? 0xFFFF : 0xFF))
                                      .add(I.getOperand(1))
                                      .add(I.getOperand(2));
        return done(I, MIB);
      }
    }
    return true;
  }

  switch (Opc) {
  case G_CONSTANT:
  case G_GLOBAL_VALUE:
  case G_BLOCK_ADDR:
  case G_FRAME_INDEX:
    return selectConstant(I);
  case G_IMPLICIT_DEF:
    I.setDesc(TII.get(IMPLICIT_DEF));
    return constrain(I.getOperand(0).getReg());
  case G_PHI:
    I.setDesc(TII.get(PHI));
    return constrain(I.getOperand(0).getReg());
  case G_FREEZE:
  case G_PTRTOINT:
  case G_INTTOPTR:
  case G_BITCAST: {
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    if (sizeOf(Dst) != sizeOf(Src) || !constrain(Dst) || !constrain(Src))
      return false;
    build(I, COPY, Dst).addUse(Src);
    I.eraseFromParent();
    return true;
  }
  case G_ADD:
  case G_SUB:
  case G_AND:
  case G_OR:
  case G_XOR:
    return selectBinary(I);
  case G_PTR_ADD:
    return selectPtrAdd(I);

  case G_ZEXT:
  case G_SEXT:
  case G_ANYEXT:
  case G_TRUNC:
    return selectExt(I);
  case G_MERGE_VALUES:
    return selectMerge(I);
  case G_UNMERGE_VALUES:
    return selectUnmerge(I);
  case G_LOAD:
    return selectLoad(I);
  case G_STORE:
    return selectStore(I);
  case G_ICMP:
    return selectICmp(I);
  case G_BRCOND:
    return selectBrCond(I);
  case G_BR:
    return done(I, build(I, PIC16F1::JMP).addMBB(I.getOperand(0).getMBB()));
  case G_BRINDIRECT:
    return done(I, build(I, PIC16F1::JMPreg).addUse(I.getOperand(0).getReg()));
  case G_SELECT:
    return selectSelect(I);
  case G_SHL:
  case G_LSHR:
  case G_ASHR:
    return selectShift(I);
  case G_UADDO:
  case G_UADDE:
  case G_USUBO:
  case G_USUBE:
    return selectCarryOp(I);
  default:
    return false;
  }
}

namespace llvm {
InstructionSelector *
createPIC16F1InstructionSelector(const PIC16F1TargetMachine &TM,
                                 const PIC16F1Subtarget &STI,
                                 const PIC16F1RegisterBankInfo &RBI) {
  return new PIC16F1InstructionSelector(TM, STI, RBI);
}
} // namespace llvm
