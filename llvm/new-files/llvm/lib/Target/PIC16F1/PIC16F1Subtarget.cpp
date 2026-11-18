//===-- PIC16F1Subtarget.cpp - Subtarget information ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1Subtarget.h"
#include "GISel/PIC16F1CallLowering.h"
#include "GISel/PIC16F1LegalizerInfo.h"
#include "GISel/PIC16F1RegisterBankInfo.h"
#include "PIC16F1SelectionDAGInfo.h"
#include "PIC16F1TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "PIC16F1GenSubtargetInfo.inc"

void PIC16F1Subtarget::anchor() {}

static std::string cpuOrGeneric(const std::string &CPU) {
  return CPU.empty() ? "generic" : CPU;
}

PIC16F1Subtarget::PIC16F1Subtarget(const Triple &TT, const std::string &CPU,
                                   const std::string &FS,
                                   const TargetMachine &TM)
    : PIC16F1GenSubtargetInfo(TT, cpuOrGeneric(CPU), cpuOrGeneric(CPU), FS),
      InstrInfo(*this), TLInfo(TM, *this), FrameLowering(*this) {
  ParseSubtargetFeatures(cpuOrGeneric(CPU), cpuOrGeneric(CPU), FS);
  TSInfo = std::make_unique<PIC16F1SelectionDAGInfo>();

  CallLoweringInfo = std::make_unique<PIC16F1CallLowering>(TLInfo);
  InlineAsmLoweringInfo = std::make_unique<InlineAsmLowering>(&TLInfo);
  Legalizer = std::make_unique<PIC16F1LegalizerInfo>(*this);
  auto *RBI = new PIC16F1RegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);
  InstSelector.reset(createPIC16F1InstructionSelector(
      static_cast<const PIC16F1TargetMachine &>(TM), *this, *RBI));
}

PIC16F1Subtarget::~PIC16F1Subtarget() = default;

const SelectionDAGTargetInfo *PIC16F1Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
