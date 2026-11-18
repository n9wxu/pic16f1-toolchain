//===-- PIC16F1Disassembler.cpp - Disassembler for PIC16F1 ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "pic16f1-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class PIC16F1Disassembler : public MCDisassembler {
public:
  PIC16F1Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // namespace

static MCDisassembler *createPIC16F1Disassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new PIC16F1Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePIC16F1Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getThePIC16F1Target(),
                                         createPIC16F1Disassembler);
}

/// Signed 6-bit offset of ADDFSR and of MOVIW/MOVWI k[FSRn].
static DecodeStatus decodeOff6(MCInst &Inst, unsigned Field, uint64_t Address,
                               const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(SignExtend32<6>(Field)));
  return MCDisassembler::Success;
}

/// Signed 9-bit word offset of BRA.
static DecodeStatus decodeBra(MCInst &Inst, unsigned Field, uint64_t Address,
                              const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(SignExtend32<9>(Field)));
  return MCDisassembler::Success;
}

#include "PIC16F1GenDisassemblerTables.inc"

DecodeStatus PIC16F1Disassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CStream) const {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }
  Size = 2;
  // Bits 14 and 15 do not exist in program memory. Data stored in flash as
  // RETLW words and erased words (0x3FFF) decode like any other word.
  uint32_t Insn = support::endian::read16le(Bytes.data());
  if (Insn & 0xC000)
    return MCDisassembler::Fail;
  // CLRW ignores its two low bits.
  if ((Insn & 0x3FFC) == 0x0100)
    Insn |= 3;
  if (STI.hasFeature(PIC16F1::FeatureBanks64)) {
    DecodeStatus S =
        decodeInstruction(DecoderTableBanks6416, Instr, Insn, Address, this, STI);
    if (S != MCDisassembler::Fail)
      return S;
  }
  return decodeInstruction(DecoderTable16, Instr, Insn, Address, this, STI);
}
