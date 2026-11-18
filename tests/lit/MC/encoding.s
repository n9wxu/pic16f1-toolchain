; RUN: llvm-mc -triple=pic16f1 -show-encoding %s | FileCheck %s

; One instruction of each encoding format. The full instruction set is
; compared with Microchip's assembler by scripts/test-assembler.sh.

    addwf 0x25, F
; CHECK: addwf 0x25, F ; encoding: [0xa5,0x07]
    addwf 0x25, W
; CHECK: addwf 0x25, W ; encoding: [0x25,0x07]
    ADDWF 0x25, 1
; CHECK: addwf 0x25, F ; encoding: [0xa5,0x07]
    movwf 0x7f
; CHECK: movwf 0x7f ; encoding: [0xff,0x00]
    bsf 0x0b, 7
; CHECK: bsf 0x0b, 7 ; encoding: [0x8b,0x17]
    movlw 0xa5
; CHECK: movlw 165 ; encoding: [0xa5,0x30]
    call 0x123
; CHECK: call 291 ; encoding: [0x23,0x21]
    goto 0x7ff
; CHECK: goto 2047 ; encoding: [0xff,0x2f]
    movlb 31
; CHECK: movlb 31 ; encoding: [0x3f,0x00]
    movlp 0x7f
; CHECK: movlp 127 ; encoding: [0xff,0x31]
    addfsr FSR1, -32
; CHECK: addfsr FSR1, -32 ; encoding: [0x60,0x31]
    moviw ++FSR0
; CHECK: moviw ++FSR0 ; encoding: [0x10,0x00]
    movwi FSR1--
; CHECK: movwi FSR1-- ; encoding: [0x1f,0x00]
    moviw -3[FSR1]
; CHECK: moviw -3[FSR1] ; encoding: [0x7d,0x3f]
    movwi 31[FSR0]
; CHECK: movwi 31[FSR0] ; encoding: [0x9f,0x3f]
    retfie
; CHECK: retfie ; encoding: [0x09,0x00]

; A specifier applied to a constant is evaluated by the assembler.
    movlw hi8(0x1234)
; CHECK: movlw hi8(4660) ; encoding: [0x12,0x30]
    movlb bank(0x19A)
; CHECK: movlb bank(410) ; encoding: [0x23,0x00]
    movf off(0x19A), W
; CHECK: movf off(410), W ; encoding: [0x1a,0x08]
