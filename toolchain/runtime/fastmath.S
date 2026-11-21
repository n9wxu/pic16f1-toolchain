; The fast arithmetic library (libpicfast.a, linked by -mmath=fast): the
; routines of muldiv.S and float.c that take the time, written for speed
; where those are written for size.
;
;   __mulhi3, __mulsi3   a multiplier byte at a time, eight unrolled rounds
;                        each, and a zero byte in a few instructions
;   __umodsi3            (which the other 32-bit divisions call) by the size
;                        of the divisor: a remainder of one, two or four
;                        bytes, and only the rounds whose quotient bit can
;                        be 1
;   __pic_fmul24,        the 24-bit multiplication and division at the heart
;   __pic_fdiv24         of float multiply and divide, for float.c built
;                        with -DFLOAT_FAST
;
; The results are the same, bit for bit, as the small routines'; what
; differs is the time and some 500 words of flash. Calling convention and
; the registers free to use are as in muldiv.S. The multiplications call a
; row routine, one level of the hardware stack.

#define R0   0x70
#define R1   0x71
#define R2   0x72
#define R3   0x73
#define R4   0x74
#define R5   0x75
#define R6   0x76
#define R7   0x77
#define TMP0 0x7E
#define TMP1 0x7F
#define FSR0L 0x04
#define FSR0H 0x05
#define FSR1L 0x06
#define FSR1H 0x07
#define STATUS 0x03
#define C 0
#define Z 2

#ifdef ONE_PAGE
#define PAGESEL(target)
#else
#define PAGESEL(target) movlp page(target)
#endif

;------------------------------------------------------------------------------
; 16 x 16 -> 16. The product is built in R4/R5, high multiplier byte first:
; doubled, and the multiplicand added, for each bit.
;------------------------------------------------------------------------------
    .section .text.__mulhi3,"ax",@progbits
    .globl  __mulhi3
__mulhi3:
    clrf    R4
    clrf    R5
    PAGESEL(__pic_mulrow16)
    movf    R3, W
    call    __pic_mulrow16
    movf    R2, W
    call    __pic_mulrow16
    movf    R4, W
    movwf   R0
    movf    R5, W
    movwf   R1
    return

; A multiplier byte in W, with Z as MOVF left it.
    .section .text.__pic_mulrow16,"ax",@progbits
    .globl  __pic_mulrow16
__pic_mulrow16:
    btfsc   STATUS, Z
    bra     8f
    movwf   TMP0
    .rept 8
    lslf    R4, F
    rlf     R5, F
    lslf    TMP0, F
    btfss   STATUS, C
    bra     1f
    movf    R0, W
    addwf   R4, F
    movf    R1, W
    addwfc  R5, F
1:
    .endr
    return
8:  ; A zero byte: the product so far, times 256.
    movf    R4, W
    movwf   R5
    clrf    R4
    return

;------------------------------------------------------------------------------
; 32 x 32 -> 32. The product is built in FSR0L, FSR0H, TMP0, TMP1.
;------------------------------------------------------------------------------
    .section .text.__mulsi3,"ax",@progbits
    .globl  __mulsi3
__mulsi3:
    ; A zero byte of the multiplier (R4-R7) costs next to nothing and one of
    ; the multiplicand does not, so if only the multiplicand has its top
    ; byte zero, the two change places.
    movf    R7, W
    btfsc   STATUS, Z
    bra     2f
    movf    R3, W
    btfss   STATUS, Z
    bra     2f
    movf    R0, W
    xorwf   R4, W
    xorwf   R0, F
    xorwf   R4, F
    movf    R1, W
    xorwf   R5, W
    xorwf   R1, F
    xorwf   R5, F
    movf    R2, W
    xorwf   R6, W
    xorwf   R2, F
    xorwf   R6, F
    movf    R7, W
    movwf   R3
    clrf    R7
2:
    clrf    FSR0L
    clrf    FSR0H
    clrf    TMP0
    clrf    TMP1
    PAGESEL(__pic_mulrow32)
    movf    R7, W
    call    __pic_mulrow32
    movf    R6, W
    call    __pic_mulrow32
    movf    R5, W
    call    __pic_mulrow32
    movf    R4, W
    call    __pic_mulrow32
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

    .section .text.__pic_mulrow32,"ax",@progbits
    .globl  __pic_mulrow32
