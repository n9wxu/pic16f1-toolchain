//===-- PIC16F1PreLegalizerCombiner.cpp - Combines before legalization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs the target-independent GlobalISel combines before legalization. The
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
#include "llvm/Support/CommandLine.h"

#define GET_GICOMBINER_DEPS
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "pic16f1-prelegalizer-combiner"

using namespace llvm;

/// A logic operation on two truncations.
static bool isLogicOfTruncs(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI) {
  const MachineInstr *L = MRI.getVRegDef(MI.getOperand(1).getReg());
  const MachineInstr *R = MRI.getVRegDef(MI.getOperand(2).getReg());
  return L && R && L->getOpcode() == TargetOpcode::G_TRUNC &&
         R->getOpcode() == TargetOpcode::G_TRUNC;
}

/// \p Reg is a byte extended to 16 bits whose only use is a right shift by
/// a constant: a shift of the byte, as long as the extension stays visible.
static bool feedsByteShift(Register Reg, const MachineRegisterInfo &MRI) {
  if (MRI.getType(Reg) != LLT::scalar(16) || !MRI.hasOneNonDBGUse(Reg))
    return false;
  const MachineInstr &Use = *MRI.use_instr_nodbg_begin(Reg);
  return Use.getOpcode() == TargetOpcode::G_LSHR &&
         Use.getOperand(1).getReg() == Reg &&
         getIConstantVRegValWithLookThrough(Use.getOperand(2).getReg(), MRI)
             .has_value();
}

/// \p Reg without the truncations in front of it.
static Register throughTruncs(Register Reg, const MachineRegisterInfo &MRI) {
  while (const MachineInstr *Def = MRI.getVRegDef(Reg)) {
    if (Def->getOpcode() != TargetOpcode::G_TRUNC)
      break;
    Reg = Def->getOperand(1).getReg();
  }
  return Reg;
}

/// \p Mul is (x / y) * y, possibly on the low part of the quotient with y
/// a constant: half of the remainder x - (x / y) * y. Gives the division.
static const MachineInstr *mulOfUDivBySame(const MachineInstr &Mul,
                                           const MachineRegisterInfo &MRI) {
  if (Mul.getOpcode() != TargetOpcode::G_MUL)
    return nullptr;
  const MachineInstr *Div =
      MRI.getVRegDef(throughTruncs(Mul.getOperand(1).getReg(), MRI));
  if (!Div || Div->getOpcode() != TargetOpcode::G_UDIV)
    return nullptr;
  Register Y = Div->getOperand(2).getReg();
  Register MulY = Mul.getOperand(2).getReg();
  if (Y == MulY)
    return Div;
  auto C = getIConstantVRegValWithLookThrough(Y, MRI);
  auto MulC = getIConstantVRegValWithLookThrough(MulY, MRI);
  if (C && MulC && C->Value.trunc(MulC->Value.getBitWidth()) == MulC->Value)
    return Div;
  return nullptr;
}

/// x - (x / y) * y is the remainder, which the divide routine computes
/// anyway (LLVM writes a remainder this way when it sees no instruction
/// that gives both). As a remainder it joins the division into one
/// G_UDIVREM, which is one library call. The subtraction and the multiply
/// may be on the low part of the values. Up to 32 bits; the 64-bit
/// routines are separate.
static bool matchSubMulUDiv(const MachineInstr &Sub,
                            const MachineRegisterInfo &MRI,
                            const MachineInstr *&Div) {
  const MachineInstr *Mul = MRI.getVRegDef(Sub.getOperand(2).getReg());
  if (!Mul || !(Div = mulOfUDivBySame(*Mul, MRI)))
    return false;
  LLT Ty = MRI.getType(Div->getOperand(0).getReg());
  return Ty.isScalar() && Ty.getSizeInBits() <= 32 &&
         throughTruncs(Sub.getOperand(1).getReg(), MRI) ==
             Div->getOperand(1).getReg();
}

static void applySubMulUDiv(MachineInstr &Sub, MachineRegisterInfo &MRI,
                            MachineIRBuilder &B, const MachineInstr *Div) {
  Register Dst = Sub.getOperand(0).getReg();
  LLT Wide = MRI.getType(Div->getOperand(0).getReg());
  B.setInstrAndDebugLoc(Sub);
  if (MRI.getType(Dst) == Wide) {
    B.buildURem(Dst, Div->getOperand(1), Div->getOperand(2));
  } else {
    auto Rem = B.buildURem(Wide, Div->getOperand(1), Div->getOperand(2));
    B.buildTrunc(Dst, Rem);
  }
  Sub.eraseFromParent();
}

/// What a shift by \p Amount costs in instructions per byte of the value,
/// roughly: whole bytes move for nothing, and a byte shifts four places
/// with a swap.
static unsigned shiftCost(unsigned Amount, unsigned Bits) {
  if (Bits == 8)
    return Amount < 4 ? Amount : Amount - 3;
  return Amount % 8;
}

