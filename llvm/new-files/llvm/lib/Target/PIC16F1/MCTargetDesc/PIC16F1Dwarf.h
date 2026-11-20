//===-- PIC16F1Dwarf.h - Call frame conventions ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// How a debugger walks the call stack and finds a caller's stack frame
// (docs/spec/debug-interface.md in the toolchain's repository; LLDB's
// ABIPIC16F1 plugin has the same rules for code without this information).
//
// Return addresses are on the hardware stack, which a debug server shows at
// 0x810000, two bytes a level, as word addresses. The frame at level n
// returns to entry n and its caller is at level n - 1.
//
// A function with a stack frame has its frame's base in FSR1 while its own
// code runs, but not dependably just after a call: it takes its stack
// pointer back from the cell __sp only when it next needs it. What is
// dependable, in the function a program is stopped in, is the cell. So "the
// stack level" of a frame, the value the cell has while it runs, is:
//   in the innermost frame   the content of the cell;
//   in a caller's frame      what unwinding gives for FSR1.
// The two are told apart by BSR, which unwinding sets to 0xFF, a value the
// register itself cannot hold. A function with a stack frame that makes
// calls has its frame base at its stack level, and its caller's stack level
// is that plus the size of its frame. A function with a static frame leaves
// the level as it is.
//
// The cell is the first thing in RAM (the linker scripts see to it), so
// that these descriptions, which cannot hold a relocation, can name it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1DWARF_H
#define LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1DWARF_H

#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace PIC16F1Dwarf {

// DWARF numbers the compiler gives no variable but unwinding needs.
enum { FSR1 = 23, RP6 = 22, PC = 32, STKPTR = 33, BSR = 34 };

const uint64_t DataBase = 0x800000;
const uint64_t StackBase = 0x810000;
/// The stack pointer cell __sp, as an address in the ELF file.
const uint64_t Cell = 0x802000;
/// What unwinding leaves in BSR.
const uint8_t CallerMark = 0xFF;

inline void op(SmallVectorImpl<char> &E, uint8_t Byte) { E.push_back(Byte); }
inline void uleb(SmallVectorImpl<char> &E, uint64_t V) {
  raw_svector_ostream OS(E);
  encodeULEB128(V, OS);
}

/// Push the address StackBase + 2 * stkptr. (Constants are added with
/// DW_OP_plus_uconst: one pushed by itself has the size of an address,
/// which a debugger may take to be a pointer's 16 bits.)
inline void stackEntryAddress(SmallVectorImpl<char> &E) {
  op(E, dwarf::DW_OP_bregx);
  uleb(E, STKPTR);
  op(E, 0);
  op(E, dwarf::DW_OP_lit1);
  op(E, dwarf::DW_OP_shl);
  op(E, dwarf::DW_OP_plus_uconst);
  uleb(E, StackBase);
}

/// Push the call frame address: a number that identifies the frame and,
/// as debuggers expect of a stack that grows downward, is lower the deeper
/// the frame: StackBase + 2 * ((30 - stkptr) & 31). (An empty stack, stkptr
/// 31, gives the highest.) Nothing is stored there.
inline void frameAddress(SmallVectorImpl<char> &E) {
  op(E, dwarf::DW_OP_const1u);
  op(E, 0x1E);
  op(E, dwarf::DW_OP_bregx);
  uleb(E, STKPTR);
  op(E, 0);
  op(E, dwarf::DW_OP_minus);
  op(E, dwarf::DW_OP_const1u);
  op(E, 0x1F);
  op(E, dwarf::DW_OP_and);
  op(E, dwarf::DW_OP_lit1);
  op(E, dwarf::DW_OP_shl);
  op(E, dwarf::DW_OP_plus_uconst);
  uleb(E, StackBase);
}

/// Push the content of the cell.
inline void cell(SmallVectorImpl<char> &E) {
  op(E, dwarf::DW_OP_lit0);
  op(E, dwarf::DW_OP_plus_uconst);
  uleb(E, Cell);
  op(E, dwarf::DW_OP_deref_size);
  op(E, 2);
}

/// Push what an interrupt handler that makes calls has kept of the cell:
/// the two bytes after it.
inline void savedCell(SmallVectorImpl<char> &E) {
  op(E, dwarf::DW_OP_lit0);
  op(E, dwarf::DW_OP_plus_uconst);
  uleb(E, Cell + 2);
  op(E, dwarf::DW_OP_deref_size);
  op(E, 2);
}

/// Push the stack level of the frame: the cell in the innermost frame,
/// the unwound FSR1 in a caller's.
inline void stackLevel(SmallVectorImpl<char> &E) {
  SmallString<16> Inner, Outer;
  cell(Inner);
  op(Outer, dwarf::DW_OP_breg0 + FSR1);
  op(Outer, 0);
  // if (BSR == CallerMark) goto Outer
  op(E, dwarf::DW_OP_bregx);
  uleb(E, BSR);
  op(E, 0);
  op(E, dwarf::DW_OP_const1u);
  op(E, CallerMark);
  op(E, dwarf::DW_OP_eq);
  op(E, dwarf::DW_OP_bra);
  op(E, Inner.size() + 3);
  op(E, 0);
  E.append(Inner.begin(), Inner.end());
  op(E, dwarf::DW_OP_skip);
  op(E, Outer.size());
  op(E, 0);
  E.append(Outer.begin(), Outer.end());
}

/// DW_CFA_val_expression: the caller's \p Reg is the value of \p Expr.
inline void valExpression(SmallVectorImpl<char> &Out, unsigned Reg,
                          const SmallVectorImpl<char> &Expr) {
  op(Out, dwarf::DW_CFA_val_expression);
  uleb(Out, Reg);
  uleb(Out, Expr.size());
  Out.append(Expr.begin(), Expr.end());
}

/// The rules that hold for every function: the call frame address falls as
/// the hardware stack deepens, the caller's pc is the entry (a word
/// address) doubled, its stkptr is one less, its BSR is the mark, and its
/// stack level is this frame's, which is right for a function with a static
/// frame and is replaced by one with a stack frame.
inline void commonRules(SmallVectorImpl<char> &Out) {
  SmallString<32> E;
  frameAddress(E);
  op(Out, dwarf::DW_CFA_def_cfa_expression);
  uleb(Out, E.size());
  Out.append(E.begin(), E.end());

  E.clear();
  stackEntryAddress(E);

  op(E, dwarf::DW_OP_deref_size);
  op(E, 2);
  op(E, dwarf::DW_OP_lit1);
  op(E, dwarf::DW_OP_shl);
  valExpression(Out, PC, E);

  E.clear();
  op(E, dwarf::DW_OP_bregx);
  uleb(E, STKPTR);
  op(E, 0x7f); // -1
  op(E, dwarf::DW_OP_lit31);
  op(E, dwarf::DW_OP_and);
  valExpression(Out, STKPTR, E);

  E.clear();
  op(E, dwarf::DW_OP_const1u);
  op(E, CallerMark);
  valExpression(Out, BSR, E);

  E.clear();
  stackLevel(E);
  valExpression(Out, FSR1, E);
}

} // namespace PIC16F1Dwarf
} // namespace llvm

#endif
