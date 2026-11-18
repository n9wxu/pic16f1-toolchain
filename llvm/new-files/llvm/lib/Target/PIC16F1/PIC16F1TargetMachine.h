//===-- PIC16F1TargetMachine.h - Target machine ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1TARGETMACHINE_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1TARGETMACHINE_H

#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <optional>

namespace llvm {

class PIC16F1TargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  PIC16F1Subtarget Subtarget;

public:
  PIC16F1TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT);
  ~PIC16F1TargetMachine() override;

  const PIC16F1Subtarget *getSubtargetImpl(const Function &F) const override {
    return &Subtarget;
  }
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  /// Use what is known about the registers a called function changes, when
  /// it is compiled before its caller: the caller then need not keep
  /// values out of registers the callee leaves alone, and a function only
  /// this file calls need not save any. Off with -mllvm -enable-ipra=0.
  bool useIPRA() const override {
    return getOptLevel() != CodeGenOptLevel::None;
  }
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
};

} // namespace llvm

#endif