/// A multiplication by a constant with two set bits, or by the difference
/// of two powers of two, when the shifts for it are short. The multiply
/// routine takes some sixty instructions for a small constant; x * 3 as
/// x + (x << 1) takes eight, in about the room of the call.
static bool matchMulByTwoBits(const MachineInstr &MI,
                              const MachineRegisterInfo &MRI, unsigned &Low,
                              unsigned &High, bool &Subtract) {
  LLT Ty = MRI.getType(MI.getOperand(0).getReg());
  if (!Ty.isScalar() || Ty.getSizeInBits() > 32)
    return false;
  // Leave the multiply of a remainder for matchSubMulUDiv.
  if (mulOfUDivBySame(MI, MRI))
    return false;
  auto C = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
  if (!C)
    return false;
  unsigned Bits = Ty.getSizeInBits();
  APInt V = C->Value.zextOrTrunc(Bits);
  if (V.popcount() == 2) {
    Low = V.countr_zero();
    High = V.logBase2();
    Subtract = false;
  } else if (V.popcount() > 2 && (V + V.getLoBits(V.countr_zero() + 1))
                                         .isPowerOf2()) {
    // 2^High - 2^Low: a run of ones from Low up to High - 1.
    Low = V.countr_zero();
    High = (V + V.getLoBits(Low + 1)).logBase2();
    Subtract = true;
  } else {
    return false;
  }
  unsigned Budget = Bits > 16 ? 2 : 3;
  return shiftCost(Low, Bits) + shiftCost(High - Low, Bits) <= Budget;
}

static void applyMulByTwoBits(MachineInstr &MI, MachineRegisterInfo &MRI,
                              MachineIRBuilder &B, unsigned Low, unsigned High,
                              bool Subtract) {
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  B.setInstrAndDebugLoc(MI);
  Register X = MI.getOperand(1).getReg();
  if (Low)
    X = B.buildShl(Ty, X, B.buildConstant(Ty, Low)).getReg(0);
  auto Top = B.buildShl(Ty, X, B.buildConstant(Ty, High - Low));
  if (Subtract)
    B.buildSub(Dst, Top, X);
  else
    B.buildAdd(Dst, Top, X);
  MI.eraseFromParent();
}

namespace {

#define GET_GICOMBINER_TYPES
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

class PIC16F1PreLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const PIC16F1PreLegalizerCombinerImplRuleConfig &RuleConfig;
  const PIC16F1Subtarget &STI;

public:
  PIC16F1PreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, GISelValueTracking &VT,
      GISelCSEInfo *CSEInfo,
      const PIC16F1PreLegalizerCombinerImplRuleConfig &RuleConfig,
      const PIC16F1Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "PIC16F1PreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

PIC16F1PreLegalizerCombinerImpl::PIC16F1PreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, GISelValueTracking &VT,
    GISelCSEInfo *CSEInfo,
    const PIC16F1PreLegalizerCombinerImplRuleConfig &RuleConfig,
    const PIC16F1Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize=*/true, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

static cl::opt<unsigned> PreBudget(
    "pic16f1-prelegalizer-combine-budget", cl::Hidden, cl::init(1000),
    cl::desc("Rule applications allowed per function, on top of 100 per "
             "instruction"));

/// The combiner works until no rule applies, and nothing in it notices two
/// rules that undo each other: it would then run for ever and allocate
/// instructions until the machine is out of memory. This stops applying
/// rules after far more of them than any real function needs. The code is
/// correct at every step, so stopping early only leaves it less optimized.
class BoundedPreCombiner : public PIC16F1PreLegalizerCombinerImpl {
  mutable uint64_t Budget;

public:
  template <typename... Ts>
  BoundedPreCombiner(MachineFunction &MF, Ts &&...Args)
      : PIC16F1PreLegalizerCombinerImpl(MF, std::forward<Ts>(Args)...),
        Budget(PreBudget + 100 * uint64_t(MF.getInstructionCount())) {}

  bool tryCombineAll(MachineInstr &I) const override {
    if (Budget == 0)
      return false;
    if (!PIC16F1PreLegalizerCombinerImpl::tryCombineAll(I))
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

class PIC16F1PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  PIC16F1PreLegalizerCombiner();

  StringRef getPassName() const override {
    return "PIC16F1PreLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  PIC16F1PreLegalizerCombinerImplRuleConfig RuleConfig;
};

} // end anonymous namespace

void PIC16F1PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
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

PIC16F1PreLegalizerCombiner::PIC16F1PreLegalizerCombiner()
    : MachineFunctionPass(ID) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool PIC16F1PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);
  const PIC16F1Subtarget &ST = MF.getSubtarget<PIC16F1Subtarget>();
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
  BoundedPreCombiner Impl(MF, CInfo, *VT, CSEInfo, RuleConfig,
                                          ST, MDT, ST.getLegalizerInfo());
  return Impl.combineMachineInstrs();
}

char PIC16F1PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(PIC16F1PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine PIC16F1 machine instructions before legalization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(PIC16F1PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine PIC16F1 machine instructions before legalization",
                    false, false)

FunctionPass *llvm::createPIC16F1PreLegalizerCombiner() {
  return new PIC16F1PreLegalizerCombiner();
}
