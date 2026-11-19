; crc16: CRC-16/CCITT-FALSE, a bit at a time, of "123456789" and of a
; 64-byte buffer. See tests/programs/crc16/main.c.
#include "bench.inc"

#define CL  0x70            /* the CRC being computed */
#define CH  0x71
#define AL  0x72            /* the first result */
#define AH  0x73
#define N   0x74
#define I   0x75
#define BUF 0x20            /* 64 bytes in bank 0 */

    bench_start
    clrf    FSR1H
    point_fsr0 check
    movlw   9
    call    crc16
    movf    CL, W
    movwf   AL
    movf    CH, W
    movwf   AH

    movlw   BUF             ; buf[i] = i * 7 + 3
    movwf   FSR1L
    movlw   64
    movwf   N
    movlw   3
1:
    movwi   FSR1++
    addlw   7
    decfsz  N, F
    bra     1b

    movlw   BUF
    movwf   FSR0L
    clrf    FSR0H
    movlw   64
    call    crc16

    point_fsr0 names
    movlw   AL + 2
    call    result16
    call    result16_next      ; CRC is just below

    movlw   0xB1            ; a == 0x29B1
    xorwf   AL, F
    movlw   0x29
    xorwf   AH, W
    iorwf   AL, F
    exit_code_from_z
    bench_exit

; CH:CL = CRC of the W bytes at FSR0.
crc16:
    movwf   N
    movlw   0xFF
    movwf   CL
    movwf   CH
2:
    moviw   FSR0++
    xorwf   CH, F
    movlw   8
    movwf   I
3:
    lslf    CL, F
    rlf     CH, F
    btfss   STATUS, C
    bra     4f
    movlw   0x10
    xorwf   CH, F
    movlw   0x21
    xorwf   CL, F
4:
    decfsz  I, F
    bra     3b
    decfsz  N, F
    bra     2b
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
check:
    .ascii  "123456789"
names:
    .asciz  "check="
    .asciz  "buf="
