; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: not ld.lld -T %ldscript --defsym main=0 --defsym exit=0 %t.o -o %t.elf 2>&1 | FileCheck %s

; Code compiled with -mcmodel=small never sets PCLATH and refers to
; __pic16f1_one_page. The linker script then requires all code to lie in
; the first 2K words.
; CHECK: code compiled with -mcmodel=small, but the program is larger than one page

    .globl __pic16f1_one_page
    .section .reset,"ax",@progbits
    .globl _start
_start:
    return
    .text
    .space 0x1000
