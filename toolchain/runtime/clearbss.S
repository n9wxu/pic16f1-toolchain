; Startup piece: clear .bss. Linked when a module refers to
; __do_clear_bss, which the compiler does for every module that defines
; zero-initialised data. See crt0.S.
;
; FSR1 (0x06/0x07) writes RAM. STATUS is 0x03, Z is bit 2.
    .section .init6,"ax",@progbits
    .globl  __do_clear_bss
__do_clear_bss:
    movlw   lo8(__bss_start)
    movwf   0x06
    movlw   hi8(__bss_start)
    movwf   0x07
    bra     .Ltest
.Lclear:
    movlw   0
    movwi   FSR1++
.Ltest:
    movf    0x06, W
    xorlw   lo8(__bss_end)
    btfss   0x03, 2
    bra     .Lclear
    movf    0x07, W
    xorlw   hi8(__bss_end)
    btfss   0x03, 2
    bra     .Lclear
