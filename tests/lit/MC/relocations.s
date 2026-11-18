; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: llvm-readelf -h -r %t.o | FileCheck %s

; CHECK: Machine: 5046

    movlw lo8(sym+3)
; CHECK: 00000000 {{.*}} R_PIC16F1_LO8 {{.*}} sym + 3
    movlw hi8(sym)
; CHECK: 00000002 {{.*}} R_PIC16F1_HI8 {{.*}} sym + 0
    movlw pm_lo8(func)
; CHECK: 00000004 {{.*}} R_PIC16F1_PM_LO8 {{.*}} func + 0
    movlw pm_hi8(func)
; CHECK: 00000006 {{.*}} R_PIC16F1_PM_HI8 {{.*}} func + 0
    movlp page(func)
; CHECK: 00000008 {{.*}} R_PIC16F1_PAGE7 {{.*}} func + 0
    call func
; CHECK: 0000000a {{.*}} R_PIC16F1_CALL11 {{.*}} func + 0
    goto func
; CHECK: 0000000c {{.*}} R_PIC16F1_CALL11 {{.*}} func + 0
    movlb bank(sym)
; CHECK: 0000000e {{.*}} R_PIC16F1_BANK5 {{.*}} sym + 0
    movf sym+1, W
; CHECK: 00000010 {{.*}} R_PIC16F1_F7 {{.*}} sym + 1
    bra func
; CHECK: 00000012 {{.*}} R_PIC16F1_BRA9 {{.*}} func + 0

; A branch to a local label in the same section needs no relocation.
    bra local
    nop
local:
    return
; CHECK-NOT: local

    .data
    .2byte pm(func)
; CHECK: 00000000 {{.*}} R_PIC16F1_PM16 {{.*}} func + 0
    .2byte sym
; CHECK: 00000002 {{.*}} R_PIC16F1_16 {{.*}} sym + 0
