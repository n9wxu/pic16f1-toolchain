//===-- PIC16F1TargetMachine.cpp - Target machine ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1TargetMachine.h"
#include "PIC16F1.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/Localizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializePIC16F1Target() {
  RegisterTargetMachine<PIC16F1TargetMachine> X(getThePIC16F1Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeGlobalISel(PR);
  initializePIC16F1PreLegalizerCombinerPass(PR);
  initializePIC16F1PostLegalizerCombinerPass(PR);
  initializePIC16F1AsmPrinterPass(PR);
  initializePIC16F1DAGToDAGISelLegacyPass(PR);
  initializePIC16F1ExpandPseudoPass(PR);
  initializePIC16F1ExpandBranchesPass(PR);
  initializePIC16F1PeepholePass(PR);
  initializePIC16F1PointerSetupPass(PR);
  initializePIC16F1StaticFramesPass(PR);
}

namespace {
/// Interrupt handlers go in ".isr", which the linker script places at the
/// interrupt vector (word address 4).
class PIC16F1ObjectFile : public TargetLoweringObjectFileELF {
public:
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override {
    if (const auto *F = dyn_cast<Function>(GO))
      if (F->hasFnAttribute("interrupt"))
        return getContext().getELFSection(".isr", ELF::SHT_PROGBITS,
                                          ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
    return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
  }

  /// The variables that stand in for a stack frame (PIC16F1StaticFrames.cpp)
  /// take up no room in the file.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override {
    if (GO->getSection().starts_with(".noinit"))
      return getContext().getELFSection(GO->getSection(), ELF::SHT_NOBITS,
                                        ELF::SHF_ALLOC | ELF::SHF_WRITE);
    return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, Kind, TM);
  }
};
} // namespace

PIC16F1TargetMachine::PIC16F1TargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Medium), OL),
      TLOF(std::make_unique<PIC16F1ObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  // Every variable gets a section of its own, whatever -fdata-sections
  // says. The linker keeps a section that fits in a bank from crossing
  // into the next one, and code relies on that: it selects the bank once
  // for all the bytes of a variable.
  this->Options.DataSections = true;
  initAsmInfo();
  // Code models: "small" promises that all code lies in the first page (2K
  // words), so PCLATH never needs setting before a call or a long jump.
  // The linker checks the promise. The default, "medium", sets PCLATH
  // every time.
  // Instruction selection is GlobalISel. A function it cannot handle yet
  // falls back to the SelectionDAG selector; -global-isel-abort=2 reports
  // each such function, and -global-isel=0 selects SelectionDAG throughout.
  // Repeated instruction sequences become subroutines in functions
  // optimized for size (PIC16F1InstrInfo.cpp).
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);
  setGlobalISel(true);
  setGlobalISelAbort(GlobalISelAbortMode::Disable);
}

PIC16F1TargetMachine::~PIC16F1TargetMachine() = default;

namespace {
class PIC16F1PassConfig : public TargetPassConfig {
public:
  PIC16F1PassConfig(PIC16F1TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  PIC16F1TargetMachine &getPIC16F1TargetMachine() const {
    return getTM<PIC16F1TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  void addPreGlobalInstructionSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreRegAlloc() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *PIC16F1TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new PIC16F1PassConfig(*this, PM);
}

MachineFunctionInfo *PIC16F1TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return PIC16F1MachineFunctionInfo::create<PIC16F1MachineFunctionInfo>(
      Allocator, F, STI);
}

void PIC16F1PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createPIC16F1StaticFramesPass());
}

bool PIC16F1PassConfig::addInstSelector() {
  addPass(createPIC16F1ISelDag(getPIC16F1TargetMachine(), getOptLevel()));
  return false;
}

bool PIC16F1PassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void PIC16F1PassConfig::addPreLegalizeMachineIR() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createPIC16F1PreLegalizerCombiner());
}

void PIC16F1PassConfig::addPreRegBankSelect() {
  // Also without optimization: besides combining, this pass puts address
  // computations next to their users and keeps carry chains together.
  addPass(createPIC16F1PostLegalizerCombiner());
}

bool PIC16F1PassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

bool PIC16F1PassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

void PIC16F1PassConfig::addPreGlobalInstructionSelect() {
  // The translator puts every constant at the top of the function, and
  // without optimization each then sits in a stack slot from there on.
  // Move them next to their users. With optimization the register
  // allocator forms constants again where that pays, and does it better.
  if (getOptLevel() == CodeGenOptLevel::None)
    addPass(new Localizer());
}

bool PIC16F1PassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}

void PIC16F1PassConfig::addPreRegAlloc() {
  addPass(createPIC16F1PointerSetupPass());
}

void PIC16F1PassConfig::addPreEmitPass() {
  // The expanded instructions name file addresses, not registers. LLVM's
  // collector of register usage (for -O1 and up) and its outliner both run
  // after this hook; the expansion pass records the registers a function
  // changes for the first, and the second then works on final instructions.
  addPass(createPIC16F1ExpandPseudoPass());
  // Only branch pseudos are left; every other instruction now has its final
  // size, so branch distances can be checked.
  addPass(&BranchRelaxationPassID);
  addPass(createPIC16F1ExpandBranchesPass());
  // Last, on the final instructions; it only removes instructions and
  // changes operands, so branch distances can only shrink. The same holds
  // for outlining.
  addPass(createPIC16F1PeepholePass());
}
