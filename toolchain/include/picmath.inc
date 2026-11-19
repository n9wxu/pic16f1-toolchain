; Integer multiply, divide, negate and shift as assembler macros that work
; on whatever file registers they are given.
;
;     #include <picmath.inc>
;         pic_mul16     0x20, 0x21,  0x22, 0x23,  0x24, 0x25
;
; The runtime library (toolchain/runtime) is these same macros placed on
; the compiler's registers; an assembly source can place them on its own.
;
; Rules that hold for every macro:
;   - An operand is one file-register address per byte, low byte first.
;     The bytes of a value need not be next to each other.
;   - The registers given to one macro must all be different, and all
;     reachable with the bank that is selected when the code runs: in one
;     bank, in common RAM (0x70-0x7F), or core registers such as FSR0L.
;     The macros never change the bank.
;   - W and the STATUS flags are changed. Nothing else is touched but the
;     registers named.
;   - The code is placed in line where the macro is written, and falls out
;     of its end. Wrap it in a label and a return to make a subroutine.
;   - Numeric labels 1 to 3 and 7 to 9 are used inside.
;
; Multiplication is shift-and-add and stops when no bits of the multiplier
; are left. Division is restoring division, one quotient bit per round.
; Dividing by zero gives an all-ones quotient and the dividend as the
; remainder.

#ifndef PICMATH_INC
#define PICMATH_INC

#define PICMATH_STATUS 0x03
#define PICMATH_C      0
#define PICMATH_Z      2

;------------------------------------------------------------------------------
; 8 bits
;------------------------------------------------------------------------------

; p = a * b. a and b are destroyed.
.macro pic_mul8 a, b, p
    clrf    \p
1:
    lsrf    \b, F               ; next multiplier bit into carry
    btfss   PICMATH_STATUS, PICMATH_C
    bra     2f
    movf    \a, W
    addwf   \p, F
2:
    lslf    \a, F
    movf    \b, F               ; any multiplier bits left?
    btfss   PICMATH_STATUS, PICMATH_Z
    bra     1b
.endm

; n = n / d, r = n % d, unsigned. d is kept. cnt is a scratch byte.
.macro pic_udivmod8 n, d, r, cnt
    clrf    \r
    movlw   8
    movwf   \cnt
1:
    lslf    \n, F               ; remainder:dividend one place left
    rlf     \r, F
    btfsc   PICMATH_STATUS, PICMATH_C   ; a ninth bit: larger than any divisor
    bra     2f
    movf    \d, W
    subwf   \r, W               ; carry set if remainder >= divisor
    btfss   PICMATH_STATUS, PICMATH_C
    bra     3f
2:
    movf    \d, W
    subwf   \r, F
    bsf     \n, 0
3:
    decfsz  \cnt, F
    bra     1b
.endm

; x = -x
.macro pic_neg8 x
    comf    \x, F
    incf    \x, F
.endm

; x = |x|
.macro pic_abs8 x
    movf    \x, W
    btfsc   \x, 7
    sublw   0
    movwf   \x
.endm

; n = n / d, r = n % d, signed: the quotient rounds toward zero and the
; remainder has the sign of the dividend. d is left as its absolute value.
; cnt and sign are scratch bytes.
.macro pic_divmod8 n, d, r, cnt, sign
    clrf    \sign               ; bit 7: dividend negative; bit 0: quotient negative
    movlw   0x81
    btfsc   \n, 7
    movwf   \sign
    movlw   0x01
    btfsc   \d, 7
    xorwf   \sign, F
    pic_abs8 \n
    pic_abs8 \d
    pic_udivmod8 \n, \d, \r, \cnt
    btfss   \sign, 0
    bra     8f
    pic_neg8 \n
8:
    btfss   \sign, 7
    bra     9f
    pic_neg8 \r
9:
.endm

;------------------------------------------------------------------------------
; 16 bits
;------------------------------------------------------------------------------

; p = a * b. a and b are destroyed.
.macro pic_mul16 a0, a1, b0, b1, p0, p1
    clrf    \p0
    clrf    \p1
1:
    lsrf    \b1, F              ; next multiplier bit into carry
    rrf     \b0, F
    btfss   PICMATH_STATUS, PICMATH_C
    bra     2f
    movf    \a0, W
    addwf   \p0, F
    movf    \a1, W
    addwfc  \p1, F
