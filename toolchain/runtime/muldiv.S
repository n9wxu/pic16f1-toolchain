; Integer multiply and divide for 8, 16 and 32 bits: the routines the
; compiler calls for operations the core has no instruction for. (The 64-bit
; ones are in muldiv64.c.)
;
; Calling convention: the first operand is in R0.. (0x70 up, low byte
; first), the second follows it, and the result comes back in R0... So
;    8 bits:  0x70 op 0x71            -> 0x70
;   16 bits:  0x70/71 op 0x72/73      -> 0x70/71
;   32 bits:  0x70-73 op 0x74-77      -> 0x70-73
; R0-R7 (0x70-0x77), TMP0/TMP1 (0x7E/0x7F), FSR0 (0x04/0x05), W and the
; flags are free for a callee to change; anything else is preserved.
; STATUS is 0x03: bit 0 is carry, bit 2 is zero.
;
; Multiplication is shift-and-add and stops when no bits of the multiplier
; are left. Division is restoring division, one quotient bit per round; the
; quotient replaces the dividend bit by bit. Dividing by zero gives an
; all-ones quotient.
;
; Every routine is in a section of its own, so that a program gets only the
; ones it calls.

; Built twice, like crt0.S: with -DONE_PAGE for programs within one page,
; where PCLATH is never set.
#ifdef ONE_PAGE
#define PAGESEL(target)
#else
#define PAGESEL(target) movlp page(target)
#endif

;------------------------------------------------------------------------------
; 8 bits
;------------------------------------------------------------------------------

    .section .text.__mulqi3,"ax",@progbits
    .globl  __mulqi3
__mulqi3:
    clrf    0x72            ; product
.Lmul8_loop:
    lsrf    0x71, F         ; next multiplier bit into carry
    btfss   0x03, 0
    bra     .Lmul8_next
    movf    0x70, W
    addwf   0x72, F
.Lmul8_next:
    lslf    0x70, F
    movf    0x71, F         ; any multiplier bits left?
    btfss   0x03, 2
    bra     .Lmul8_loop
    movf    0x72, W
    movwf   0x70
    return

; Quotient in 0x70, remainder in 0x72. The compiler relies on the
; remainder being there when it wants both.
    .section .text.__udivqi3,"ax",@progbits
    .globl  __udivqi3
__udivqi3:
    clrf    0x72
    movlw   8
    movwf   0x7E
.Ldiv8_loop:
    lslf    0x70, F         ; remainder:dividend one place left
    rlf     0x72, F
    btfsc   0x03, 0         ; a ninth bit: larger than any divisor
    bra     .Ldiv8_sub
    movf    0x71, W
    subwf   0x72, W         ; carry set if remainder >= divisor
    btfss   0x03, 0
    bra     .Ldiv8_next
.Ldiv8_sub:
    movf    0x71, W
    subwf   0x72, F
    bsf     0x70, 0
.Ldiv8_next:
    decfsz  0x7E, F
    bra     .Ldiv8_loop
    return

    .section .text.__umodqi3,"ax",@progbits
    .globl  __umodqi3
__umodqi3:
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    0x72, W
    movwf   0x70
    return

; Both operands to their absolute values.
    .section .text.__pic_absqi2,"ax",@progbits
    .globl  __pic_absqi2
__pic_absqi2:
    movf    0x70, W
    btfsc   0x70, 7
    sublw   0
    movwf   0x70
    movf    0x71, W
    btfsc   0x71, 7
    sublw   0
    movwf   0x71
    return

; The quotient is negative when the signs differ. TMP1 keeps the sign;
; the unsigned routine does not use it.
    .section .text.__divqi3,"ax",@progbits
    .globl  __divqi3
__divqi3:
    movf    0x70, W
    xorwf   0x71, W
    movwf   0x7F
    PAGESEL(__pic_absqi2)
    call    __pic_absqi2
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    0x70, W
    btfsc   0x7F, 7
    sublw   0
    movwf   0x70
    return

; The remainder has the sign of the dividend.
    .section .text.__modqi3,"ax",@progbits
    .globl  __modqi3
__modqi3:
    movf    0x70, W
    movwf   0x7F
    PAGESEL(__pic_absqi2)
    call    __pic_absqi2
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    0x72, W
    btfsc   0x7F, 7
    sublw   0
    movwf   0x70
    return

;------------------------------------------------------------------------------
; 16 bits
;------------------------------------------------------------------------------

    .section .text.__mulhi3,"ax",@progbits
    .globl  __mulhi3
__mulhi3:
    clrf    0x74            ; product
    clrf    0x75
