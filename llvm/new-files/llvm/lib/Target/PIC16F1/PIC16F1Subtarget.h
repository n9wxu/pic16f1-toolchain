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
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "PIC16F1GenSubtargetInfo.inc"

namespace llvm {
class StringRef;

class PIC16F1Subtarget : public PIC16F1GenSubtargetInfo {
  virtual void anchor();
  PIC16F1InstrInfo InstrInfo;
  PIC16F1TargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;
  PIC16F1FrameLowering FrameLowering;

public:
  PIC16F1Subtarget(const Triple &TT, const std::string &CPU,
                   const std::string &FS, const TargetMachine &TM);
  ~PIC16F1Subtarget() override;

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

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
};
} // namespace llvm

#endif
