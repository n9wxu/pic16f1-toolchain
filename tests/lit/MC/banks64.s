; RUN: llvm-mc -triple=pic16f1 -mcpu=pic16f18877 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=pic16f1 -mcpu=pic16f18877 -filetype=obj %s -o %t.o
; RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump -d --mcpu=pic16f18877 %t.o | FileCheck %s --check-prefix=DIS

; Devices with 64 banks encode MOVLB as 00 0001 01kk kkkk. The words below
; are what Microchip's pic-as produces for a PIC16F18877.

    movlb 5
; CHECK: movlb 5 ; encoding: [0x45,0x01]
; DIS:   45 01 movlb 5
    movlb 40
; CHECK: movlb 40 ; encoding: [0x68,0x01]
; DIS:   68 01 movlb 40
    movlb 63
; CHECK: movlb 63 ; encoding: [0x7f,0x01]
    movlb bank(sym)
; RELOC: R_PIC16F1_BANK6 {{.*}} sym + 0
