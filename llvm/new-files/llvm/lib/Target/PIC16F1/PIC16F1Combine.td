//===-- PIC16F1Combine.td - GlobalISel combine rules --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

// Before legalization, on the types the program uses: LLVM's all_combines
// group (as of LLVM 23) without three rules. Two cost more than they save
// where a multiply is a library call and a comparison result is a branch:
//
//  * sub_of_mul_const turns a - x*C into a + x*(-C). The multiply routine
//    loops once per bit up to the top set bit of its second operand, and
//    -C has all the high bits set.
//  * hoist_logic_op_with_same_opcode_hands is replaced by the rule above.
//  * match_selects (from select_combines) turns a select between constants
//    into arithmetic on the condition. The condition is itself a select
//    here, so that adds work.
// hoist_logic_op_with_same_opcode_hands, except for truncations: it turns
// and(trunc x, trunc y) into trunc(and x, y), and narrowing turns that
// straight back, so the two rules would run for ever, creating new
// instructions each round. The narrow form is also the cheaper one here.
def hoist_logic_op_except_trunc : GICombineRule<
  (defs root:$root, instruction_steps_matchdata:$info),
  (match (wip_match_opcode G_AND, G_OR, G_XOR):$root,
    [{ return !isLogicOfTruncs(*${root}, MRI) &&
              Helper.matchHoistLogicOpWithSameOpcodeHands(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildInstructionSteps(*${root}, ${info}); }])>;

// x - (x / y) * y is the remainder; see matchSubMulUDiv.
def sub_mul_udiv_matchdata : GIDefMatchData<"const MachineInstr *">;
def sub_mul_udiv_to_urem : GICombineRule<
  (defs root:$root, sub_mul_udiv_matchdata:$div),
  (match (wip_match_opcode G_SUB):$root,
         [{ return matchSubMulUDiv(*${root}, MRI, ${div}); }]),
  (apply [{ applySubMulUDiv(*${root}, MRI, B, ${div}); }])>;

// A signed division by a power of two of up to 16 bits is left for the
// legalizer, which has a short form for it; wider ones become shifts here.
def sdiv_by_pow2_wide : GICombineRule<
  (defs root:$root),
  (match (G_SDIV $dst, $x, $y, (MIFlags (not IsExact))):$root,
   [{ return MRI.getType(${x}.getReg()).getScalarSizeInBits() > 16 &&
             Helper.matchDivByPow2(*${root}, /*IsSigned=*/true); }]),
  (apply [{ Helper.applySDivByPow2(*${root}); }])>;

// x * (2^a + 2^b) and x * (2^a - 2^b) as two shifts and an add or subtract,
// when the shifts are short: far quicker than the multiply routine, in
// about the room of the call.
def mul_by_two_bits_matchdata : GIDefMatchData<"std::tuple<unsigned, unsigned, bool>">;
def mul_by_two_bits : GICombineRule<
  (defs root:$root, mul_by_two_bits_matchdata:$info),
  (match (wip_match_opcode G_MUL):$root,
         [{ return matchMulByTwoBits(*${root}, MRI, std::get<0>(${info}),
                                     std::get<1>(${info}),
                                     std::get<2>(${info})); }]),
  (apply [{ applyMulByTwoBits(*${root}, MRI, B, std::get<0>(${info}),
                              std::get<1>(${info}), std::get<2>(${info})); }])>;

// LLVM's zext_trunc, which turns zext(trunc x) into x when the truncation
// is known to lose nothing, except in front of a right shift by a constant:
// there the extension is what says that the shift is a shift of one byte.
def zext_trunc_keep_byte_shift : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_TRUNC $src, $x, (MIFlags NoUWrap)),
          (G_ZEXT $root, $src),
   [{ return !feedsByteShift(${root}.getReg(), MRI) &&
             Helper.matchZextOfTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def pic16f1_prelegalizer_combines : GICombineGroup<[
    integer_reassoc_combines, trivial_combines, vector_ops_combines,
    freeze_combines, cast_combines, insert_vec_elt_combines,
    extract_vec_elt_combines, combines_for_extload,
    combine_extracted_vector_load, undef_combines, identity_combines,
    phi_combines, simplify_add_to_sub, hoist_logic_op_except_trunc,
    shifts_too_big, reassocs, ptr_add_immed_chain, cmp_combines,
    shl_ashr_to_sext_inreg, neg_and_one_to_sext_inreg, sext_inreg_of_load,
    width_reduction_combines, select_undef_cmp, select_constant_cmp,
    select_to_iminmax, select_zero_false, select_not,
    known_bits_simplifications, trunc_shift, not_cmp_fold,
    opt_brcond_by_inverting_cond, const_combines, xor_of_and_with_same_reg,
    ptr_add_with_zero, shift_immed_chain, shift_of_shifted_logic_chain,
    load_or_combine, sub_mul_udiv_to_urem, mul_by_two_bits, div_rem_to_divrem,
    funnel_shift_combines,
    bitreverse_shift, commute_shift, form_bitfield_extract,
    constant_fold_binops, constant_fold_fma, constant_fold_cast_op,
    constant_fold_unary_int_op, fabs_fneg_fold, mulh_combines,
    redundant_neg_operands, and_or_disjoint_mask, fma_combines,
    fold_binop_into_select, intrem_combines, udiv_by_pow2,
    sdiv_by_pow2_wide, udiv_by_const, sdiv_by_const,
    fdiv_repeated_divison, sub_add_reg, select_to_minmax, fsub_to_fneg,
    commute_constant_to_rhs, match_ands, match_ors, or_and_xor_combines,
    simplify_neg_minmax, combine_concat_vector, sext_trunc, zext_trunc_keep_byte_shift,
    prefer_sign_combines, shuffle_combines, combine_use_vector_truncate,
    merge_combines, overflow_combines, truncsat_combines,
    lshr_of_trunc_of_lshr, ctls_combines, add_shift, sub_one_from_sub,
    binop_with_neg, sub_minus_one, max_min_combines, avgfloor_u_match,
    avgfloor_s_match, avgceil_u_match, avgceil_s_match, combine_or_of_and]>;

def PIC16F1PreLegalizerCombiner
    : GICombiner<"PIC16F1PreLegalizerCombinerImpl",
                 [pic16f1_prelegalizer_combines]> {
}

// A 16-bit add, subtract, logic operation or right shift of which only the
// low byte is used is an operation on bytes, at half the cost.
def narrow_trunc_binop : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_TRUNC):$root,
         [{ return matchNarrowTruncBinop(*${root}, MRI); }]),
  (apply [{ applyNarrowTruncBinop(*${root}, MRI, B); }])>;

