; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: ld.lld -T %S/Inputs/banks.ld %t.o -o %t.elf
; RUN: llvm-nm -n %t.elf | FileCheck %s

; The script starts .data at linear address 0x2050: offset 80 in the linear
; view, so the first of the second bank's 80 bytes. The next bank boundary
; is at 0x20A0.

; A section of up to 16 bytes that would cross a bank boundary starts in the
; next bank instead; code selects the bank once for the whole variable.
; A larger one is placed without regard to banks.

; CHECK: 00802050 D filler
; CHECK: 008020a0 D pair
; CHECK: 008020a2 D large

    .text
    .globl _start
_start:
    return

    .section .data.filler,"aw",@progbits
    .globl filler
filler:
    .space 79

    ; Would occupy 0x209F and 0x20A0.
    .section .data.pair,"aw",@progbits
    .globl pair
pair:
    .space 2

    .section .data.large,"aw",@progbits
    .globl large
large:
    .space 100
