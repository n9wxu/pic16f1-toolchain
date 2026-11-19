; bitops: bit counting, reversal and parity over 0..255, then single-bit
; operations on a port latch. See tests/programs/bitops/main.c.
#include "bench.inc"

/* Results, the first printed uppermost; see result16. */
#define POP   0x76
#define REV   0x74
#define PAR   0x72
#define LATCH 0x70
#define I   0x78
#define V   0x79
#define R   0x7A
#define K   0x7B

    bench_start
    clrf    FSR1H
    movlw   LATCH           ; results = 0
    movwf   FSR1L
    movlw   8
    movwf   K
    movlw   0
1:
    movwi   FSR1++
    decfsz  K, F
    bra     1b

    clrf    I
2:
    ; pop += popcount8(i): clear the lowest set bit until none is left.
    movf    I, W
    movwf   V
    btfsc   STATUS, Z
    bra     4f
3:
    incf    POP, F
    btfsc   STATUS, Z
    incf    POP + 1, F
    decf    V, W
    andwf   V, F
    btfss   STATUS, Z
    bra     3b
4:
    ; rev += reverse8(i) * (i & 3): the bits out of one end into the other,
    ; and the product by adding.
    movf    I, W
    movwf   V
    movlw   8
    movwf   K
5:
    lsrf    V, F
    rlf     R, F
    decfsz  K, F
    bra     5b
    movf    I, W
    andlw   3
    btfsc   STATUS, Z
    bra     7f
    movwf   K
6:
    movf    R, W
    addwf   REV, F
    movlw   0
    addwfc  REV + 1, F
    decfsz  K, F
    bra     6b
7:
    ; par += parity16(i * 257 + 1). The value is i in both bytes, plus one:
    ; its low byte is i + 1 and its high byte i, or 0 when that carried.
    incf    I, W
    movwf   V
    movf    I, W
    btfsc   STATUS, Z       ; W is still i; Z is from the increment
    movlw   0
    xorwf   V, F            ; fold the bytes,
    swapf   V, W            ; the nibbles,
    xorwf   V, F
    lsrf    V, W            ; the pairs,
    xorwf   V, F
    btfsc   V, 2            ; and the last two bits
    incf    V, F
    btfss   V, 0
    bra     8f
    incf    PAR, F
    btfsc   STATUS, Z
    incf    PAR + 1, F
8:
    incfsz  I, F
    bra     2b

    movlb   1
    clrf    TRISB
    movlb   2
    clrf    LATB
    bsf     LATB, 0
    bsf     LATB, 7
    bsf     LATB, 3
    bcf     LATB, 0
    movlw   0x18
    xorwf   LATB, F
    movf    LATB, W
    movwf   LATCH
    movlb   0
    btfsc   PORTB, 7
    bsf     LATCH, 0

    point_fsr0 names
    movlw   POP + 2
    call    result16
    call    result16_next
    call    result16_next
    call    result16_next

    ; pop == 1024 && latch == 0x91
    movlw   0x04
    xorwf   POP + 1, W
    iorwf   POP, F
    movlw   0x91
    xorwf   LATCH, W
    iorwf   POP, F
    exit_code_from_z
    bench_exit

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "pop="
    .asciz  "rev="
    .asciz  "par="
    .asciz  "latch="
