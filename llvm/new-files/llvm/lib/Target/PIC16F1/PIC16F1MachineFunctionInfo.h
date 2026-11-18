//===-- PIC16F1MachineFunctionInfo.h - Per-function information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class PIC16F1MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// Frame index of the first variadic argument.
  int VarArgsFrameIndex = 0;

public:
  PIC16F1MachineFunctionInfo() = default;
  PIC16F1MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {
  }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }
};

} // namespace llvm

#endif
