//===-- PIC16F1MCTargetDesc.cpp - PIC16F1 target descriptions ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1MCTargetDesc.h"
#include "PIC16F1Dwarf.h"
#include "llvm/MC/MCDwarf.h"
#include "PIC16F1InstPrinter.h"
#include "PIC16F1MCAsmInfo.h"
#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "PIC16F1GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "PIC16F1GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "PIC16F1GenRegisterInfo.inc"

static MCInstrInfo *createPIC16F1MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitPIC16F1MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createPIC16F1MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitPIC16F1MCRegisterInfo(X, PIC16F1::PC);
  return X;
}

static MCAsmInfo *createPIC16F1MCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT,
                                         const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new PIC16F1MCAsmInfo(TT, Options);
  // The rules every function starts from when a debugger unwinds it.
  SmallString<96> Rules;
  PIC16F1Dwarf::commonRules(Rules);
  MAI->addInitialFrameState(
      MCCFIInstruction::createEscape(nullptr, Rules.str()));
  return MAI;
}

static MCSubtargetInfo *
createPIC16F1MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createPIC16F1MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createPIC16F1MCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new PIC16F1InstPrinter(MAI, MII, MRI);
  return nullptr;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePIC16F1TargetMC() {
  Target &T = getThePIC16F1Target();
  TargetRegistry::RegisterMCAsmInfo(T, createPIC16F1MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createPIC16F1MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createPIC16F1MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createPIC16F1MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createPIC16F1MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createPIC16F1MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createPIC16F1MCAsmBackend);
}