__pic_mulrow32:
    btfsc   STATUS, Z
    bra     8f
    movwf   FSR1L
    .rept 8
    lslf    FSR0L, F
    rlf     FSR0H, F
    rlf     TMP0, F
    rlf     TMP1, F
    lslf    FSR1L, F
    btfss   STATUS, C
    bra     1f
    movf    R0, W
    addwf   FSR0L, F
    movf    R1, W
    addwfc  FSR0H, F
    movf    R2, W
    addwfc  TMP0, F
    movf    R3, W
    addwfc  TMP1, F
1:
    .endr
    return
8:
    movf    TMP0, W
    movwf   TMP1
    movf    FSR0H, W
    movwf   TMP0
    movf    FSR0L, W
    movwf   FSR0H
    clrf    FSR0L
    return

;------------------------------------------------------------------------------
; 32 / 32: the remainder in R0-R3 (where the dividend was) and the quotient
; in FSR0L, FSR0H, TMP0, TMP1, as muldiv.S's routine leaves them. FSR1H is
; not touched (the signed divisions keep their sign there). Dividing by
; zero gives a quotient of zero and the dividend as the remainder.
;
; The dividend is shifted left out of R0-R3 into the remainder, and the
; quotient's bits come in at its bottom. A divisor of n bytes cannot go
; into the dividend's top n - 1 bytes, so those start as the remainder and
; their rounds are not run.
;------------------------------------------------------------------------------
    .section .text.__umodsi3,"ax",@progbits
    .globl  __umodsi3
__umodsi3:
    movf    R7, W
    iorwf   R6, W
    btfss   STATUS, Z
    bra     .Lfour
    movf    R5, W
    btfss   STATUS, Z
    bra     .Ltwo

    ; A divisor of one byte: a remainder of one byte in FSR0L.
    movf    R4, W
    btfsc   STATUS, Z
    bra     .Lzero
    clrf    FSR0L
    movlw   32
    movwf   FSR1L
.Lone_skip:
    ; A zero byte at the top of the dividend is eight rounds that do
    ; nothing.
    movf    R3, W
    btfss   STATUS, Z
    bra     .Lone
    movf    R2, W
    movwf   R3
    movf    R1, W
    movwf   R2
    movf    R0, W
    movwf   R1
    clrf    R0
    movlw   8
    subwf   FSR1L, F
    btfss   STATUS, Z
    bra     .Lone_skip
    bra     .Lone_done
.Lone:
    lslf    R0, F
    rlf     R1, F
    rlf     R2, F
    rlf     R3, F
    rlf     FSR0L, F
    btfsc   STATUS, C           ; a ninth bit: certainly not less
    bra     .Lone_sub
    movf    R4, W
    subwf   FSR0L, W
    btfss   STATUS, C
    bra     .Lone_next
    movwf   FSR0L
    bsf     R0, 0
    bra     .Lone_next
.Lone_sub:
    movf    R4, W
    subwf   FSR0L, F
    bsf     R0, 0
.Lone_next:
    decfsz  FSR1L, F
    bra     .Lone
.Lone_done:
    ; The quotient is in R0-R3 and the remainder in FSR0L: to their places.
    movf    FSR0L, W
    movwf   FSR1L
    movf    R0, W
    movwf   FSR0L
    movf    R1, W
    movwf   FSR0H
    movf    R2, W
    movwf   TMP0
    movf    R3, W
    movwf   TMP1
    movf    FSR1L, W
    movwf   R0
    clrf    R1
    clrf    R2
    clrf    R3
    return
.Lzero:
    clrf    FSR0L
    clrf    FSR0H
    clrf    TMP0
    clrf    TMP1
    return

    ; A divisor of two bytes: a remainder of two in FSR0L/FSR0H, which
    ; starts as the dividend's top byte; 24 rounds.
.Ltwo:
    movf    R3, W
    movwf   FSR0L
    clrf    FSR0H
    movf    R2, W
    movwf   R3
    movf    R1, W
    movwf   R2
    movf    R0, W
    movwf   R1
    clrf    R0
    movlw   24
    movwf   FSR1L
