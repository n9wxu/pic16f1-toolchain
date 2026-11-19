; sort: insertion sort of 32 bytes and bubble sort of 16 words, then checks
; and checksums. See tests/programs/sort/main.c.
;
; The weighted sum, a8[i] * (i + 1) over all i, is taken from the top down:
; adding a running total of the elements so far counts each one once for
; every element at or below it, with no multiplication.
#include "bench.inc"

#define A8  0x20            /* 32 bytes, bank 0 */
#define A16 0x40            /* 16 words, bank 0 */
#define N8  32
#define N16 16

#define SUM 0x70            /* a result; also the multiply's product */
#define MIN 0x72            /* a result: a8[0] as a 16-bit value */
#define U0  0x74            /* a16[i - 1]; also the running total */
#define U1  0x75
#define T0  0x76            /* a16[i]; also the multiply's operands */
#define T1  0x77
#define SEED0 0x78
#define SEED1 0x79
#define K   0x7A
#define N   0x7B
#define KEY 0x7C
#define FLAG 0x7D           /* bit 0: swapped; bit 1: out of order */

    bench_start
    clrf    FSR1H

    ; a8[i] = seed = seed * 37 + 11, from 0x5A.
    movlw   A8
    movwf   FSR1L
    movlw   0x5A
    movwf   SEED0
    movlw   N8
    movwf   K
1:
    lslf    SEED0, W        ; 37 s = s + 4 s + 32 s
    movwf   T0
    lslf    T0, F
    movf    T0, W
    addwf   SEED0, F
    lslf    T0, F
    lslf    T0, F
    lslf    T0, W
    addwf   SEED0, W
    addlw   11
    movwf   SEED0
    movwi   FSR1++
    decfsz  K, F
    bra     1b

    ; a16[i] = seed = seed * 25173 + 13849, from 0xACE1. FSR1 is at a16.
    movlw   0xE1
    movwf   SEED0
    movlw   0xAC
    movwf   SEED1
    movlw   N16
    movwf   K
2:
    movlw   lo8(25173)
    movwf   T0
    movlw   hi8(25173)
    movwf   T1
    movlw   lo8(13849)      ; the product is added to the constant
    movwf   SUM
    movlw   hi8(13849)
    movwf   SUM + 1
3:
    lsrf    T1, F
    rrf     T0, F
    btfss   STATUS, C
    bra     4f
    movf    SEED0, W
    addwf   SUM, F
    movf    SEED1, W
    addwfc  SUM + 1, F
4:
    lslf    SEED0, F
    rlf     SEED1, F
    movf    T0, W
    iorwf   T1, W
    btfss   STATUS, Z
    bra     3b
    movf    SUM, W
    movwf   SEED0
    movwi   FSR1++
    movf    SUM + 1, W
    movwf   SEED1
    movwi   FSR1++
    decfsz  K, F
    bra     2b

    ; Insertion sort. FSR1 is at a8[j]; it starts at a8[i].
    movlw   A8 + 1
    movwf   N
5:
    movf    N, W
    movwf   FSR1L
    movf    INDF1, W
    movwf   KEY
6:
    movf    FSR1L, W        ; j > 0
    xorlw   A8
    btfsc   STATUS, Z
    bra     7f
    moviw   -1[FSR1]        ; a8[j - 1] > key
    subwf   KEY, W
    btfsc   STATUS, C
    bra     7f
    moviw   --FSR1
    movwi   1[FSR1]
    bra     6b
7:
    movf    KEY, W
    movwf   INDF1
    incf    N, F
    btfss   N, 6            ; a8 ends at 0x40
    bra     5b

    ; Bubble sort: passes over one element fewer each time, until a pass
    ; swaps nothing.
    movlw   N16 - 1
    movwf   N
8:
    bcf     FLAG, 0
    movlw   A16
    movwf   FSR1L
    movf    N, W
    movwf   K
    btfsc   STATUS, Z
    bra     .Lsorted
9:
    call    compare
    btfsc   STATUS, C
    bra     .Lno_swap
    movf    T0, W
    movwi   0[FSR1]
    movf    T1, W
    movwi   1[FSR1]
    movf    U0, W
    movwi   2[FSR1]
    movf    U1, W
    movwi   3[FSR1]
    bsf     FLAG, 0
.Lno_swap:
    addfsr  FSR1, 2
    decfsz  K, F
    bra     9b
    decf    N, F
    btfsc   FLAG, 0
    bra     8b
.Lsorted:

    ; ok = both arrays are in order
    bcf     FLAG, 1
    movlw   A8
    movwf   FSR1L
    movlw   N8 - 1
    movwf   K
.Lcheck8:
    moviw   FSR1++
    subwf   INDF1, W
    btfss   STATUS, C
    bsf     FLAG, 1
    decfsz  K, F
    bra     .Lcheck8
    incf    FSR1L, F        ; now at a16
    movlw   N16 - 1
    movwf   K
.Lcheck16:
    call    compare
    btfss   STATUS, C
    bsf     FLAG, 1
    addfsr  FSR1, 2
    decfsz  K, F
    bra     .Lcheck16

    ; sum8 = a8[0] * 1 + a8[1] * 2 + ...
    clrf    SUM
    clrf    SUM + 1
    clrf    U0
    clrf    U1
    movlw   A8 + N8
    movwf   FSR1L
    movlw   N8
    movwf   K
.Lsum8:
    moviw   --FSR1
    addwf   U0, F
    movlw   0
    addwfc  U1, F
    movf    U0, W
    addwf   SUM, F
    movf    U1, W
    addwfc  SUM + 1, F
    decfsz  K, F
    bra     .Lsum8
    point_fsr0 names
    movlw   SUM + 2
    call    result16

    ; mix16 = (a16[0] + 0) ^ (a16[1] + 1) ^ ...
    clrf    SUM
    clrf    SUM + 1
    movlw   A16
    movwf   FSR1L
    clrf    K
.Lmix16:
    moviw   FSR1++
    addwf   K, W
    xorwf   SUM, F
    moviw   FSR1++
    btfsc   STATUS, C
    addlw   1
    xorwf   SUM + 1, F
    incf    K, F
    btfss   K, 4
    bra     .Lmix16
    movlw   SUM + 2
    call    result16

    movlb   0
    movf    A8, W
    movwf   MIN
    clrf    MIN + 1
    movlw   MIN + 2
    call    result16
    movlw   A16 + 2 * N16
    call    result16

    movlw   0
    btfsc   FLAG, 1
    movlw   1
    bench_exit

; For the words at FSR1 (U) and FSR1 + 2 (T): carry clear if U > T.
compare:
    moviw   0[FSR1]
    movwf   U0
    moviw   1[FSR1]
    movwf   U1
    moviw   2[FSR1]
    movwf   T0
    moviw   3[FSR1]
    movwf   T1
    movf    U0, W
    subwf   T0, W
    movf    U1, W
    subwfb  T1, W
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "sum8="
    .asciz  "mix16="
    .asciz  "min8="
    .asciz  "max16="
