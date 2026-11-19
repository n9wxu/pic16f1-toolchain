; fib: fib(24) in 16 bits and fib(40) in 32. See tests/programs/fib/main.c.
;
; One round of the C loop is t = a + b; a = b; b = t. Two rounds are
; a += b; b += a, with no copying, and both counts are even.
#include "bench.inc"

/* The results, the first printed uppermost; see result16. */
#define C0  0x70            /* fib32: a */
#define C1  0x71
#define C2  0x72
#define C3  0x73
#define A0  0x74            /* fib16: a */
#define A1  0x75
#define B0  0x76            /* fib16: b */
#define B1  0x77
#define D0  0x78            /* fib32: b */
#define D1  0x79
#define D2  0x7A
#define D3  0x7B
#define CNT 0x7C

    bench_start
    clrf    FSR1H
    movlw   C0              ; a = 0 and b = 0, twelve bytes
    movwf   FSR1L
    movlw   12
    movwf   CNT
    movlw   0
1:
    movwi   FSR1++
    decfsz  CNT, F
    bra     1b
    incf    B0, F           ; b = 1
    incf    D0, F

    movlw   24 / 2
    movwf   CNT
2:
    movf    B0, W
    addwf   A0, F
    movf    B1, W
    addwfc  A1, F
    movf    A0, W
    addwf   B0, F
    movf    A1, W
    addwfc  B1, F
    decfsz  CNT, F
    bra     2b

    movlw   40 / 2
    movwf   CNT
3:
    movf    D0, W
    addwf   C0, F
    movf    D1, W
    addwfc  C1, F
    movf    D2, W
    addwfc  C2, F
    movf    D3, W
    addwfc  C3, F
    movf    C0, W
    addwf   D0, F
    movf    C1, W
    addwfc  D1, F
    movf    C2, W
    addwfc  D2, F
    movf    C3, W
    addwfc  D3, F
    decfsz  CNT, F
    bra     3b

    point_fsr0 names
    movlw   A0 + 2
    call    result16
    call    result32_next

    ; 46368 = 0xB520 and 102334155 = 0x06197ECB
    movlw   0x20
    xorwf   A0, F
    movlw   0xB5
    xorwf   A1, W
    iorwf   A0, F
    movlw   0xCB
    xorwf   C0, W
    iorwf   A0, F
    movlw   0x7E
    xorwf   C1, W
    iorwf   A0, F
    movlw   0x19
    xorwf   C2, W
    iorwf   A0, F
    movlw   0x06
    xorwf   C3, W
    iorwf   A0, F
    exit_code_from_z
    bench_exit

    bench_io
    bench_result16
    bench_result32

    .section .rodata,"a",@progbits
names:
    .asciz  "fib24="
    .asciz  "fib40="
