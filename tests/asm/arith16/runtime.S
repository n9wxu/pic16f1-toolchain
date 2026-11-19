; arith16: 8- and 16-bit multiply, divide, modulo and shifts.
; See tests/programs/arith16/main.c.
;
; Where the C multiplies or shifts by the loop counter, the value is carried
; from one round to the next instead: i * i grows by the odd numbers,
; i * 37 by 37, x << i by one shift. The divisions are real divisions.
#include "bench.inc"
#include <picmath.inc>

#define A0  0x70            /* dividend, then quotient */
#define A1  0x71
#define B0  0x72            /* divisor */
#define B1  0x73
#define P0  0x74            /* remainder */
#define P1  0x75
#define CNT 0x76
#define SIGN 0x77
#define I   0x78
#define X0  0x79            /* values carried round the loops */
#define X1  0x7A
#define Y0  0x7B
#define Y1  0x7C
#define Z0  0x7D
#define Z1  0x7E

/* Results, in bank 0, the first printed uppermost; see result16. */
#define SQUARES 0x2A
#define MIX     0x28
#define SACC    0x26
#define SHIFTS  0x24
#define GCD     0x22
#define ISQRT   0x20

    bench_start
    clrf    FSR1H
    movlb   0
    movlw   ISQRT           ; results = 0
    movwf   FSR1L
    movlw   12
    movwf   CNT
    movlw   0
1:
    movwi   FSR1++
    decfsz  CNT, F
    bra     1b

    ; squares = sum of i * i for i < 100. X = i * i, Y0 = 2 * i + 1.
    clrf    X0
    clrf    X1
    clrf    Y0
    incf    Y0, F
    movlw   100
    movwf   I
2:
    movf    X0, W
    addwf   SQUARES, F
    movf    X1, W
    addwfc  SQUARES + 1, F
    movf    Y0, W
    addwf   X0, F
    movlw   0
    addwfc  X1, F
    incf    Y0, F
    incf    Y0, F
    decfsz  I, F
    bra     2b

    ; mix = sum of 50000 / i + 50000 % i for 1 <= i < 60.
    clrf    B1
    clrf    B0
3:
    incf    B0, F
    movlw   lo8(50000)
    movwf   A0
    movlw   hi8(50000)
    movwf   A1
    call    udivmod
    movf    A0, W
    addwf   MIX, F
    movf    A1, W
    addwfc  MIX + 1, F
    movf    P0, W
    addwf   MIX, F
    movf    P1, W
    addwfc  MIX + 1, F
    movlw   59
    xorwf   B0, W
    btfss   STATUS, Z
    bra     3b

    ; signed += i * 37 / 5 + i * 11 % 7 for -20 <= i < 20.
    ; X = i * 37 and Y = i * 11.
    movlw   lo8(-20 * 37)
    movwf   X0
    movlw   hi8(-20 * 37)
    movwf   X1
    movlw   lo8(-20 * 11)
    movwf   Y0
    movlw   hi8(-20 * 11)
    movwf   Y1
    movlw   40
    movwf   I
4:
    movf    X0, W
    movwf   A0
    movf    X1, W
    movwf   A1
    movlw   5
    call    sdivmod
    movf    A0, W
    addwf   SACC, F
    movf    A1, W
    addwfc  SACC + 1, F
    movf    Y0, W
    movwf   A0
    movf    Y1, W
    movwf   A1
    movlw   7
    call    sdivmod
    movf    P0, W
    addwf   SACC, F
    movf    P1, W
    addwfc  SACC + 1, F
    movlw   37
    addwf   X0, F
    movlw   0
    addwfc  X1, F
    movlw   11
    addwf   Y0, F
    movlw   0
    addwfc  Y1, F
    decfsz  I, F
    bra     4b

    ; shifts ^= (0xA5C3 << i) ^ (0xA5C3 >> i); shifts += -12345 >> (i & 7)
    ; for i < 16. X = 0xA5C3 << i, Y = 0xA5C3 >> i, Z = -12345 >> (i & 7).
    movlw   0xC3
    movwf   X0
    movwf   Y0
    movlw   0xA5
    movwf   X1
    movwf   Y1
    clrf    I
