; timer_isr: count ten Timer0 overflow interrupts while the main loop
; works. See tests/programs/timer_isr/main.c.
;
; The core saves and restores W, STATUS, BSR, the FSRs and PCLATH around an
; interrupt, and everything the handler touches is in common RAM, so the
; handler is only its own work.
#include "bench.inc"

/* Results, the first printed uppermost; see result16. */
#define TICKS  0x72
#define SHARED 0x70
#define WORK   0x74
#define T0  0x76
#define T1  0x77

#define TMR0IF 2
#define TMR0IE 5
#define GIE    7

    .section .reset,"ax",@progbits
    .globl  _start
_start:
    goto    main

    .section .isr,"ax",@progbits
    btfss   INTCON, TMR0IF
    retfie
    bcf     INTCON, TMR0IF
    incf    TICKS, F
    movlw   3
    addwf   SHARED, F
    movlw   0
    addwfc  SHARED + 1, F
    retfie

    .text
main:
    bench_init
    clrf    FSR1H
    clrf    TICKS
    clrf    TICKS + 1
    clrf    SHARED
    clrf    SHARED + 1
    clrf    WORK
    clrf    WORK + 1
    movlb   1
    movlw   0x80            ; Timer0 from Fosc/4, prescaler 1:2
    movwf   OPTION_REG
    movlb   0
    clrf    TMR0
    bcf     INTCON, TMR0IF
    bsf     INTCON, TMR0IE
    bsf     INTCON, GIE
1:
    movlw   10              ; while (ticks < 10) work = work * 5 + 1
    subwf   TICKS, W
    btfsc   STATUS, C
    bra     2f
    lslf    WORK, W
    movwf   T0
    rlf     WORK + 1, W
    movwf   T1
    lslf    T0, F
    rlf     T1, F
    incf    T0, W           ; T = 4 * work; work += T + 1
    btfsc   STATUS, Z
    incf    T1, F
    addwf   WORK, F
    movf    T1, W
    addwfc  WORK + 1, F
    bra     1b
2:
    bcf     INTCON, GIE

    point_fsr0 names
    movlw   TICKS + 2
    call    result16
    call    result16_next
    movf    WORK, W         ; "worked" follows the names, and "idle" that
    iorwf   WORK + 1, W
    btfsc   STATUS, Z
    addfsr  FSR0, 8
    call    puts

    movlw   10              ; ticks == 10 && shared == 30
    xorwf   TICKS, F
    movlw   30
    xorwf   SHARED, W
    iorwf   SHARED + 1, W
    iorwf   TICKS, F
    exit_code_from_z
    bench_exit

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "ticks="
    .asciz  "shared="
    .asciz  "worked\n"
    .asciz  "idle\n"
