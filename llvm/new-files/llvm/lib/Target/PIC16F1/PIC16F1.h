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
  // A single bit of a byte is clear or set. Only the 8-bit immediate form
  // of the branch takes these; the immediate is the bit number.
  COND_BC = 6,
  COND_BS = 7,
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
  PCL_ADDR = 0x02,
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
class InstructionSelector;
class PIC16F1RegisterBankInfo;
class PIC16F1Subtarget;
class PIC16F1TargetMachine;
class PassRegistry;

InstructionSelector *
createPIC16F1InstructionSelector(const PIC16F1TargetMachine &,
                                 const PIC16F1Subtarget &,
                                 const PIC16F1RegisterBankInfo &);

FunctionPass *createPIC16F1ISelDag(PIC16F1TargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
/// Turn the code-generator pseudo instructions into machine instructions.
FunctionPass *createPIC16F1ExpandPseudoPass();
/// Turn the compare-and-branch pseudos into machine instructions. Runs after
/// branch relaxation.
FunctionPass *createPIC16F1ExpandBranchesPass();
FunctionPass *createPIC16F1PeepholePass();
/// Mark the functions that cannot be re-entered and give their small
/// variables fixed addresses.
ModulePass *createPIC16F1StaticFramesPass();
/// Step a pointer through an array in place of indexing it in a loop.
FunctionPass *createPIC16F1PointerIVsPass();
void initializePIC16F1PointerIVsPass(PassRegistry &);
void initializePIC16F1StaticFramesPass(PassRegistry &);
/// The largest frame object that gets a fixed address in a function with a
/// static frame (-pic16f1-static-slot-limit); 0 if none does.
unsigned pic16f1StaticSlotLimit();
class MachineFunction;
class GlobalVariable;
class Module;
/// __sp, the cell that holds the stack pointer between functions (the
/// runtime library defines it).
const GlobalVariable *pic16f1StackPointerCell(const Module &M);
/// The function has a stack frame and needs the stack pointer in FSR1.
bool pic16f1UsesStackPointer(const MachineFunction &MF);
/// -pic16f1-main-saves-registers: something outside the module calls main.
bool pic16f1MainIsCalled();
/// Copy pointers to FSR0 in front of the loads and stores that use them.
FunctionPass *createPIC16F1PointerSetupPass();

FunctionPass *createPIC16F1PreLegalizerCombiner();
FunctionPass *createPIC16F1PostLegalizerCombiner();
void initializePIC16F1PreLegalizerCombinerPass(PassRegistry &);
void initializePIC16F1PostLegalizerCombinerPass(PassRegistry &);

void initializePIC16F1AsmPrinterPass(PassRegistry &);
void initializePIC16F1DAGToDAGISelLegacyPass(PassRegistry &);
void initializePIC16F1ExpandPseudoPass(PassRegistry &);
void initializePIC16F1ExpandBranchesPass(PassRegistry &);
void initializePIC16F1PeepholePass(PassRegistry &);
void initializePIC16F1PointerSetupPass(PassRegistry &);

} // namespace llvm

#endif
