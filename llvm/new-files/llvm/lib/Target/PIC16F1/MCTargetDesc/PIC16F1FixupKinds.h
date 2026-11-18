//===-- PIC16F1FixupKinds.h - PIC16F1 fixups ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1FIXUPKINDS_H
#define LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace PIC16F1 {

// Every instruction fixup patches the low bits of one little-endian 14-bit
// instruction word. Program addresses are byte addresses (two per word);
// data addresses are 16-bit FSR addresses.
enum Fixups {
  fixup_lo8 = FirstTargetFixupKind, // literal: low byte of an address
  fixup_hi8,                        // literal: high byte of an address
  fixup_pm_lo8,                     // literal: low byte of a word address
  fixup_pm_hi8,                     // literal: high byte of a word address
  fixup_call11,                     // CALL/GOTO: low 11 bits of a word address
  fixup_page7,                      // MOVLP: bits 14-8 of a word address
  fixup_bra9,                       // BRA: signed word offset from the next word
  fixup_bank5,                      // MOVLB: bank of a data address
  fixup_f7,                         // file operand: offset within the bank
  fixup_pm16,                       // data: 16-bit word address
  fixup_bank6,                      // MOVLB on a 64-bank device

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace PIC16F1
} // end namespace llvm

#endif
