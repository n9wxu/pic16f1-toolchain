//===-- PIC16F1FrameLowering.cpp - Frame lowering ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// The data stack is addressed through FSR1 and grows down. FSR1 points at
// the lowest byte in use. Return addresses are on the hardware stack, so a
// frame holds only saved registers, locals, spills and outgoing arguments.
//
// An interrupt handler runs on its own stack: it loads FSR1 with
// __isr_stack_top on entry, and RETFIE puts the interrupted value back from
// the shadow register. Because of that, code may move FSR1 temporarily to
// reach a distant slot without an interrupt overwriting live data.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1FrameLowering.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"

using namespace llvm;

PIC16F1FrameLowering::PIC16F1FrameLowering(const PIC16F1Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(1), 0,
                          Align(1)),
      STI(STI) {}

void PIC16F1FrameLowering::adjustSP(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL, int64_t Amount) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  if (Amount == 0)
    return;
  // ADDFSR takes -32..31. Up to four of them are shorter than or equal to
  // the arithmetic form, and they leave W and the flags alone.
  if (Amount >= -128 && Amount <= 124) {
    while (Amount != 0) {
      int64_t Step = Amount > 0 ? std::min<int64_t>(Amount, 31)
                                : std::max<int64_t>(Amount, -32);
      BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDFSR)).addImm(1).addImm(Step);
      Amount -= Step;
    }
    return;
  }
  BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW)).addImm(Amount & 0xFF);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDWF))
      .addImm(PIC16F1::FSR1L_ADDR)
      .addImm(1);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW)).addImm((Amount >> 8) & 0xFF);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDWFC))
      .addImm(PIC16F1::FSR1H_ADDR)
      .addImm(1);
}

void PIC16F1FrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL;

  if (MF.getFunction().hasFnAttribute("interrupt")) {
    const char *Top = "__isr_stack_top";
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_LO8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1L_ADDR);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_HI8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1H_ADDR);
  }
  adjustSP(MBB, I, DL, -int64_t(MFI.getStackSize()));
}

void PIC16F1FrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  // RETFIE restores FSR1, so an interrupt handler has nothing to undo.
  if (MF.getFunction().hasFnAttribute("interrupt"))
    return;
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  adjustSP(MBB, I, DL, MF.getFrameInfo().getStackSize());
}

MachineBasicBlock::iterator PIC16F1FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The outgoing-argument area is part of the frame.
  return MBB.erase(I);
}
