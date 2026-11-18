//===-- PIC16F1ELFObjectWriter.cpp - PIC16F1 ELF writer ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1FixupKinds.h"
#include "MCTargetDesc/PIC16F1MCAsmInfo.h"
#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class PIC16F1ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  PIC16F1ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(false, OSABI, ELF::EM_PIC16F1,
                                /*HasRelocationAddend*/ true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return ELF::R_PIC16F1_8;
    case FK_Data_2:
      // ".2byte pm(function)" stores a word address.
      return Target.getSpecifier() == PIC16F1::S_PM ? ELF::R_PIC16F1_PM16
                                                    : ELF::R_PIC16F1_16;
    case FK_Data_4:
      return ELF::R_PIC16F1_32;
    case PIC16F1::fixup_lo8:
      return ELF::R_PIC16F1_LO8;
    case PIC16F1::fixup_hi8:
      return ELF::R_PIC16F1_HI8;
    case PIC16F1::fixup_pm_lo8:
      return ELF::R_PIC16F1_PM_LO8;
    case PIC16F1::fixup_pm_hi8:
      return ELF::R_PIC16F1_PM_HI8;
    case PIC16F1::fixup_call11:
      return ELF::R_PIC16F1_CALL11;
    case PIC16F1::fixup_page7:
      return ELF::R_PIC16F1_PAGE7;
    case PIC16F1::fixup_bra9:
      return ELF::R_PIC16F1_BRA9;
    case PIC16F1::fixup_bank5:
      return ELF::R_PIC16F1_BANK5;
    case PIC16F1::fixup_f7:
      return ELF::R_PIC16F1_F7;
    case PIC16F1::fixup_pm16:
      return ELF::R_PIC16F1_PM16;
    default:
      reportError(Fixup.getLoc(), "unsupported relocation type");
      return ELF::R_PIC16F1_NONE;
    }
  }
};
} // end of anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createPIC16F1ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<PIC16F1ELFObjectWriter>(OSABI);
}
