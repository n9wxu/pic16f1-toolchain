//===-- PIC16F1FrameLowering.h - Frame lowering ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1FRAMELOWERING_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1FRAMELOWERING_H

#include "PIC16F1.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class PIC16F1Subtarget;

class PIC16F1FrameLowering : public TargetFrameLowering {
protected:
  bool hasFPImpl(const MachineFunction &MF) const override { return false; }

public:
  explicit PIC16F1FrameLowering(const PIC16F1Subtarget &STI);

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override {
    return true;
  }

private:
  const PIC16F1Subtarget &STI;
  /// Add \p Amount to the stack pointer.
  void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                const DebugLoc &DL, int64_t Amount) const;
};

} // namespace llvm

#endif
