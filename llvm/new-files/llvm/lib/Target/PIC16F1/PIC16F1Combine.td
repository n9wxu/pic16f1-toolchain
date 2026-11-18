//===-- PIC16F1Combine.td - GlobalISel combine rules --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

// Before legalization, on the types the program uses: everything.
def PIC16F1PreLegalizerCombiner
    : GICombiner<"PIC16F1PreLegalizerCombinerImpl", [all_combines]> {
}

// After legalization values are 8 and 16 bits wide and splitting has left
// behind operations on constants and on undefined pieces. Fold those, and
// only use rules that do not widen anything.
def PIC16F1PostLegalizerCombiner
    : GICombiner<"PIC16F1PostLegalizerCombinerImpl",
                 [identity_combines, constant_fold_binops, undef_combines,
                  redundant_and, commute_constant_to_rhs, const_combines,
                  known_bits_simplifications, select_combines]> {
}
