//===-- PIC16F1InstPrinter.cpp - Print PIC16F1 instructions ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1InstPrinter.h"
#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "PIC16F1GenAsmWriter.inc"

void PIC16F1InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void PIC16F1InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

/// Names the assembler reads as operand keywords. A symbol with one of these
/// names is printed in parentheses so that it reads back as a symbol.
static bool isOperandKeyword(StringRef Name) {
  return Name.equals_insensitive("w") || Name.equals_insensitive("f") ||
         Name.equals_insensitive("fsr0") || Name.equals_insensitive("fsr1");
}

void PIC16F1InstPrinter::printExprOperand(const MCExpr &E, raw_ostream &O) {
  const auto *SRE = dyn_cast<MCSymbolRefExpr>(&E);
  bool Quote = SRE && isOperandKeyword(SRE->getSymbol().getName());
  if (Quote)
    O << '(';
  MAI.printExpr(O, E);
  if (Quote)
    O << ')';
}

void PIC16F1InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg())
    O << getRegisterName(Op.getReg());
  else if (Op.isImm())
    O << Op.getImm();
  else
    printExprOperand(*Op.getExpr(), O);
}

void PIC16F1InstPrinter::printFile(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    O << format_hex(Op.getImm(), 4);
  else
    printExprOperand(*Op.getExpr(), O);
}

void PIC16F1InstPrinter::printDest(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  O << (MI->getOperand(OpNo).getImm() ? 'F' : 'W');
}

void PIC16F1InstPrinter::printFSR(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  O << "FSR" << (MI->getOperand(OpNo).getImm() & 1);
}

void PIC16F1InstPrinter::printIndMode(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  unsigned M = MI->getOperand(OpNo).getImm();
  unsigned N = (M >> 2) & 1;
  switch (M & 3) {
  case 0:
    O << "++FSR" << N;
    break;
  case 1:
    O << "--FSR" << N;
    break;
  case 2:
    O << "FSR" << N << "++";
    break;
  case 3:
    O << "FSR" << N << "--";
    break;
  }
}
