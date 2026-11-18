//===-- PIC16F1MachineFunctionInfo.cpp - PIC16F1 machine function info ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1MachineFunctionInfo.h"

using namespace llvm;

void PIC16F1MachineFunctionInfo::anchor() { }

MachineFunctionInfo *PIC16F1MachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<PIC16F1MachineFunctionInfo>(*this);
}