5:
    movlw   7
    andwf   I, W
    btfss   STATUS, Z
    bra     6f
    movlw   lo8(-12345)
    movwf   Z0
    movlw   hi8(-12345)
    movwf   Z1
6:
    movf    X0, W
    xorwf   Y0, W
    xorwf   SHIFTS, F
    movf    X1, W
    xorwf   Y1, W
    xorwf   SHIFTS + 1, F
    movf    Z0, W
    addwf   SHIFTS, F
    movf    Z1, W
    addwfc  SHIFTS + 1, F
    lslf    X0, F
    rlf     X1, F
    lsrf    Y1, F
    rrf     Y0, F
    asrf    Z1, F
    rrf     Z0, F
    incf    I, F
    btfss   I, 4
    bra     5b

    ; gcd(1071, 462)
    movlw   lo8(1071)
    movwf   A0
    movlw   hi8(1071)
    movwf   A1
    movlw   lo8(462)
    movwf   B0
    movlw   hi8(462)
    movwf   B1
7:
    call    udivmod         ; t = a % b
    movf    B0, W           ; a = b
    movwf   A0
    movf    B1, W
    movwf   A1
    movf    P0, W           ; b = t
    movwf   B0
    movf    P1, W
    movwf   B1
    iorwf   B0, W
    btfss   STATUS, Z
    bra     7b
    movf    A0, W
    movwf   GCD
    movf    A1, W
    movwf   GCD + 1

    ; isqrt(40000): r counts up while (r + 1) * (r + 1) <= n and r < 255.
    ; X = (r + 1) * (r + 1), Y = 2 * r + 3, the step to the next square.
    clrf    X1
    clrf    X0
    incf    X0, F
    clrf    Y1
    movlw   3
    movwf   Y0
8:
    movf    X0, W           ; 40000 - X: borrow means X is larger
    sublw   lo8(40000)
    movlw   hi8(40000)
    movwf   A0
    movf    X1, W
    subwfb  A0, W
    btfss   STATUS, C
    bra     9f
    incf    ISQRT, F
    movf    Y0, W
    addwf   X0, F
    movf    Y1, W
    addwfc  X1, F
    movlw   2
    addwf   Y0, F
    movlw   0
    addwfc  Y1, F
    incfsz  ISQRT, W        ; r < 255
    bra     8b
9:

    point_fsr0 names
    movlw   SQUARES + 2
    call    result16
    call    result16_next
    call    result16_next
    call    result16_next
    call    result16_next
    call    result16_next

    ; squares == 0x029E && gcd == 21 && isqrt == 200
    movlb   0
    movlw   0x9E
    xorwf   SQUARES, F
    movlw   0x02
    xorwf   SQUARES + 1, W
    iorwf   SQUARES, F
    movlw   21
    xorwf   GCD, W
    iorwf   GCD + 1, W
    iorwf   SQUARES, F
    movlw   200
    xorwf   ISQRT, W
    iorwf   ISQRT + 1, W
    iorwf   SQUARES, F
    exit_code_from_z
    bench_exit

; A = A / W and P = A % W, signed, for a positive W: both results have the
; sign of the dividend.
sdivmod:
    movwf   B0
    clrf    B1
    movf    A1, W
    movwf   SIGN
    btfsc   SIGN, 7
    call    negate_a
    call    udivmod
    btfss   SIGN, 7
    return
    pic_neg16 P0, P1
negate_a:
    pic_neg16 A0, A1
    return

; A = A / B and P = A % B, unsigned.
udivmod:
    pic_udivmod16 A0, A1,  B0, B1,  P0, P1,  CNT
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "squares="
    .asciz  "mix="
    .asciz  "signed="
    .asciz  "shifts="
    .asciz  "gcd="
    .asciz  "isqrt="