2:
    lslf    \a0, F
    rlf     \a1, F
    movf    \b0, W              ; any multiplier bits left?
    iorwf   \b1, W
    btfss   PICMATH_STATUS, PICMATH_Z
    bra     1b
.endm

; n = n / d, r = n % d, unsigned. d is kept. cnt is a scratch byte.
.macro pic_udivmod16 n0, n1, d0, d1, r0, r1, cnt
    clrf    \r0
    clrf    \r1
    movlw   16
    movwf   \cnt
1:
    lslf    \n0, F              ; remainder:dividend one place left
    rlf     \n1, F
    rlf     \r0, F
    rlf     \r1, F
    btfsc   PICMATH_STATUS, PICMATH_C   ; a seventeenth bit: larger than any divisor
    bra     2f
    movf    \d0, W
    subwf   \r0, W
    movf    \d1, W
    subwfb  \r1, W              ; carry set if remainder >= divisor
    btfss   PICMATH_STATUS, PICMATH_C
    bra     3f
2:
    movf    \d0, W
    subwf   \r0, F
    movf    \d1, W
    subwfb  \r1, F
    bsf     \n0, 0
3:
    decfsz  \cnt, F
    bra     1b
.endm

; x = -x
.macro pic_neg16 x0, x1
    comf    \x0, F
    comf    \x1, F
    incf    \x0, F
    btfsc   PICMATH_STATUS, PICMATH_Z
    incf    \x1, F
.endm

; x = |x|
.macro pic_abs16 x0, x1
    btfss   \x1, 7
    bra     7f
    pic_neg16 \x0, \x1
7:
.endm

; n = n / d, r = n % d, signed: the quotient rounds toward zero and the
; remainder has the sign of the dividend. d is left as its absolute value.
; cnt and sign are scratch bytes.
.macro pic_divmod16 n0, n1, d0, d1, r0, r1, cnt, sign
    clrf    \sign               ; bit 7: dividend negative; bit 0: quotient negative
    movlw   0x81
    btfsc   \n1, 7
    movwf   \sign
    movlw   0x01
    btfsc   \d1, 7
    xorwf   \sign, F
    pic_abs16 \n0, \n1
    pic_abs16 \d0, \d1
    pic_udivmod16 \n0, \n1, \d0, \d1, \r0, \r1, \cnt
    btfss   \sign, 0
    bra     8f
    pic_neg16 \n0, \n1
8:
    btfss   \sign, 7
    bra     9f
    pic_neg16 \r0, \r1
9:
.endm

;------------------------------------------------------------------------------
; 32 bits
;------------------------------------------------------------------------------

; p = a * b. a and b are destroyed.
.macro pic_mul32 a0, a1, a2, a3, b0, b1, b2, b3, p0, p1, p2, p3
    clrf    \p0
    clrf    \p1
    clrf    \p2
    clrf    \p3
1:
    lsrf    \b3, F              ; next multiplier bit into carry
    rrf     \b2, F
    rrf     \b1, F
    rrf     \b0, F
    btfss   PICMATH_STATUS, PICMATH_C
    bra     2f
    movf    \a0, W
    addwf   \p0, F
    movf    \a1, W
    addwfc  \p1, F
    movf    \a2, W
    addwfc  \p2, F
    movf    \a3, W
    addwfc  \p3, F
2:
    lslf    \a0, F
    rlf     \a1, F
    rlf     \a2, F
    rlf     \a3, F
    movf    \b0, W              ; any multiplier bits left?
    iorwf   \b1, W
    iorwf   \b2, W
    iorwf   \b3, W
    btfss   PICMATH_STATUS, PICMATH_Z
    bra     1b
.endm

; n = n / d, r = n % d, unsigned. d is kept. cnt is a scratch byte.
.macro pic_udivmod32 n0, n1, n2, n3, d0, d1, d2, d3, r0, r1, r2, r3, cnt
    clrf    \r0
    clrf    \r1
    clrf    \r2
    clrf    \r3
    movlw   32
    movwf   \cnt
1:
    lslf    \n0, F              ; remainder:dividend one place left
    rlf     \n1, F
    rlf     \n2, F
    rlf     \n3, F
    rlf     \r0, F
    rlf     \r1, F
    rlf     \r2, F
    rlf     \r3, F
    btfsc   PICMATH_STATUS, PICMATH_C   ; a thirty-third bit: larger than any divisor
    bra     2f
    movf    \d0, W
    subwf   \r0, W
    movf    \d1, W
    subwfb  \r1, W
    movf    \d2, W
    subwfb  \r2, W
    movf    \d3, W
    subwfb  \r3, W              ; carry set if remainder >= divisor
    btfss   PICMATH_STATUS, PICMATH_C
    bra     3f
