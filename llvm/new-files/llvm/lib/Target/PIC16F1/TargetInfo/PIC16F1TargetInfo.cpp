//===-- PIC16F1TargetInfo.cpp - PIC16F1 Target Implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getThePIC16F1Target() {
  static Target ThePIC16F1Target;
  return ThePIC16F1Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePIC16F1TargetInfo() {
  RegisterTarget<Triple::pic16f1> X(getThePIC16F1Target(), "pic16f1",
                                   "Microchip PIC16F1 enhanced mid-range", "PIC16F1");
}
