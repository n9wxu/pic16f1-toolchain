//===-- PIC16F1.td - Describe the PIC16F1 target ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Microchip enhanced mid-range core (PIC16F1xxx): 14-bit instructions, one
// working register, banked data memory, two 16-bit indirect pointers.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

// The newer families (PIC16F188xx, PIC16F152xx, ...) have 64 data banks: a
// 6-bit BSR and a MOVLB with a 6-bit operand and a different encoding.
def FeatureBanks64
    : SubtargetFeature<"banks64", "HasBanks64", "true",
                       "64 data banks (6-bit BSR)">;

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", []>;
def : Proc<"pic16f1937", []>;
def : Proc<"pic16f1829", []>;
def : Proc<"pic16f1619", []>;
def : Proc<"pic16f18877", [FeatureBanks64]>;
def : Proc<"pic16f15276", [FeatureBanks64]>;

include "PIC16F1RegisterInfo.td"
include "GISel/PIC16F1RegisterBanks.td"
include "PIC16F1CallingConv.td"
include "PIC16F1InstrInfo.td"
include "PIC16F1Combine.td"

defm : RemapAllTargetPseudoPointerOperands<GPR16>;

def PIC16F1InstrInfo : InstrInfo;

def PIC16F1AsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

def PIC16F1AsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 0;
}

def PIC16F1 : Target {
  let InstructionSet = PIC16F1InstrInfo;
  let AssemblyParsers = [PIC16F1AsmParser];
  let AssemblyWriters = [PIC16F1AsmWriter];
}
