; matrix: a 4x4 matrix of 16-bit elements multiplied by a second, and the
; product by a scaled copy of itself. See tests/programs/matrix/main.c.
;
; a, b and c are 32 bytes each, one after another from the start of RAM in
; the view the FSRs have of it (0x2000). Both FSRs stay in that view until
; the results are printed, so a byte in FSRxL is a whole address.
#include "bench.inc"

#define MA  0x00            /* offsets from 0x2000 */
#define MB  0x20
#define MC  0x40

#define A0  0x70            /* multiplicand */
#define A1  0x71
#define B0  0x72            /* multiplier */
#define B1  0x73
#define S0  0x74            /* sum of products; also sum */
#define S1  0x75
#define X0  0x7C
#define X1  0x7D
#define T0  0x78
#define T1  0x79
#define IJ  0x7A            /* 8 * i + 2 * j: the offset of element [i][j] */
#define K   0x7B
#define TRACE 0x76          /* printed first: just above SUM */
#define SUM S0

    bench_start
    ; a[i][j] = init_a[i][j], sign-extended.
    point_fsr0 init_a
    movlw   0x20
    movwf   FSR1H
    clrf    FSR1L
    movlw   16
    movwf   K
1:
    moviw   FSR0++
    movwi   FSR1++
    andlw   0x80
    btfss   STATUS, Z
    movlw   0xFF
    movwi   FSR1++
    decfsz  K, F
    bra     1b

    ; b[i][j] = a[j][i] * 3 - i. FSR1 is at b already; FSR0 walks down
    ; column i of a.
    movlw   0x20
    movwf   FSR0H
    clrf    IJ              ; 2 * i
2:
    movf    IJ, W
    movwf   FSR0L
    movlw   4
    movwf   K
3:
    moviw   0[FSR0]
    movwf   X0
    moviw   1[FSR0]
    movwf   X1
    addfsr  FSR0, 8
    lslf    X0, W           ; T = 2 * x
    movwf   T0
    rlf     X1, W
    movwf   T1
    movf    T0, W           ; x = 3 * x
    addwf   X0, F
    movf    T1, W
    addwfc  X1, F
    lsrf    IJ, W           ; x -= i
    subwf   X0, W
    movwi   FSR1++
    movlw   0
    subwfb  X1, W
    movwi   FSR1++
    decfsz  K, F
    bra     3b
    incf    IJ, F
    incf    IJ, F
    btfss   IJ, 3
    bra     2b

    call    multiply

    ; a = c and b = c / 16, rounding toward zero.
    movlw   MC
    movwf   FSR0L
    movlw   MB
    movwf   FSR1L
    movlw   16
    movwf   K
4:
    moviw   FSR0++
    movwf   X0
    movwi   -32[FSR1]
    moviw   FSR0++
    movwf   X1
    movwi   -31[FSR1]
    btfss   X1, 7           ; a negative value needs 15 added first
    bra     5f
    movlw   15
    addwf   X0, F
    movlw   0
    addwfc  X1, F
5:
    asrf    X1, F
    rrf     X0, F
    asrf    X1, F
    rrf     X0, F
    asrf    X1, F
    rrf     X0, F
    asrf    X1, F
    rrf     X0, W
    movwi   FSR1++
    movf    X1, W
    movwi   FSR1++
    decfsz  K, F
    bra     4b

    call    multiply

    ; trace = c[0][0] + c[1][1] + ...; sum = sum * 31 + c[i][j] for all.
    clrf    TRACE
    clrf    TRACE + 1
    clrf    SUM
    clrf    SUM + 1
    movlw   MC
    movwf   FSR0L
    clrf    IJ              ; counts 0, 2, ... 30; the diagonal is every fifth
    movlw   1
    movwf   K
6:
    decfsz  K, F
    bra     7f
    movlw   5
    movwf   K
    moviw   0[FSR0]
    addwf   TRACE, F
    moviw   1[FSR0]
    addwfc  TRACE + 1, F
7:
    movf    SUM, W          ; T = sum * 32
    movwf   T0
    movf    SUM + 1, W
    movwf   T1
    movlw   5
    movwf   X0
8:
    lslf    T0, F
    rlf     T1, F
    decfsz  X0, F
    bra     8b
    movf    SUM, W          ; sum = T - sum + c[i][j]
    subwf   T0, W
    movwf   SUM
    movf    SUM + 1, W
    subwfb  T1, W
    movwf   SUM + 1
    moviw   FSR0++
    addwf   SUM, F
    moviw   FSR0++
    addwfc  SUM + 1, F
    incf    IJ, F
    incf    IJ, F
    btfss   IJ, 5
    bra     6b

    clrf    FSR1H
    point_fsr0 names
    movlw   TRACE + 2
    call    result16
    call    result16_next
    movlw   0
    bench_exit

; c = a * b.
multiply:
    clrf    IJ
1:
    clrf    S0
    clrf    S1
    movf    IJ, W           ; row i of a
    andlw   0x18
    movwf   FSR0L
    movf    IJ, W           ; column j of b
    andlw   0x06
    addlw   MB
    movwf   FSR1L
    movlw   4
    movwf   K
2:
    moviw   FSR0++
    movwf   A0
    moviw   FSR0++
    movwf   A1
    moviw   0[FSR1]
    movwf   B0
    moviw   1[FSR1]
    movwf   B1
    addfsr  FSR1, 8
3:
    lsrf    B1, F           ; S += A * B, by shifting and adding
    rrf     B0, F
    btfss   STATUS, C
    bra     4f
    movf    A0, W
    addwf   S0, F
    movf    A1, W
    addwfc  S1, F
4:
    lslf    A0, F
    rlf     A1, F
    movf    B0, W
    iorwf   B1, W
    btfss   STATUS, Z
    bra     3b
    decfsz  K, F
    bra     2b
    movf    IJ, W
    addlw   MC
    movwf   FSR0L
    movf    S0, W
    movwi   FSR0++
    movf    S1, W
    movwi   FSR0++
    incf    IJ, F
    incf    IJ, F
    btfss   IJ, 5
    bra     1b
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
init_a:
    .byte   1, -2, 3, 4,  5, 6, -7, 8,  -9, 10, 11, 12,  13, 14, 15, -16
names:
    .asciz  "trace="
    .asciz  "sum="
