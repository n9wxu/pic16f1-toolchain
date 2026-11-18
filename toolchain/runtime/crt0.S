; Startup code for the PIC16F1 LLVM toolchain.
;
; Word 0 holds a jump here. The interrupt handler, if the program has one,
; is placed at word 4 by the linker script (section .isr).
;
; Core register file addresses used below, fixed by the instruction set:
;   0x03 STATUS (bit 2 = Z)   0x04/0x05 FSR0L/H   0x06/0x07 FSR1L/H
; 0x70/0x71 are the compiler's R0/R1 byte registers in common RAM.

    .section .reset,"ax",@progbits
    movlp   page(_start)
    goto    _start

    .text
    .globl  _start
_start:
    ; Copy initialised data from flash to RAM. FSR0 reads flash through the
    ; 0x8000 data view; FSR1 writes RAM.
    movlw   lo8(__data_load_start)
    movwf   0x04
    movlw   hi8(__data_load_start)
    movwf   0x05
    movlw   lo8(__data_start)
    movwf   0x06
    movlw   hi8(__data_start)
    movwf   0x07
    movlw   lo8(__data_size)
    movwf   0x70
    movlw   hi8(__data_size)
    movwf   0x71
.Lcopy:
    movf    0x70, W
    iorwf   0x71, W
    btfsc   0x03, 2
    bra     .Lcopied
    moviw   FSR0++
    movwi   FSR1++
    movlw   1
    subwf   0x70, F
    movlw   0
    subwfb  0x71, F
    bra     .Lcopy
.Lcopied:

    ; Clear .bss.
    movlw   lo8(__bss_start)
    movwf   0x06
    movlw   hi8(__bss_start)
    movwf   0x07
    movlw   lo8(__bss_size)
    movwf   0x70
    movlw   hi8(__bss_size)
    movwf   0x71
.Lclear:
    movf    0x70, W
    iorwf   0x71, W
    btfsc   0x03, 2
    bra     .Lcleared
    movlw   0
    movwi   FSR1++
    movlw   1
    subwf   0x70, F
    movlw   0
    subwfb  0x71, F
    bra     .Lclear
.Lcleared:

    ; The data stack grows down from the end of RAM; FSR1 is its pointer.
    movlw   lo8(__stack_top)
    movwf   0x06
    movlw   hi8(__stack_top)
    movwf   0x07
    movlb   0

    movlp   page(main)
    call    main

    ; main's result is in R0:R1, which is also where exit takes its argument.
    movlp   page(exit)
    call    exit

    ; The default exit stops the processor. A program may define its own.
    .weak   exit
exit:
.Lstop:
    sleep
    bra     .Lstop
