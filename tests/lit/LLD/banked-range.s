; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: not ld.lld -T %S/Inputs/layout.ld %t.o -o %t.elf 2>&1 | FileCheck %s

; Flash cannot be reached with banked addressing: the linker must refuse.
; CHECK: address is not reachable with banked addressing

    .text
    .globl _start
_start:
    movlb bank(table)
    movf table, W

    .section .rodata,"a",@progbits
table:
    .byte 1
