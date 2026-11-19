; bitops: see tests/programs/bitops/main.c.
;
; The counting loops have no input, so their results are known when the
; program is written. What is left for run time is what touches the
; hardware: the writes to the port latch, each of which shows on the pins,
; and the one read of the port, which decides the last digit and the exit
; code. runtime.S is the version that does the counting when it runs.
#include "bench.inc"

#define DIGIT 0x70

    bench_start
    movlb   1
    clrf    TRISB
    movlb   2
    clrf    LATB
    bsf     LATB, 0
    bsf     LATB, 7
    bsf     LATB, 3
    bcf     LATB, 0
    movlw   0x18
    xorwf   LATB, F         ; the latch now holds 0x90
    movlb   0
    movlw   '0'             ; latch |= 1 if the pin reads high
    btfsc   PORTB, 7
    movlw   '1'
    movwf   DIGIT

    point_fsr0 text
    call    puts
    movf    DIGIT, W
    call    putc
    movlw   10
    call    putc
    movf    DIGIT, W        ; exit code 0 if latch == 0x91
    xorlw   '1'
    bench_exit

    bench_io hex=0

    .section .rodata,"a",@progbits
text:
    .asciz  "pop=0400\nrev=FF40\npar=00AA\nlatch=009"
