; arith32: 32-bit add, multiply, divide and shifts.
; See tests/programs/arith32/main.c.
;
; mulmod is written for what it is given: b is 7, so there are three rounds
; and each adds; a and r stay below m, so "% m" is one subtraction of m when
; the value has reached it. i * 100000 is carried from round to round. Every divisor fits in
; 15 bits, so the divide keeps a 16-bit remainder.
#include "bench.inc"
#include <picmath.inc>

#define N0  0x70            /* dividend, then quotient; other work */
#define N1  0x71
#define N2  0x72
#define N3  0x73
#define D0  0x74            /* divisor or multiplier */
#define D1  0x75
#define D2  0x76
#define D3  0x77
#define Q0  0x78            /* product; remainder (the upper half zero) */
#define Q1  0x79
#define Q2  0x7A
#define Q3  0x7B
#define CNT 0x7C
#define I   0x7D
#define SIGN 0x7E
#define T   0x7F

/* Bank 0: results, the first printed uppermost (see result16), then loop
 * variables. */
#define X   0x2C
#define DIV 0x28
#define PW  0x24
#define SUM 0x20
#define DD  0x30            /* d, 16 bits */
#define XI  0x32            /* i * 100000 */
#define J   0x36

    bench_start
    clrf    FSR1H
    clrf    FSR0H
    movlb   0
    movlw   SUM             ; results = 0
    movwf   FSR1L
    movlw   16
    movwf   CNT
    movlw   0
1:
    movwi   FSR1++
    decfsz  CNT, F
    bra     1b

    ; x = x * 1664525 + 1013904223, ten times from 1.
    incf    X, F
    movlw   10
    movwf   I
.Llcg:
    movlw   0x0D            ; 1664525 = 0x0019660D
    movwf   D0
    movlw   0x66
    movwf   D1
    movlw   0x19
    movwf   D2
    clrf    D3
    pic_mul32 X, X + 1, X + 2, X + 3,  D0, D1, D2, D3,  Q0, Q1, Q2, Q3
    movf    Q0, W           ; 1013904223 = 0x3C6EF35F
    addlw   0x5F
    movwf   X
    movlw   0xF3
    addwfc  Q1, W
    movwf   X + 1
    movlw   0x6E
    addwfc  Q2, W
    movwf   X + 2
    movlw   0x3C
    addwfc  Q3, W
    movwf   X + 3
    decfsz  I, F
    bra     .Llcg

    ; div += 4000000000 / d + 4000000000 % d for d = 3, 334, ... below 3000.
    movlw   3
    movwf   DD
    clrf    DD + 1
    clrf    Q2              ; the remainder's upper half
    clrf    Q3
2:
    clrf    N0              ; 4000000000 = 0xEE6B2800
    movlw   0x28
    movwf   N1
    movlw   0x6B
    movwf   N2
    movlw   0xEE
    movwf   N3
    movf    DD, W
    movwf   D0
    movf    DD + 1, W
    movwf   D1
    call    udivmod
    movlw   N0
    movwf   FSR0L
    movlw   DIV
    call    add32
    movlw   Q0
    movwf   FSR0L
    movlw   DIV
    call    add32
    movlw   lo8(331)
    addwf   DD, F
    movlw   hi8(331)
    addwfc  DD + 1, F
    movlw   lo8(3000)
    subwf   DD, W
    movlw   hi8(3000)
    subwfb  DD + 1, W
    btfss   STATUS, C
    bra     2b

    ; pw = pw * 7 mod 1000003, twenty times from 1. N is a, pw collects r.
    incf    PW, F
    movlw   20
    movwf   I
3:
    movf    PW, W
    movwf   N0
    movf    PW + 1, W
    movwf   N1
    movf    PW + 2, W
    movwf   N2
    movf    PW + 3, W
    movwf   N3
    clrf    PW
    clrf    PW + 1
    clrf    PW + 2
    clrf    PW + 3
    movlw   3
    movwf   J
4:
    movlw   N0              ; r = (r + a) % m
    movwf   FSR0L
    movlw   PW
    call    add32
    movlw   PW
    call    reduce
    lslf    N0, F           ; a = (a << 1) % m
    rlf     N1, F
    rlf     N2, F
    rlf     N3, F
    movlw   N0
    call    reduce
    decfsz  J, F
    bra     4b
    decfsz  I, F
    bra     3b

    ; s += i * 100000 / 7 + (-100000 >> (i & 15)) for -5 <= i < 5.
    movlw   0xE0            ; -500000 = 0xFFF85EE0
    movwf   XI
    movlw   0x5E
    movwf   XI + 1
    movlw   0xF8
    movwf   XI + 2
    movlw   0xFF
    movwf   XI + 3
    movlw   -5
    movwf   I
