//===-- PIC16F1AsmBackend.cpp - PIC16F1 assembler backend ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1FixupKinds.h"
#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class PIC16F1AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

  /// Field value for a fixup whose target address is known at assembly time.
  uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value) const;

public:
  PIC16F1AsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  void applyFixup(const MCFragment &, const MCFixup &, const MCValue &Target,
                  uint8_t *Data, uint64_t Value, bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createPIC16F1ELFObjectWriter(OSABI);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // Name, bit offset, bit size, flags.
    const static MCFixupKindInfo Infos[PIC16F1::NumTargetFixupKinds] = {
        {"fixup_lo8", 0, 8, 0},     {"fixup_hi8", 0, 8, 0},
        {"fixup_pm_lo8", 0, 8, 0},  {"fixup_pm_hi8", 0, 8, 0},
        {"fixup_call11", 0, 11, 0}, {"fixup_page7", 0, 7, 0},
        {"fixup_bra9", 0, 9, 0},    {"fixup_bank5", 0, 5, 0},
        {"fixup_f7", 0, 7, 0},      {"fixup_pm16", 0, 16, 0},
    };
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

uint64_t PIC16F1AsmBackend::adjustFixupValue(const MCFixup &Fixup,
                                             uint64_t Value) const {
  MCContext &Ctx = getContext();
  switch (Fixup.getKind()) {
  case PIC16F1::fixup_lo8:
    return Value & 0xFF;
  case PIC16F1::fixup_hi8:
    return (Value >> 8) & 0xFF;
  case PIC16F1::fixup_pm_lo8:
    return (Value >> 1) & 0xFF;
  case PIC16F1::fixup_pm_hi8:
    return (Value >> 9) & 0xFF;
  case PIC16F1::fixup_pm16:
    return (Value >> 1) & 0xFFFF;
  case PIC16F1::fixup_call11:
    return (Value >> 1) & 0x7FF;
  case PIC16F1::fixup_page7:
    return (Value >> 9) & 0x7F;
  case PIC16F1::fixup_bank5:
  case PIC16F1::fixup_f7:
    // Only banked (traditional) addresses can be split without knowing the
    // device's memory layout.
    if (Value >= 0x1000)
      Ctx.reportError(Fixup.getLoc(), "address is not a banked data address");
    return Fixup.getKind() == PIC16F1::fixup_bank5 ? (Value >> 7) & 0x1F
                                                   : Value & 0x7F;
  case PIC16F1::fixup_bra9: {
    // Value is target minus the address of the BRA itself, in bytes.
    int64_t Offset = int64_t(Value) - 2;
    if (Offset & 1)
      Ctx.reportError(Fixup.getLoc(), "branch target is not word aligned");
    Offset >>= 1;
    if (Offset < -256 || Offset > 255)
      Ctx.reportError(Fixup.getLoc(), "branch target out of range for BRA");
    return Offset & 0x1FF;
  }
  default:
    return Value;
  }
}

void PIC16F1AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                   const MCValue &Target, uint8_t *Data,
                                   uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  if (!IsResolved)
    return; // The addend lives in the relocation.
  Value = adjustFixupValue(Fixup, Value);
  if (!Value)
    return;
  MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
  unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

bool PIC16F1AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                     const MCSubtargetInfo *STI) const {
  if (Count % 2 != 0)
    return false;
  for (uint64_t I = 0; I != Count; ++I)
    OS.write('\0'); // NOP is 0x0000
  return true;
}

} // end anonymous namespace

MCAsmBackend *llvm::createPIC16F1MCAsmBackend(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              const MCRegisterInfo &MRI,
                                              const MCTargetOptions &Options) {
  return new PIC16F1AsmBackend(STI, ELF::ELFOSABI_STANDALONE);
}
