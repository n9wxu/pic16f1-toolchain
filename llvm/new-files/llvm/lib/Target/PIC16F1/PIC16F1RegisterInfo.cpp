//===-- PIC16F1RegisterInfo.cpp - Register information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1RegisterInfo.h"
#include "PIC16F1.h"
#include "PIC16F1FrameLowering.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-reg-info"

static cl::opt<bool> MainSavesRegisters(
    "pic16f1-main-saves-registers", cl::Hidden, cl::init(false),
    cl::desc("Make main save the callee-saved registers like any other "
             "function, for programs that call main from another file"));

bool llvm::pic16f1MainIsCalled() { return MainSavesRegisters; }

#define GET_REGINFO_TARGET_DESC
#include "PIC16F1GenRegisterInfo.inc"

PIC16F1RegisterInfo::PIC16F1RegisterInfo()
    : PIC16F1GenRegisterInfo(PIC16F1::NoRegister) {}

const MCPhysReg *
PIC16F1RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (MF->getFunction().hasFnAttribute("interrupt"))
    return CSR_Interrupt_SaveList;
  // The startup code calls main and keeps nothing in registers across the
  // call. A program that calls main itself (C allows it) from the file that
  // defines it shows here as a use; one that calls it from another file
  // needs -pic16f1-main-saves-registers (-mmain-saves-registers).
  const Function &F = MF->getFunction();
  if (F.doesNotReturn() || (!MainSavesRegisters && F.getName() == "main" &&
                            F.use_empty() && !F.hasAddressTaken()))
    return CSR_NoRegs_SaveList;
  return CSR_Normal_SaveList;
}

const uint32_t *
PIC16F1RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                          CallingConv::ID) const {
  return CSR_NormalCall_RegMask;
}

const uint32_t *PIC16F1RegisterInfo::getLibcallPreservedMask(StringRef Name) {
  static const char *const SmallMath[] = {
      "__mulqi3",  "__udivqi3", "__umodqi3", "__divqi3",  "__modqi3",
      "__mulhi3",  "__udivhi3", "__umodhi3", "__divhi3",  "__modhi3"};
  for (const char *Routine : SmallMath)
    if (Name == Routine)
      return CSR_SmallMath_RegMask;
  return nullptr;
}

BitVector PIC16F1RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  if (!MF.getInfo<PIC16F1MachineFunctionInfo>()->fsr1IsRegister()) {
    Reserved.set(PIC16F1::SP);
    Reserved.set(PIC16F1::FSR1L);
    Reserved.set(PIC16F1::FSR1H);
  }
  Reserved.set(PIC16F1::W);
  Reserved.set(PIC16F1::TMP0);
  Reserved.set(PIC16F1::TMP1);
  return Reserved;
}

const TargetRegisterClass *PIC16F1RegisterInfo::getLargestLegalSuperClass(
    const TargetRegisterClass *RC, const MachineFunction &MF) const {
  if (RC == &PIC16F1::FSR1CRegClass)
    return &PIC16F1::GPR16RegClass;
  return RC;
}

const TargetRegisterClass *
PIC16F1RegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &PIC16F1::GPR16RegClass;
}

bool PIC16F1RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOperandNum,
                                              RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  // An object with a fixed address: the instruction becomes its directly
  // addressed form.
  if (const auto *Static =
          MF.getInfo<PIC16F1MachineFunctionInfo>()->getStaticObject(FrameIndex)) {
    if (MI.isDebugInstr()) {
      MI.getOperand(FIOperandNum).ChangeToImmediate(0);
      return false;
    }
    unsigned Direct;
    switch (MI.getOpcode()) {
    case PIC16F1::LD8fi:   Direct = PIC16F1::LD8abs; break;
    case PIC16F1::LD16fi:  Direct = PIC16F1::LD16abs; break;
    case PIC16F1::ST8fi:   Direct = PIC16F1::ST8abs; break;
    case PIC16F1::ST16fi:  Direct = PIC16F1::ST16abs; break;
    case PIC16F1::STI8fi:  Direct = PIC16F1::STI8abs; break;
    case PIC16F1::STI16fi: Direct = PIC16F1::STI16abs; break;
    case PIC16F1::LEAfi:   Direct = PIC16F1::LDI16; break;
    default:
      llvm_unreachable("frame object with a fixed address in an instruction "
                       "without a direct form");
    }
    int64_t Offset = Static->second + MI.getOperand(FIOperandNum + 1).getImm();
    MI.setDesc(MF.getSubtarget().getInstrInfo()->get(Direct));
    MI.removeOperand(FIOperandNum + 1);
    MI.getOperand(FIOperandNum).ChangeToGA(Static->first, Offset);
    return false;
  }
  // The stack grows down and FSR1 points at the lowest byte in use, so every
  // object sits at a non-negative offset from it. Nothing else (such as a
  // return address) is on this stack.
  int64_t Offset = MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + SPAdj;
  MI.getOperand(FIOperandNum).ChangeToImmediate(Offset);
  return false;
}

Register
PIC16F1RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return PIC16F1::SP;
}
