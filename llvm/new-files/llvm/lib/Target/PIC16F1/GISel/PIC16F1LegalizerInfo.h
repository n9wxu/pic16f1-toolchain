//===-- PIC16F1LegalizerInfo.h - Legalizer rules ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_GISEL_PIC16F1LEGALIZERINFO_H
#define LLVM_LIB_TARGET_PIC16F1_GISEL_PIC16F1LEGALIZERINFO_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {
class PIC16F1Subtarget;

class PIC16F1LegalizerInfo : public LegalizerInfo {
public:
  explicit PIC16F1LegalizerInfo(const PIC16F1Subtarget &ST);

  bool legalizeCustom(LegalizerHelper &Helper, MachineInstr &MI,
                      LostDebugLocObserver &LocObserver) const override;

private:
  bool legalizeNarrowMulDiv(LegalizerHelper &Helper, MachineInstr &MI,
                            LostDebugLocObserver &LocObserver) const;
  bool legalizeWideShift(LegalizerHelper &Helper, MachineInstr &MI,
                         LostDebugLocObserver &LocObserver) const;
  bool legalizeWideExt(LegalizerHelper &Helper, MachineInstr &MI) const;
  bool legalizeWideICmp(LegalizerHelper &Helper, MachineInstr &MI) const;
  bool legalizeWideTrunc(LegalizerHelper &Helper, MachineInstr &MI) const;
  bool legalizeVAStart(LegalizerHelper &Helper, MachineInstr &MI) const;
};

} // namespace llvm

#endif
