; RUN: llvm-mc -triple=pic16f1 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s

    subwf 0x20, W
; CHECK: 20 02 subwf 0x20, W
    btfss 0x03, 2
; CHECK: 03 1d btfss 0x03, 2
    bra 5
; CHECK: 05 32 bra 5
    bra -3
; CHECK: fd 33 bra -3
    addfsr FSR0, 31
; CHECK: 1f 31 addfsr FSR0, 31
    moviw -32[FSR1]
; CHECK: 60 3f moviw -32[FSR1]
    movwi --FSR1
; CHECK: 1d 00 movwi --FSR1
    clrw
; CHECK: 03 01 clrw
    retlw 0x42
; CHECK: 42 34 retlw 66
