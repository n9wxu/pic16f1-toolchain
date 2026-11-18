//===-- PIC16F1MCAsmInfo.h - PIC16F1 asm properties ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1MCASMINFO_H
#define LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1MCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCExpr.h"

namespace llvm {
class Triple;

class PIC16F1MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit PIC16F1MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;
  bool evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr, MCValue &Res,
                                 const MCAssembler *Asm) const override;
};

namespace PIC16F1 {

// Operators that select part of an address: lo8(sym), bank(sym), ...
enum Specifier : uint16_t {
  S_None,
  S_LO8 = MCSymbolRefExpr::FirstTargetSpecifier, // low byte
  S_HI8,    // high byte
  S_PM_LO8, // low byte of the word address of code
  S_PM_HI8, // high byte of the word address of code
  S_PM,     // word address of code
  S_BANK,   // bank number of a data address
  S_OFF,    // offset of a data address within its bank
  S_PAGE,   // PCLATH value for a code address
};

StringRef getSpecifierName(uint16_t S);
/// Returns S_None if \p Name is not a specifier.
uint16_t parseSpecifierName(StringRef Name);
/// Apply a specifier to a known address.
int64_t applySpecifier(uint16_t S, int64_t Value);

} // end namespace PIC16F1
} // end namespace llvm

#endif