.Lmul16_loop:
    lsrf    0x73, F         ; next multiplier bit into carry
    rrf     0x72, F
    btfss   0x03, 0
    bra     .Lmul16_next
    movf    0x70, W
    addwf   0x74, F
    movf    0x71, W
    addwfc  0x75, F
.Lmul16_next:
    lslf    0x70, F
    rlf     0x71, F
    movf    0x72, W         ; any multiplier bits left?
    iorwf   0x73, W
    btfss   0x03, 2
    bra     .Lmul16_loop
    movf    0x74, W
    movwf   0x70
    movf    0x75, W
    movwf   0x71
    return

; Quotient in 0x70/71, remainder in 0x74/75. The compiler relies on the
; remainder being there when it wants both.
    .section .text.__udivhi3,"ax",@progbits
    .globl  __udivhi3
__udivhi3:
    clrf    0x74
    clrf    0x75
    movlw   16
    movwf   0x7E
.Ldiv16_loop:
    lslf    0x70, F         ; remainder:dividend one place left
    rlf     0x71, F
    rlf     0x74, F
    rlf     0x75, F
    btfsc   0x03, 0         ; a seventeenth bit: larger than any divisor
    bra     .Ldiv16_sub
    movf    0x72, W
    subwf   0x74, W
    movf    0x73, W
    subwfb  0x75, W         ; carry set if remainder >= divisor
    btfss   0x03, 0
    bra     .Ldiv16_next
.Ldiv16_sub:
    movf    0x72, W
    subwf   0x74, F
    movf    0x73, W
    subwfb  0x75, F
    bsf     0x70, 0
.Ldiv16_next:
    decfsz  0x7E, F
    bra     .Ldiv16_loop
    return

    .section .text.__umodhi3,"ax",@progbits
    .globl  __umodhi3
__umodhi3:
    PAGESEL(__udivhi3)
    call    __udivhi3
    movf    0x74, W
    movwf   0x70
    movf    0x75, W
    movwf   0x71
    return

; Both operands to their absolute values; __pic_neghi negates the first.
    .section .text.__pic_abshi2,"ax",@progbits
    .globl  __pic_abshi2
    .globl  __pic_neghi
__pic_abshi2:
    btfss   0x73, 7
    bra     .Labs16_first
    comf    0x72, F
    comf    0x73, F
    incf    0x72, F
    btfsc   0x03, 2
    incf    0x73, F
.Labs16_first:
    btfss   0x71, 7
    return
__pic_neghi:
    comf    0x70, F
    comf    0x71, F
    incf    0x70, F
    btfsc   0x03, 2
    incf    0x71, F
    return

    .section .text.__divhi3,"ax",@progbits
    .globl  __divhi3
__divhi3:
    movf    0x71, W
    xorwf   0x73, W
    movwf   0x7F
    PAGESEL(__pic_abshi2)
    call    __pic_abshi2
    PAGESEL(__udivhi3)
    call    __udivhi3
    btfss   0x7F, 7
    return
    PAGESEL(__pic_neghi)
    goto    __pic_neghi

    .section .text.__modhi3,"ax",@progbits
    .globl  __modhi3
__modhi3:
    movf    0x71, W
    movwf   0x7F
    PAGESEL(__pic_abshi2)
    call    __pic_abshi2
    PAGESEL(__umodhi3)
    call    __umodhi3
    btfss   0x7F, 7
    return
    PAGESEL(__pic_neghi)
    goto    __pic_neghi

;------------------------------------------------------------------------------
; 32 bits. Four more bytes are needed than R0-R7 give: FSR0 and TMP0/TMP1.
;------------------------------------------------------------------------------

    .section .text.__mulsi3,"ax",@progbits
    .globl  __mulsi3
__mulsi3:
    clrf    0x04            ; product in FSR0L, FSR0H, TMP0, TMP1
    clrf    0x05
    clrf    0x7E
    clrf    0x7F
.Lmul32_loop:
    lsrf    0x77, F         ; next multiplier bit into carry
    rrf     0x76, F
    rrf     0x75, F
    rrf     0x74, F
    btfss   0x03, 0
    bra     .Lmul32_next
    movf    0x70, W
    addwf   0x04, F
    movf    0x71, W
    addwfc  0x05, F
    movf    0x72, W
    addwfc  0x7E, F
    movf    0x73, W
    addwfc  0x7F, F
.Lmul32_next:
    lslf    0x70, F
    rlf     0x71, F
    rlf     0x72, F
    rlf     0x73, F
    movf    0x74, W         ; any multiplier bits left?
    iorwf   0x75, W
    iorwf   0x76, W
    iorwf   0x77, W
    btfss   0x03, 2
    bra     .Lmul32_loop
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