.Ltwo_round:
    lslf    R0, F
    rlf     R1, F
    rlf     R2, F
    rlf     R3, F
    rlf     FSR0L, F
    rlf     FSR0H, F
    btfsc   STATUS, C
    bra     .Ltwo_sub
    movf    R4, W
    subwf   FSR0L, W
    movwf   TMP0
    movf    R5, W
    subwfb  FSR0H, W
    btfss   STATUS, C
    bra     .Ltwo_next
    movwf   FSR0H
    movf    TMP0, W
    movwf   FSR0L
    bsf     R0, 0
    bra     .Ltwo_next
.Ltwo_sub:
    movf    R4, W
    subwf   FSR0L, F
    movf    R5, W
    subwfb  FSR0H, F
    bsf     R0, 0
.Ltwo_next:
    decfsz  FSR1L, F
    bra     .Ltwo_round
    ; The quotient is in R0-R2 and the remainder in FSR0L/FSR0H.
    movf    R2, W
    movwf   TMP0
    clrf    TMP1
    clrf    R2
    clrf    R3
    bra     .Lswap

    ; A larger divisor: a remainder of four bytes, which starts as the
    ; dividend's top two; 16 rounds.
.Lfour:
    movf    R2, W
    movwf   FSR0L
    movf    R3, W
    movwf   FSR0H
    clrf    TMP0
    clrf    TMP1
    movf    R1, W
    movwf   R3
    movf    R0, W
    movwf   R2
    clrf    R1
    clrf    R0
    movlw   16
    movwf   FSR1L
.Lfour_round:
    lslf    R0, F
    rlf     R1, F
    rlf     R2, F
    rlf     R3, F
    rlf     FSR0L, F
    rlf     FSR0H, F
    rlf     TMP0, F
    rlf     TMP1, F
    btfsc   STATUS, C           ; a 33rd bit: subtract, and that is that
    bra     .Lfour_sub
    movf    R4, W
    subwf   FSR0L, F
    movf    R5, W
    subwfb  FSR0H, F
    movf    R6, W
    subwfb  TMP0, F
    movf    R7, W
    subwfb  TMP1, F
    btfsc   STATUS, C
    bra     .Lfour_one
    movf    R4, W               ; it was less: put it back
    addwf   FSR0L, F
    movf    R5, W
    addwfc  FSR0H, F
    movf    R6, W
    addwfc  TMP0, F
    movf    R7, W
    addwfc  TMP1, F
    bra     .Lfour_next
.Lfour_sub:
    movf    R4, W
    subwf   FSR0L, F
    movf    R5, W
    subwfb  FSR0H, F
    movf    R6, W
    subwfb  TMP0, F
    movf    R7, W
    subwfb  TMP1, F
.Lfour_one:
    bsf     R0, 0
.Lfour_next:
    decfsz  FSR1L, F
    bra     .Lfour_round
    ; The quotient is in R0/R1 and the remainder in FSR0L, FSR0H, TMP0, TMP1.
    movf    TMP0, W
    movwf   R2
    movf    TMP1, W
    movwf   R3
    clrf    TMP0
    clrf    TMP1
.Lswap:
    ; R0 with FSR0L and R1 with FSR0H; the rest is in place.
    movf    R0, W
    xorwf   FSR0L, W
    xorwf   R0, F
    xorwf   FSR0L, F
    movf    R1, W
    xorwf   FSR0H, W
    xorwf   R1, F
    xorwf   FSR0H, F
    return

;------------------------------------------------------------------------------
; unsigned long __pic_fmul24(unsigned long a, unsigned long b)
;
; For float multiplication: a and b are significands of 24 bits. The
; result is their 48-bit product shifted right 20 places, with its lowest
; bit set if any of the 20 bits lost was. The product is built in FSR0L,
; FSR0H, TMP0, TMP1, R3, R7 (low byte first).
;------------------------------------------------------------------------------
    .section .text.__pic_fmul24,"ax",@progbits
    .globl  __pic_fmul24
