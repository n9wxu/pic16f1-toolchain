//===- PIC16F1.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Microchip PIC16F1 (enhanced mid-range) support.
//
// Address conventions:
//   * Program memory uses byte addresses, two per 14-bit word, as in the
//     Intel HEX files Microchip tools exchange.
//   * Data addresses are 0x800000 plus the 16-bit address the FSR registers
//     use: banked registers at 0x0000-0x0FFF, linear RAM at 0x2000 up, and
//     flash read as data at 0x8000 up.
//
// Direct addressing needs a bank number and a 7-bit offset. For a symbol in
// linear RAM the linker converts: the linear region holds the 80 general
// purpose bytes of each bank back to back, so linear offset n is bank n / 80,
// file address 0x20 + n % 80.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class PIC16F1 final : public TargetInfo {
public:
  PIC16F1(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  /// The banked form (bank << 7 | offset) of a data address, or -1.
  int64_t bankedAddress(uint64_t val) const;
};
} // namespace

PIC16F1::PIC16F1(Ctx &ctx) : TargetInfo(ctx) {
  // Unprogrammed flash reads 0x3FFF; filling gaps with it keeps the image
  // identical to what a programmer would leave there.
  trapInstr = {0xFF, 0x3F, 0xFF, 0x3F};
  defaultImageBase = 0;
  defaultMaxPageSize = 2;
  defaultCommonPageSize = 2;
}

RelExpr PIC16F1::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  return type == R_PIC16F1_BRA9 ? R_PC : R_ABS;
}

static const uint64_t dataSpace = 0x800000;
static const uint64_t linearBase = 0x2000;
static const uint64_t linearEnd = 0x29B0; // 31 banks of 80 bytes
static const uint64_t bankedEnd = 0x1000;

int64_t PIC16F1::bankedAddress(uint64_t val) const {
  if (val < dataSpace)
    return -1;
  uint64_t a = val - dataSpace;
  if (a < bankedEnd)
    return a;
  if (a >= linearBase && a < linearEnd) {
    uint64_t n = a - linearBase;
    return ((n / 80) << 7) | (0x20 + n % 80);
  }
  return -1;
}

void PIC16F1::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  // Replace the low `bits` bits of the instruction word.
  auto patch = [&](unsigned bits, uint64_t v) {
    uint16_t mask = (1u << bits) - 1;
    write16le(loc, (read16le(loc) & ~mask) | (v & mask));
  };

  switch (rel.type) {
  case R_PIC16F1_8:
    *loc = val;
    break;
  case R_PIC16F1_16:
    write16le(loc, val);
    break;
  case R_PIC16F1_32:
    write32le(loc, val);
    break;
  case R_PIC16F1_PM16:
    checkAlignment(ctx, loc, val, 2, rel);
    write16le(loc, val >> 1);
    break;
  case R_PIC16F1_LO8:
    patch(8, val);
    break;
  case R_PIC16F1_HI8:
    patch(8, val >> 8);
    break;
  case R_PIC16F1_PM_LO8:
    checkAlignment(ctx, loc, val, 2, rel);
    patch(8, val >> 1);
    break;
  case R_PIC16F1_PM_HI8:
    patch(8, val >> 9);
    break;
  case R_PIC16F1_CALL11:
    checkAlignment(ctx, loc, val, 2, rel);
    patch(11, val >> 1);
    break;
  case R_PIC16F1_PAGE7:
    patch(7, val >> 9);
    break;
  case R_PIC16F1_BRA9: {
    // val is the target minus the address of the BRA; the offset counts
    // words from the instruction after it.
    int64_t offset = ((int64_t)val - 2) >> 1;
    checkAlignment(ctx, loc, val, 2, rel);
    checkInt(ctx, loc, offset, 9, rel);
    patch(9, offset);
    break;
  }
  case R_PIC16F1_BANK5:
  case R_PIC16F1_F7: {
    int64_t banked = bankedAddress(val);
    if (banked < 0) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " against '" << rel.sym
               << "': address is not reachable with banked addressing";
      break;
    }
    if (rel.type == R_PIC16F1_BANK5)
      patch(5, banked >> 7);
    else
      patch(7, banked);
    break;
  }
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setPIC16F1TargetInfo(Ctx &ctx) { ctx.target.reset(new PIC16F1(ctx)); }