; R0-R3 = FSR0L, FSR0H, TMP0, TMP1: where the product and the remainder
; are built.
    .section .text.__pic_movsi,"ax",@progbits
    .globl  __pic_movsi
__pic_movsi:
    movf    0x04, W
    movwf   0x70
    movf    0x05, W
    movwf   0x71
    movf    0x7E, W
    movwf   0x72
    movf    0x7F, W
    movwf   0x73
    return

; Quotient in 0x70-73, remainder in FSR0L, FSR0H, TMP0, TMP1. The round
; counter is R8, which the caller expects back: it goes on the stack.
    .section .text.__udivsi3,"ax",@progbits
    .globl  __udivsi3
__udivsi3:
    movf    0x78, W
    movwi   --FSR1
    clrf    0x04
    clrf    0x05
    clrf    0x7E
    clrf    0x7F
    movlw   32
    movwf   0x78
.Ldiv32_loop:
    lslf    0x70, F         ; remainder:dividend one place left
    rlf     0x71, F
    rlf     0x72, F
    rlf     0x73, F
    rlf     0x04, F
    rlf     0x05, F
    rlf     0x7E, F
    rlf     0x7F, F
    btfsc   0x03, 0         ; a thirty-third bit: larger than any divisor
    bra     .Ldiv32_sub
    movf    0x74, W
    subwf   0x04, W
    movf    0x75, W
    subwfb  0x05, W
    movf    0x76, W
    subwfb  0x7E, W
    movf    0x77, W
    subwfb  0x7F, W         ; carry set if remainder >= divisor
    btfss   0x03, 0
    bra     .Ldiv32_next
.Ldiv32_sub:
    movf    0x74, W
    subwf   0x04, F
    movf    0x75, W
    subwfb  0x05, F
    movf    0x76, W
    subwfb  0x7E, F
    movf    0x77, W
    subwfb  0x7F, F
    bsf     0x70, 0
.Ldiv32_next:
    decfsz  0x78, F
    bra     .Ldiv32_loop
    moviw   FSR1++
    movwf   0x78
    return

; Quotient in 0x70-73 and remainder in 0x74-77: what the compiler calls
; when it wants both.
    .section .text.__udivmodsi4,"ax",@progbits
    .globl  __udivmodsi4
__udivmodsi4:
    PAGESEL(__udivsi3)
    call    __udivsi3
    movf    0x04, W
    movwf   0x74
    movf    0x05, W
    movwf   0x75
    movf    0x7E, W
    movwf   0x76
    movf    0x7F, W
    movwf   0x77
    return

    .section .text.__umodsi3,"ax",@progbits
    .globl  __umodsi3
__umodsi3:
    PAGESEL(__udivsi3)
    call    __udivsi3
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

; Both operands to their absolute values; __pic_negsi negates the first.
    .section .text.__pic_abssi2,"ax",@progbits
    .globl  __pic_abssi2
    .globl  __pic_negsi
__pic_abssi2:
    btfss   0x77, 7
    bra     .Labs32_first
    comf    0x74, F
    comf    0x75, F
    comf    0x76, F
    comf    0x77, F
    movlw   1
    addwf   0x74, F
    movlw   0
    addwfc  0x75, F
    addwfc  0x76, F
    addwfc  0x77, F
.Labs32_first:
    btfss   0x73, 7
    return
__pic_negsi:
    comf    0x70, F
    comf    0x71, F
    comf    0x72, F
    comf    0x73, F
    movlw   1
    addwf   0x70, F
    movlw   0
    addwfc  0x71, F
    addwfc  0x72, F
    addwfc  0x73, F
    return

; The sign waits on the stack: the unsigned routine uses every free byte.
    .section .text.__divsi3,"ax",@progbits
    .globl  __divsi3
__divsi3:
    movf    0x73, W
    xorwf   0x77, W
    movwi   --FSR1
    PAGESEL(__pic_abssi2)
    call    __pic_abssi2
    PAGESEL(__udivsi3)
    call    __udivsi3
    moviw   FSR1++
    andlw   0x80
    btfsc   0x03, 2
    return
    PAGESEL(__pic_negsi)
    goto    __pic_negsi

    .section .text.__modsi3,"ax",@progbits
    .globl  __modsi3
__modsi3:
    movf    0x73, W
    movwi   --FSR1
    PAGESEL(__pic_abssi2)
    call    __pic_abssi2
    PAGESEL(__umodsi3)
    call    __umodsi3
    moviw   FSR1++
    andlw   0x80
    btfsc   0x03, 2
    return
    PAGESEL(__pic_negsi)
    goto    __pic_negsi
