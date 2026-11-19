//===-- PIC16F1MachineFunctionInfo.h - Per-function information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1MACHINEFUNCTIONINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class GlobalVariable;

class PIC16F1MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// Frame index of the first variadic argument.
  int VarArgsFrameIndex = 0;

  /// FSR1 is a register in this function, and this is the virtual register
  /// that holds the stack pointer meanwhile (see PIC16F1CallLowering.cpp).
  Register StackPointerValue;

  /// Frame objects that have a fixed address in place of a stack slot (see
  /// PIC16F1StaticFrames.cpp): the variable each is in, and where.
  DenseMap<int, std::pair<const GlobalVariable *, int64_t>> StaticObjects;

public:
  PIC16F1MachineFunctionInfo() = default;
  PIC16F1MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {
  }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  bool fsr1IsRegister() const { return StackPointerValue.isValid(); }
  Register getStackPointerValue() const { return StackPointerValue; }
  void setStackPointerValue(Register R) { StackPointerValue = R; }

  void setStaticObject(int FI, const GlobalVariable *GV, int64_t Offset) {
    StaticObjects[FI] = {GV, Offset};
  }
  /// Where frame object \p FI is, if it has a fixed address.
  const std::pair<const GlobalVariable *, int64_t> *
  getStaticObject(int FI) const {
    auto It = StaticObjects.find(FI);
    return It == StaticObjects.end() ? nullptr : &It->second;
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }
};

} // namespace llvm

#endif
