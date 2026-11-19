//===-- PIC16F1Subtarget.h - Subtarget information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1SUBTARGET_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1SUBTARGET_H

#include "PIC16F1FrameLowering.h"
#include "PIC16F1ISelLowering.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1RegisterInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "PIC16F1GenSubtargetInfo.inc"

namespace llvm {
class StringRef;

class PIC16F1Subtarget : public PIC16F1GenSubtargetInfo {
  virtual void anchor();
  bool HasBanks64 = false;
  mutable DenseMap<const Function *, BitVector> DualClobbers;
  PIC16F1InstrInfo InstrInfo;
  PIC16F1TargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;
  PIC16F1FrameLowering FrameLowering;

  // GlobalISel.
  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<InlineAsmLowering> InlineAsmLoweringInfo;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;
  std::unique_ptr<InstructionSelector> InstSelector;

public:
  PIC16F1Subtarget(const Triple &TT, const std::string &CPU,
                   const std::string &FS, const TargetMachine &TM);
  ~PIC16F1Subtarget() override;

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  /// For a function compiled both ways (PIC16F1StaticFrames.cpp): the
  /// registers the version compiled first changes. The other version is
  /// made to agree with it.
  DenseMap<const Function *, BitVector> &dualClobbers() const {
    return DualClobbers;
  }

  bool hasBanks64() const { return HasBanks64; }
  /// End of the banked (directly addressable) data space.
  unsigned bankedEnd() const { return HasBanks64 ? 0x2000 : 0x1000; }

  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const PIC16F1InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const PIC16F1RegisterInfo *getRegisterInfo() const override {
    return &getInstrInfo()->getRegisterInfo();
  }
  const PIC16F1TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;

  const CallLowering *getCallLowering() const override {
    return CallLoweringInfo.get();
  }
  const InlineAsmLowering *getInlineAsmLowering() const override {
    return InlineAsmLoweringInfo.get();
  }
  const LegalizerInfo *getLegalizerInfo() const override {
    return Legalizer.get();
  }
  const RegisterBankInfo *getRegBankInfo() const override {
    return RegBankInfo.get();
  }
  InstructionSelector *getInstructionSelector() const override {
    return InstSelector.get();
  }
};
} // namespace llvm

#endif
