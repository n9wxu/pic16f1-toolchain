; Delays by counting instruction cycles: <xc.h>'s _delay, __delay_ms and
; __delay_us end here.
;
; void __delay_loops(unsigned long n)
;
; n arrives in R0-R3 (0x70-0x73), low byte first. The loop takes 10 cycles
; a turn and turns n + 1 times, the last one in 9 with the return; the
; macros in <xc.h> allow for that and for the call. Interrupts that occur
; meanwhile lengthen the delay.

#define R0   0x70
#define R1   0x71
#define R2   0x72
#define R3   0x73
#define STATUS 0x03

    .section .text.__delay_loops,"ax",@progbits
    .globl  __delay_loops
__delay_loops:
1:  movlw   1               ; 1 cycle
    subwf   R0, F           ; 1
    movlw   0               ; 1
    subwfb  R1, F           ; 1
    subwfb  R2, F           ; 1
    subwfb  R3, F           ; 1
    btfss   STATUS, 0       ; 2 while there was no borrow, 1 at the end
    return                  ; 2
    bra     1b              ; 2

; For the linker's choice between stack and static frames: this file calls
; nothing.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __delay_loops -\n"