2:
    movf    \d0, W
    subwf   \r0, F
    movf    \d1, W
    subwfb  \r1, F
    movf    \d2, W
    subwfb  \r2, F
    movf    \d3, W
    subwfb  \r3, F
    bsf     \n0, 0
3:
    decfsz  \cnt, F
    bra     1b
.endm

; x = -x
.macro pic_neg32 x0, x1, x2, x3
    comf    \x0, F
    comf    \x1, F
    comf    \x2, F
    comf    \x3, F
    movlw   1
    addwf   \x0, F
    movlw   0
    addwfc  \x1, F
    addwfc  \x2, F
    addwfc  \x3, F
.endm

; x = |x|
.macro pic_abs32 x0, x1, x2, x3
    btfss   \x3, 7
    bra     7f
    pic_neg32 \x0, \x1, \x2, \x3
7:
.endm

; n = n / d, r = n % d, signed: the quotient rounds toward zero and the
; remainder has the sign of the dividend. d is left as its absolute value.
; cnt and sign are scratch bytes.
.macro pic_divmod32 n0, n1, n2, n3, d0, d1, d2, d3, r0, r1, r2, r3, cnt, sign
    clrf    \sign               ; bit 7: dividend negative; bit 0: quotient negative
    movlw   0x81
    btfsc   \n3, 7
    movwf   \sign
    movlw   0x01
    btfsc   \d3, 7
    xorwf   \sign, F
    pic_abs32 \n0, \n1, \n2, \n3
    pic_abs32 \d0, \d1, \d2, \d3
    pic_udivmod32 \n0, \n1, \n2, \n3, \d0, \d1, \d2, \d3, \r0, \r1, \r2, \r3, \cnt
    btfss   \sign, 0
    bra     8f
    pic_neg32 \n0, \n1, \n2, \n3
8:
    btfss   \sign, 7
    bra     9f
    pic_neg32 \r0, \r1, \r2, \r3
9:
.endm

;------------------------------------------------------------------------------
; Shifts by a run-time count. cnt holds the number of places and is left at
; zero; a count of zero shifts nothing. A count of the value's width or more
; shifts everything out (zero, or all sign bits for pic_asr).
;------------------------------------------------------------------------------

; x <<= cnt
.macro pic_shl32 x0, x1, x2, x3, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    lslf    \x0, F
    rlf     \x1, F
    rlf     \x2, F
    rlf     \x3, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

; x >>= cnt, unsigned
.macro pic_lsr32 x0, x1, x2, x3, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    lsrf    \x3, F
    rrf     \x2, F
    rrf     \x1, F
    rrf     \x0, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

; x >>= cnt, signed
.macro pic_asr32 x0, x1, x2, x3, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    asrf    \x3, F
    rrf     \x2, F
    rrf     \x1, F
    rrf     \x0, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

; x <<= cnt
.macro pic_shl64 x0, x1, x2, x3, x4, x5, x6, x7, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    lslf    \x0, F
    rlf     \x1, F
    rlf     \x2, F
    rlf     \x3, F
    rlf     \x4, F
    rlf     \x5, F
    rlf     \x6, F
    rlf     \x7, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

; x >>= cnt, unsigned
.macro pic_lsr64 x0, x1, x2, x3, x4, x5, x6, x7, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    lsrf    \x7, F
    rrf     \x6, F
    rrf     \x5, F
    rrf     \x4, F
    rrf     \x3, F
    rrf     \x2, F
    rrf     \x1, F
    rrf     \x0, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

; x >>= cnt, signed
.macro pic_asr64 x0, x1, x2, x3, x4, x5, x6, x7, cnt
    movf    \cnt, W
    btfsc   PICMATH_STATUS, PICMATH_Z
    bra     2f
1:
    asrf    \x7, F
    rrf     \x6, F
    rrf     \x5, F
    rrf     \x4, F
    rrf     \x3, F
    rrf     \x2, F
    rrf     \x1, F
    rrf     \x0, F
    decfsz  \cnt, F
    bra     1b
2:
.endm

#endif
