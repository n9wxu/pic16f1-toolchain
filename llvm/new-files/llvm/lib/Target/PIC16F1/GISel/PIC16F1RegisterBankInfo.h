//===-- PIC16F1RegisterBankInfo.h - Register banks ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_GISEL_PIC16F1REGISTERBANKINFO_H
#define LLVM_LIB_TARGET_PIC16F1_GISEL_PIC16F1REGISTERBANKINFO_H

#include "llvm/CodeGen/RegisterBankInfo.h"

#define GET_REGBANK_DECLARATIONS
#include "PIC16F1GenRegisterBank.inc"
#undef GET_REGBANK_DECLARATIONS

namespace llvm {
class TargetRegisterInfo;

class PIC16F1GenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "PIC16F1GenRegisterBank.inc"
#undef GET_TARGET_REGBANK_CLASS
};

class PIC16F1RegisterBankInfo final : public PIC16F1GenRegisterBankInfo {
public:
  explicit PIC16F1RegisterBankInfo(const TargetRegisterInfo &TRI);

  const InstructionMapping &
  getInstrMapping(const MachineInstr &MI) const override;
};

} // namespace llvm

#endif
