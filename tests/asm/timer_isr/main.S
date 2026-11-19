; timer_isr: see tests/programs/timer_isr/main.c.
;
; What must happen at run time is ten Timer0 overflow interrupts. What the
; program prints afterwards follows from that: ten ticks, three times ten
; in `shared`, and a main loop that has run at least once. So the handler
; only counts, the main loop only waits, and the text is fixed. runtime.S
; keeps the arithmetic.
#include "bench.inc"

#define LEFT   0x70         /* ticks still to come */

#define TMR0IF 2
#define TMR0IE 5
#define GIE    7

    .section .reset,"ax",@progbits
    .globl  _start
_start:
    goto    main

    .section .isr,"ax",@progbits
    bcf     INTCON, TMR0IF  ; the only interrupt enabled
    decf    LEFT, F
    retfie

    .text
main:
    bench_init
    movlw   10
    movwf   LEFT
    movlb   1
    movlw   0x80            ; Timer0 from Fosc/4, prescaler 1:2
    movwf   OPTION_REG
    movlb   0
    clrf    TMR0
    bcf     INTCON, TMR0IF
    bsf     INTCON, TMR0IE
    bsf     INTCON, GIE
1:
    movf    LEFT, F
    btfss   STATUS, Z
    bra     1b
    bcf     INTCON, GIE
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "ticks=000A\nshared=001E\nworked\n"
