; crc32: CRC-32 (reflected), a bit at a time, of "123456789".
; See tests/programs/crc32/main.c.
#include "bench.inc"

#define C0  0x70
#define C1  0x71
#define C2  0x72
#define C3  0x73
#define N   0x74
#define I   0x75

    bench_start
    clrf    FSR1H
    movlw   0xFF
    movwf   C0
    movwf   C1
    movwf   C2
    movwf   C3
    point_fsr0 check
    movlw   9
    movwf   N
1:
    moviw   FSR0++
    xorwf   C0, F
    movlw   8
    movwf   I
2:
    lsrf    C3, F
    rrf     C2, F
    rrf     C1, F
    rrf     C0, F
    btfss   STATUS, C
    bra     3f
    movlw   0xED
    xorwf   C3, F
    movlw   0xB8
    xorwf   C2, F
    movlw   0x83
    xorwf   C1, F
    movlw   0x20
    xorwf   C0, F
3:
    decfsz  I, F
    bra     2b
    decfsz  N, F
    bra     1b
    comf    C0, F
    comf    C1, F
    comf    C2, F
    comf    C3, F

    ; FSR0 is at the name: it follows the check string.
    movlw   C0 + 4
    call    result32

    movlw   0x26            ; 0xCBF43926
    xorwf   C0, F
    movlw   0x39
    xorwf   C1, W
    iorwf   C0, F
    movlw   0xF4
    xorwf   C2, W
    iorwf   C0, F
    movlw   0xCB
    xorwf   C3, W
    iorwf   C0, F
    exit_code_from_z
    bench_exit

    bench_io
    bench_result16 entry=0
    bench_result32

    .section .rodata,"a",@progbits
check:
    .ascii  "123456789"
    .asciz  "check="
