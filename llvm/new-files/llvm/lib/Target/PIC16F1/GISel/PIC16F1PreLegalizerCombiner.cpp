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

#define GET_GICOMBINER_DEPS
#include "PIC16F1GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "pic16f1-prelegalizer-combiner"

using namespace llvm;

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
  PIC16F1PreLegalizerCombinerImpl Impl(MF, CInfo, *VT, CSEInfo, RuleConfig,
                                          ST, MDT, nullptr);
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