.Lsigned:
    movf    XI, W
    movwf   N0
    movf    XI + 1, W
    movwf   N1
    movf    XI + 2, W
    movwf   N2
    movf    XI + 3, W
    movwf   N3
    movwf   SIGN
    movlw   N0
    btfsc   SIGN, 7
    call    negate
    movlw   7
    movwf   D0
    clrf    D1
    call    udivmod
    movlw   N0
    btfsc   SIGN, 7
    call    negate
    movlw   N0
    movwf   FSR0L
    movlw   SUM
    call    add32
    movlw   0x60            ; -100000 = 0xFFFE7960
    movwf   N0
    movlw   0x79
    movwf   N1
    movlw   0xFE
    movwf   N2
    movlw   0xFF
    movwf   N3
    movf    I, W
    andlw   15
    movwf   CNT
    pic_asr32 N0, N1, N2, N3,  CNT
    movlw   N0
    movwf   FSR0L
    movlw   SUM
    call    add32
    movlw   0xA0            ; 100000 = 0x000186A0
    addwf   XI, F
    movlw   0x86
    addwfc  XI + 1, F
    movlw   0x01
    addwfc  XI + 2, F
    movlw   0
    addwfc  XI + 3, F
    incf    I, F
    movlw   5
    xorwf   I, W
    btfss   STATUS, Z
    bra     .Lsigned

    point_fsr0 names
    movlw   X + 4
    call    result32
    call    result32_next
    call    result32_next
    call    result32_next

    ; x == 0xA3A5A0E3 && pw == 0x00081B26
    movlb   0
    movlw   0xE3
    xorwf   X, F
    movlw   0xA0
    xorwf   X + 1, W
    iorwf   X, F
    movlw   0xA5
    xorwf   X + 2, W
    iorwf   X, F
    movlw   0xA3
    xorwf   X + 3, W
    iorwf   X, F
    movlw   0x26
    xorwf   PW, W
    iorwf   X, F
    movlw   0x1B
    xorwf   PW + 1, W
    iorwf   X, F
    movlw   0x08
    xorwf   PW + 2, W
    iorwf   PW + 3, W
    iorwf   X, F
    exit_code_from_z
    bench_exit

; The four bytes at address W += the four bytes at FSR0.
add32:
    movwf   FSR1L
    movlw   4
    movwf   T
    bcf     STATUS, C
1:
    moviw   FSR0++
    addwfc  INDF1, F
    addfsr  FSR1, 1
    decfsz  T, F
    bra     1b
    return

; The four bytes at address W, negated.
negate:
    movwf   FSR1L
    movlw   4
    movwf   T
    bsf     STATUS, C
1:
    comf    INDF1, F
    movlw   0
    addwfc  INDF1, F
    addfsr  FSR1, 1
    decfsz  T, F
    bra     1b
    return

; The value at address W, less 1000003 (0x0F4243) if it holds that much:
; subtract, and put it back on a borrow. The value is below twice 1000003,
; so only three bytes of it matter.
reduce:
    movwf   FSR1L
    movlw   0x43
    subwf   INDF1, F
    addfsr  FSR1, 1
    movlw   0x42
    subwfb  INDF1, F
    addfsr  FSR1, 1
    movlw   0x0F
    subwfb  INDF1, F
    btfsc   STATUS, C
    return
    addfsr  FSR1, -2
    movlw   0x43
    addwf   INDF1, F
    addfsr  FSR1, 1
    movlw   0x42
    addwfc  INDF1, F
    addfsr  FSR1, 1
    movlw   0x0F
    addwfc  INDF1, F
    return

; N = N / D and Q0:Q1 = N % D, unsigned, for a D below 0x8000.
udivmod:
    clrf    Q0
    clrf    Q1
    movlw   32
    movwf   CNT
1:
    lslf    N0, F           ; remainder:dividend one place left
    rlf     N1, F
    rlf     N2, F
    rlf     N3, F
    rlf     Q0, F
    rlf     Q1, F
    movf    D0, W           ; carry set if remainder >= divisor
    subwf   Q0, W
    movf    D1, W
    subwfb  Q1, W
    btfss   STATUS, C
    bra     2f
    movwf   Q1
    movf    D0, W
    subwf   Q0, F
    bsf     N0, 0
2:
    decfsz  CNT, F
    bra     1b
    return

    bench_io
    bench_result16 entry=0
    bench_result32

    .section .rodata,"a",@progbits
names:
    .asciz  "lcg="
    .asciz  "div="
    .asciz  "pow="
    .asciz  "signed="
