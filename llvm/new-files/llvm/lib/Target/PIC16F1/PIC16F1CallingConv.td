//===-- PIC16F1CallingConv.td - Calling conventions ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Arguments use R0-R7, bytes in single registers and 16-bit values in pairs,
// then the software stack. Values wider than 16 bits are split into 16-bit
// parts, low part first. Results come back in R0-R7. R8-R13 are preserved
// across calls. A variadic function takes all of its arguments on the stack.
//
//===----------------------------------------------------------------------===//

def RetCC_PIC16F1 : CallingConv<[
  CCIfType<[i8], CCAssignToReg<[R0, R1, R2, R3, R4, R5, R6, R7]>>,
  CCIfType<[i16], CCAssignToReg<[RP0, RP1, RP2, RP3]>>
]>;

def CC_PIC16F1_Stack : CallingConv<[
  CCIfType<[i8], CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

def CC_PIC16F1 : CallingConv<[
  CCIfType<[i8], CCAssignToReg<[R0, R1, R2, R3, R4, R5, R6, R7]>>,
  CCIfType<[i16], CCAssignToReg<[RP0, RP1, RP2, RP3]>>,
  CCDelegateTo<CC_PIC16F1_Stack>
]>;

def CSR_Normal : CalleeSavedRegs<(sequence "R%u", 8, 13)>;
// For a function whose caller reads no register afterwards: main, called
// once by the startup code, and functions that do not return.
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// The 8- and 16-bit multiply and divide routines of the runtime library
// (muldiv.S) use only R0-R5 and the scratch bytes: R6, R7 and both FSRs
// survive a call to one of them as well.
def CSR_SmallMath : CalleeSavedRegs<(add R6, R7, FSR0L, FSR0H, SP,
                                         (sequence "R%u", 8, 13))>;

// An interrupt handler preserves every register it touches. W, STATUS, BSR,
// the FSRs and PCLATH are saved and restored by the hardware.
def CSR_Interrupt : CalleeSavedRegs<(add (sequence "R%u", 0, 13), TMP0, TMP1)>;