__pic_fmul24:
    clrf    FSR0L
    clrf    FSR0H
    clrf    TMP0
    clrf    TMP1
    clrf    R3
    clrf    R7
    PAGESEL(__pic_fmulrow)
    movf    R6, W
    call    __pic_fmulrow
    movf    R5, W
    call    __pic_fmulrow
    movf    R4, W
    call    __pic_fmulrow
    ; Anything in the low 20 bits?
    movf    TMP0, W
    andlw   0x0F
    iorwf   FSR0H, W
    iorwf   FSR0L, W
    movwf   FSR1L
    ; Bytes 2 to 5 of the product, shifted right four more.
    movf    TMP0, W
    movwf   R0
    movf    TMP1, W
    movwf   R1
    movf    R3, W
    movwf   R2
    movf    R7, W
    movwf   R3
    .rept 4
    lsrf    R3, F
    rrf     R2, F
    rrf     R1, F
    rrf     R0, F
    .endr
    movf    FSR1L, W
    btfss   STATUS, Z
    bsf     R0, 0
    return

    .section .text.__pic_fmulrow,"ax",@progbits
    .globl  __pic_fmulrow
__pic_fmulrow:
    btfsc   STATUS, Z
    bra     8f
    movwf   FSR1L
    .rept 8
    lslf    FSR0L, F
    rlf     FSR0H, F
    rlf     TMP0, F
    rlf     TMP1, F
    rlf     R3, F
    rlf     R7, F
    lslf    FSR1L, F
    btfss   STATUS, C
    bra     1f
    movf    R0, W
    addwf   FSR0L, F
    movf    R1, W
    addwfc  FSR0H, F
    movf    R2, W
    addwfc  TMP0, F
    movlw   0
    addwfc  TMP1, F
    addwfc  R3, F
    addwfc  R7, F
1:
    .endr
    return
8:
    movf    R3, W
    movwf   R7
    movf    TMP1, W
    movwf   R3
    movf    TMP0, W
    movwf   TMP1
    movf    FSR0H, W
    movwf   TMP0
    movf    FSR0L, W
    movwf   FSR0H
    clrf    FSR0L
    return

;------------------------------------------------------------------------------
; unsigned long __pic_fdiv24(unsigned long a, unsigned long b)
;
; For float division: b is a significand of 24 bits with its top bit set,
; and a is less than twice b. The result is 26 bits of a / b (the first
; worth 1) and, below them, a bit that says whether anything is left over.
; The quotient is built in FSR0L, FSR0H, TMP0, TMP1.
;------------------------------------------------------------------------------
    .section .text.__pic_fdiv24,"ax",@progbits
    .globl  __pic_fdiv24
__pic_fdiv24:
    clrf    FSR0L
    clrf    FSR0H
    clrf    TMP0
    clrf    TMP1
    movlw   26
    movwf   FSR1L
.Ldiv_round:
    btfsc   R3, 0               ; a 25th bit: certainly not less
    bra     .Ldiv_sub
    movf    R4, W
    subwf   R0, W
    movwf   FSR1H
    movf    R5, W
    subwfb  R1, W
    movwf   R7
    movf    R6, W
    subwfb  R2, W
    btfss   STATUS, C
    bra     .Ldiv_bit           ; less: a 0, which the carry is
    movwf   R2
    movf    R7, W
    movwf   R1
    movf    FSR1H, W
    movwf   R0
    bra     .Ldiv_bit           ; (the carry is still set)
.Ldiv_sub:
    movf    R4, W
    subwf   R0, F
    movf    R5, W
    subwfb  R1, F
    movf    R6, W
    subwfb  R2, F
    clrf    R3
    bsf     STATUS, C
.Ldiv_bit:
    rlf     FSR0L, F
    rlf     FSR0H, F
    rlf     TMP0, F
    rlf     TMP1, F
    lslf    R0, F
    rlf     R1, F
    rlf     R2, F
    rlf     R3, F
    decfsz  FSR1L, F
    bra     .Ldiv_round
    lslf    FSR0L, F
    rlf     FSR0H, F
    rlf     TMP0, F
    rlf     TMP1, F
    movf    R0, W
    iorwf   R1, W
    iorwf   R2, W
    iorwf   R3, W
    btfss   STATUS, Z
    bsf     FSR0L, 0
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

; For the linker's choice between stack and static frames, and its count
; of hardware stack levels: what this file's code calls.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __mulhi3 -\nC __pic_mulrow16\n"
    .ascii  "F __mulsi3 -\nC __pic_mulrow32\nJ __pic_movsi\n"
    .ascii  "F __pic_fmul24 -\nC __pic_fmulrow\n"
    .ascii  "F __pic_fdiv24 -\nJ __pic_movsi\n"
