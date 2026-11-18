//===-- PIC16F1.h - Top-level interface ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1_H

#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

namespace PIC16F1CC {
// Conditions of the compare-and-branch pseudos. Each pair differs in bit 0,
// so inverting a condition is "CC ^ 1".
enum CondCodes {
  COND_EQ = 0,
  COND_NE = 1,
  COND_ULT = 2,
  COND_UGE = 3,
  COND_LT = 4,
  COND_GE = 5,
};
} // namespace PIC16F1CC

namespace PIC16F1II {
// Target flags on symbolic machine operands: which part of the address the
// instruction takes.
enum TOF {
  MO_NONE = 0,
  MO_PM,     // code address (used on LDI16 before expansion)
  MO_LO8,    // low byte of a data address
  MO_HI8,    // high byte of a data address
  MO_PM_LO8, // low byte of a code word address
  MO_PM_HI8, // high byte of a code word address
  MO_BANK,   // bank of a data address
  MO_PAGE,   // PCLATH value for a code address
};
} // namespace PIC16F1II

namespace PIC16F1 {
// File addresses of the core registers, which the instruction set fixes at
// the bottom of every bank.
enum CoreReg : unsigned {
  STATUS_ADDR = 0x03,
  FSR0L_ADDR = 0x04,
  FSR0H_ADDR = 0x05,
  FSR1L_ADDR = 0x06,
  FSR1H_ADDR = 0x07,
  PCLATH_ADDR = 0x0A,
  TMP0_ADDR = 0x7E,
};
enum StatusBit : unsigned { STATUS_C = 0, STATUS_Z = 2 };
} // namespace PIC16F1

class FunctionPass;
class PIC16F1TargetMachine;
class PassRegistry;

FunctionPass *createPIC16F1ISelDag(PIC16F1TargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
/// Turn the code-generator pseudo instructions into machine instructions.
FunctionPass *createPIC16F1ExpandPseudoPass();
/// Turn the compare-and-branch pseudos into machine instructions. Runs after
/// branch relaxation.
FunctionPass *createPIC16F1ExpandBranchesPass();

void initializePIC16F1AsmPrinterPass(PassRegistry &);
void initializePIC16F1DAGToDAGISelLegacyPass(PassRegistry &);
void initializePIC16F1ExpandPseudoPass(PassRegistry &);
void initializePIC16F1ExpandBranchesPass(PassRegistry &);

} // namespace llvm

#endif
