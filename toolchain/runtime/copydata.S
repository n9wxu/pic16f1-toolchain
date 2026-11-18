; Startup piece: copy initialised data from flash to RAM. Linked when a
; module refers to __do_copy_data, which the compiler does for every module
; that defines initialised data. See crt0.S.
;
; FSR0 (0x04/0x05) reads flash through the 0x8000 data view; FSR1
; (0x06/0x07) writes RAM. STATUS is 0x03, Z is bit 2.
    .section .init4,"ax",@progbits
    .globl  __do_copy_data
__do_copy_data:
    movlw   lo8(__data_load_start)
    movwf   0x04
    movlw   hi8(__data_load_start)
    movwf   0x05
    movlw   lo8(__data_start)
    movwf   0x06
    movlw   hi8(__data_start)
    movwf   0x07
    bra     .Ltest
.Lcopy:
    moviw   FSR0++
    movwi   FSR1++
.Ltest:
    movf    0x06, W
    xorlw   lo8(__data_end)
    btfss   0x03, 2
    bra     .Lcopy
    movf    0x07, W
    xorlw   hi8(__data_end)
    btfss   0x03, 2
    bra     .Lcopy
