//===-- PIC16F1RegisterBankInfo.cpp - Register banks ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1RegisterBankInfo.h"
#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_TARGET_REGBANK_IMPL
#include "PIC16F1GenRegisterBank.inc"

using namespace llvm;

PIC16F1RegisterBankInfo::PIC16F1RegisterBankInfo(const TargetRegisterInfo &TRI)
    : PIC16F1GenRegisterBankInfo() {}

const RegisterBankInfo::InstructionMapping &
PIC16F1RegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  // Instructions that already use register classes map through them.
  if (!isPreISelGenericOpcode(MI.getOpcode()) || MI.getOpcode() == TargetOpcode::G_PHI) {
    const InstructionMapping &Mapping = getInstrMappingImpl(MI);
    if (Mapping.isValid())
      return Mapping;
  }

  // There is one bank, so every register operand of every generic
  // instruction goes in it, whatever its width.
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  unsigned NumOperands = MI.getNumOperands();
  SmallVector<const ValueMapping *, 8> Operands(NumOperands, nullptr);
  for (unsigned I = 0; I != NumOperands; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg() || !MO.getReg())
      continue;
    LLT Ty = MRI.getType(MO.getReg());
    if (!Ty.isValid())
      continue;
    Operands[I] = &getValueMapping(0, Ty.getSizeInBits(), PIC16F1::GPRRegBank);
  }
  return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                               getOperandsMapping(Operands), NumOperands);
}
