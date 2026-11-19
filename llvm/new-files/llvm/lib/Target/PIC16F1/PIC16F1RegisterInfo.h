//===-- PIC16F1RegisterInfo.h - Register information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1REGISTERINFO_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "PIC16F1GenRegisterInfo.inc"

namespace llvm {

class PIC16F1RegisterInfo : public PIC16F1GenRegisterInfo {
public:
  PIC16F1RegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  /// The registers a runtime library routine preserves, if that is more
  /// than the calling convention promises; else null.
  static const uint32_t *getLibcallPreservedMask(StringRef Name);
  /// What survives a call to "G$isr" for a G that only its own file calls
  /// (PIC16F1StaticFrames.cpp): nothing. Such a G may save nothing, its
  /// callers knowing what it changes; this caller does not, since which
  /// copy of G the name turns out to be is the linker's choice.
  static const uint32_t *getInterruptCopyMask();

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  const TargetRegisterClass *
  getPointerRegClass(unsigned Kind = 0) const override;
  /// A value held to FSR1 by a pointer access may be anywhere when it is
  /// split off from that access.
  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif
