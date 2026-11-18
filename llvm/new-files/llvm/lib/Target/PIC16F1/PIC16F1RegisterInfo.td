//===-- PIC16F1RegisterInfo.td - Register definitions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The core has a single working register. Code generation instead works on
// fourteen byte "registers" R0-R13 that live in common RAM (0x70-0x7D), which
// is reachable from every bank. Adjacent pairs form 16-bit registers. Each
// register's encoding is its file address.
//
// TMP0/TMP1 (0x7E, 0x7F) are scratch bytes used inside instruction
// expansions. W, FSR0 and BSR are also free inside expansions and are never
// live from one code-generator instruction to the next.
//
//===----------------------------------------------------------------------===//

class PICReg<bits<16> Enc, string n> : Register<n> {
  let Namespace = "PIC16F1";
  let HWEncoding = Enc;
}

def sub_lo : SubRegIndex<8> { let Namespace = "PIC16F1"; }
def sub_hi : SubRegIndex<8, 8> { let Namespace = "PIC16F1"; }

foreach I = 0...13 in
  def R#I : PICReg<!add(0x70, I), "R"#I>, DwarfRegNum<[I]>;

def TMP0 : PICReg<0x7E, "TMP0">, DwarfRegNum<[14]>;
def TMP1 : PICReg<0x7F, "TMP1">, DwarfRegNum<[15]>;

class PICPair<bits<16> Enc, string n, list<Register> subregs>
    : RegisterWithSubRegs<n, subregs> {
  let Namespace = "PIC16F1";
  let HWEncoding = Enc;
  let SubRegIndices = [sub_lo, sub_hi];
  let CoveredBySubRegs = 1;
}

def RP0 : PICPair<0x70, "RP0", [R0, R1]>, DwarfRegNum<[16]>;
def RP1 : PICPair<0x72, "RP1", [R2, R3]>, DwarfRegNum<[17]>;
def RP2 : PICPair<0x74, "RP2", [R4, R5]>, DwarfRegNum<[18]>;
def RP3 : PICPair<0x76, "RP3", [R6, R7]>, DwarfRegNum<[19]>;
def RP4 : PICPair<0x78, "RP4", [R8, R9]>, DwarfRegNum<[20]>;
def RP5 : PICPair<0x7A, "RP5", [R10, R11]>, DwarfRegNum<[21]>;
def RP6 : PICPair<0x7C, "RP6", [R12, R13]>, DwarfRegNum<[22]>;

// The software stack pointer is FSR1. FSR0 is the scratch pointer.
def SP    : PICReg<0x06, "FSR1">, DwarfRegNum<[23]>;
def FSR0  : PICReg<0x04, "FSR0">, DwarfRegNum<[24]>;
def W     : PICReg<0x09, "W">, DwarfRegNum<[25]>;
// Only the carry flag is modelled. Z and DC are never live between
// code-generator instructions, so moves and loads may change them freely.
def CARRY : PICReg<0x03, "C">, DwarfRegNum<[26]>;

def GPR8 : RegisterClass<"PIC16F1", [i8], 8, (sequence "R%u", 0, 13)>;
def GPR16 : RegisterClass<"PIC16F1", [i16], 8, (sequence "RP%u", 0, 6)>;

def SCRATCH8 : RegisterClass<"PIC16F1", [i8], 8, (add TMP0, TMP1, W)> {
  let isAllocatable = 0;
}
def PTR16 : RegisterClass<"PIC16F1", [i16], 8, (add SP, FSR0)> {
  let isAllocatable = 0;
}
def CCR : RegisterClass<"PIC16F1", [i8], 8, (add CARRY)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}
