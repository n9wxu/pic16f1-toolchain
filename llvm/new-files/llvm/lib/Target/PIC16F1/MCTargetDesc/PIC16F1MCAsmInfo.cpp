//===-- PIC16F1MCAsmInfo.cpp - PIC16F1 asm properties ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void PIC16F1MCAsmInfo::anchor() {}

PIC16F1MCAsmInfo::PIC16F1MCAsmInfo(const Triple &TT,
                                   const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  // The size of an address in debug information, not of a C pointer (which
  // is 2). Addresses there are the ELF file's: program memory in bytes, two
  // to a word, and RAM at 0x802000 up, which do not fit in 16 bits.
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 1;
  MinInstAlignment = 2;
  MaxInstLength = 2;
  CommentString = ";";
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  // Call frame information goes with the debug information (.debug_frame);
  // there is no exception handling to want it otherwise.
  UsesCFIWithoutEH = true;
}

namespace {
const struct {
  const char *Name;
  uint16_t Spec;
} SpecifierNames[] = {
    {"lo8", PIC16F1::S_LO8},       {"hi8", PIC16F1::S_HI8},
    {"pm_lo8", PIC16F1::S_PM_LO8}, {"pm_hi8", PIC16F1::S_PM_HI8},
    {"pm", PIC16F1::S_PM},         {"bank", PIC16F1::S_BANK},
    {"off", PIC16F1::S_OFF},       {"page", PIC16F1::S_PAGE},
};
} // namespace

StringRef PIC16F1::getSpecifierName(uint16_t S) {
  for (const auto &E : SpecifierNames)
    if (E.Spec == S)
      return E.Name;
  return "";
}

uint16_t PIC16F1::parseSpecifierName(StringRef Name) {
  for (const auto &E : SpecifierNames)
    if (Name == E.Name)
      return E.Spec;
  return S_None;
}

int64_t PIC16F1::applySpecifier(uint16_t S, int64_t V) {
  switch (S) {
  case S_LO8:
    return V & 0xFF;
  case S_HI8:
    return (V >> 8) & 0xFF;
  case S_PM_LO8:
    return (V >> 1) & 0xFF;
  case S_PM_HI8:
    return (V >> 9) & 0xFF;
  case S_PM:
    return (V >> 1) & 0xFFFF;
  case S_BANK:
    return (V >> 7) & 0x1F;
  case S_OFF:
    return V & 0x7F;
  case S_PAGE:
    return (V >> 9) & 0x7F;
  default:
    return V;
  }
}

void PIC16F1MCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                          const MCSpecifierExpr &Expr) const {
  OS << PIC16F1::getSpecifierName(Expr.getSpecifier()) << '(';
  printExpr(OS, *Expr.getSubExpr());
  OS << ')';
}

bool PIC16F1MCAsmInfo::evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr,
                                                 MCValue &Res,
                                                 const MCAssembler *Asm) const {
  MCValue Value;
  if (!Expr.getSubExpr()->evaluateAsRelocatable(Value, Asm))
    return false;
  // The fixup kind says which part of the address is wanted. The assembler
  // extracts it when it resolves the fixup, or the linker does once the
  // address is known, so the value passes through unchanged here.
  Res = MCValue::get(Value.getAddSym(), Value.getSubSym(), Value.getConstant(),
                     Expr.getSpecifier());
  return true;
}