// A right shift or logic operation whose 16-bit operands have a zero high
// byte is the zero extension of the same operation on the low bytes.
def narrow_binop_of_zext : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_LSHR, G_AND, G_OR, G_XOR):$root,
         [{ return matchNarrowBinopOfZExt(*${root}, MRI, *VT); }]),
  (apply [{ applyNarrowBinopOfZExt(*${root}, MRI, B, *VT); }])>;

// x + (c ? a : 0) is c ? x + a : x, and likewise for subtract, or and xor;
// x + (a < b), which counts, is the same with a = 1.
// The select becomes a branch around the operation, which then costs
// nothing when the condition fails, instead of a branch around a load of
// zero followed by the operation.
def binop_of_select_identity : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_ADD, G_SUB, G_OR, G_XOR):$root,
         [{ return matchBinopOfSelectIdentity(*${root}, MRI); }]),
  (apply [{ applyBinopOfSelectIdentity(*${root}, MRI, B); }])>;

// A 16-bit shift by eight or more moves a byte: write it as a byte
// operation and a merge or extension, which the rules here see through.
def wide_shift_to_bytes : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHL, G_LSHR, G_ASHR):$root,
         [{ return matchWideShiftToBytes(*${root}, MRI); }]),
  (apply [{ applyWideShiftToBytes(*${root}, MRI, B); }])>;

// A 16-bit or, xor or add in which one operand has a zero byte changes only
// the other byte of the result.
def bytewise_binop : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_OR, G_XOR, G_ADD):$root,
         [{ return matchBytewiseBinop(*${root}, MRI, *VT); }]),
  (apply [{ applyBytewiseBinop(*${root}, MRI, B, *VT); }])>;

// After legalization values are 8 and 16 bits wide and splitting has left
// behind operations on constants and on undefined pieces. Fold those, and
// only use rules that do not widen anything.
def PIC16F1PostLegalizerCombiner
    : GICombiner<"PIC16F1PostLegalizerCombinerImpl",
                 [identity_combines, constant_fold_binops, undef_combines,
                  redundant_and, commute_constant_to_rhs, const_combines,
                  known_bits_simplifications, select_combines,
                  narrow_trunc_binop, narrow_binop_of_zext,
                  binop_of_select_identity, wide_shift_to_bytes,
                  bytewise_binop, unmerge_merge, merge_unmerge, unmerge_cst,
                  unmerge_undef, unmerge_dead_to_trunc, unmerge_zext_to_zext,
                  truncate_of_zext, truncate_of_sext, truncate_of_anyext,
                  anyext_trunc_fold]> {
}
