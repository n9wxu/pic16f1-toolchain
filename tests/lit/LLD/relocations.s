; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: ld.lld -T %S/Inputs/layout.ld %t.o -o %t.elf
; RUN: llvm-objdump -d -j .text %t.elf | FileCheck %s
; RUN: llvm-objdump -s -j .data %t.elf | FileCheck %s --check-prefix=DATA

; Addresses:
;   start  byte 0x200  = word 0x100
;   func   byte 0x1A00 = word 0xD00  (page 0x0D, low 11 bits 0x500)
;   var    data 0x2050: linear offset 80, so bank 1, file address 0x20
;   var+85 linear offset 165, so bank 2, file address 0x25
;   table  data 0x8700 (flash seen through an FSR)

    .text
    .globl _start
_start:
    movlp page(func)
; CHECK: movlp 13
    call func
; CHECK: call 1280
    movlw pm_lo8(func)
; CHECK: movlw 0
    movlw pm_hi8(func)
; CHECK: movlw 13
    movlw lo8(var)
; CHECK: movlw 80
    movlw hi8(var)
; CHECK: movlw 32
    movlb bank(var)
; CHECK: movlb 1
    movf var, W
; CHECK: movf 0x20, W
    movlb bank(var+85)
; CHECK: movlb 2
    movwf var+85
; CHECK: movwf 0x25
    movlw lo8(table+2)
; CHECK: movlw 2
    movlw hi8(table)
; CHECK: movlw 135
    bra _start
; The BRA is 12 words after _start; the offset counts from the next word.
; CHECK: bra -13

    .section .far,"ax",@progbits
    .globl func
func:
    return

    .data
    .globl var
var:
    .2byte pm(func)
    .2byte table
; Word address of func, then the data address of table.
; DATA: 000d0087

    .section .rodata,"a",@progbits
table:
    .byte 1, 2, 3
