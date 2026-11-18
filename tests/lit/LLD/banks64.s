; RUN: llvm-mc -triple=pic16f1 -mcpu=pic16f18877 -filetype=obj %s -o %t64.o
; RUN: ld.lld -T %S/Inputs/high.ld %t64.o -o %t64.elf
; RUN: llvm-objdump -d --mcpu=pic16f18877 -j .text %t64.elf | FileCheck %s
; RUN: llvm-mc -triple=pic16f1 -mcpu=pic16f1937 -filetype=obj %s -o %t32.o
; RUN: not ld.lld -T %S/Inputs/high.ld %t32.o -o %t32.elf 2>&1 | FileCheck %s --check-prefix=ERR

; var is at linear offset 3000: bank 3000 / 80 = 37, file address
; 0x20 + 3000 % 80 = 0x48. A 64-bank device reaches it; a 32-bank one cannot.

; CHECK: movlb 37
; CHECK: movf 0x48, W
; ERR: beyond bank 31

    .text
    .globl _start
_start:
    movlb bank(var)
    movf var, W

    .data
    .globl var
var:
    .byte 0
