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
  // Callee-saved registers are pushed on entry and popped before the
  // return, in a fixed order. The instructions are then the same in every
  // function that saves the same registers, and the outliner shares them.
  /// The static version of a function compiled both ways
  /// (PIC16F1StaticFrames.cpp) has no callers of its own and saves what its
  /// stack version's callers expect; see getCalleeSavedRegs.
  bool isProfitableForNoCSROpt(const Function &F) const override;
  /// Where debug information should look for a frame object. One with a
  /// fixed address has no place in the frame.
  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;
  void processFunctionBeforeFrameFinalized(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;
  bool assignCalleeSavedSpillSlots(
      MachineFunction &MF, const TargetRegisterInfo *TRI,
      std::vector<CalleeSavedInfo> &CSI) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;
  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

private:
  const PIC16F1Subtarget &STI;
  /// Add \p Amount to the stack pointer.
  void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                const DebugLoc &DL, int64_t Amount) const;
};

} // namespace llvm

#endif
