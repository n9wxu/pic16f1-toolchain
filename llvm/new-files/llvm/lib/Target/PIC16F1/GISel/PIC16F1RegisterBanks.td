//===-- PIC16F1RegisterBanks.td - Register banks ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// One bank: the byte registers in common RAM and their 16-bit pairs.
def GPRRegBank : RegisterBank<"GPR", [GPR16]>;
