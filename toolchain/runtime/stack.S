; The software stack pointer.
;
; A function with a stack frame keeps its stack pointer in FSR1 while it
; runs, but between functions the stack pointer lives in the cell __sp:
; functions with static frames use FSR1 as an ordinary pointer register and
; leave nothing in it. So a function with a stack frame loads FSR1 from __sp
; on entry and after every call, and stores it there before its first call
; and before it returns.
;
; This file is linked only into a program that refers to __sp, which is one
; that has a function with a stack frame. The stack grows down from the end
; of RAM.

    .section .noinit.__sp,"aw",@nobits
    .globl  __sp
__sp:
    .space  2

; Part of the startup code (see crt0.S). FSR1 reaches the cell without a
; bank selection, which is encoded differently on 32- and 64-bank devices.
    .section .init5,"ax",@progbits
    movlw   lo8(__sp)
    movwf   0x06
    movlw   hi8(__sp)
    movwf   0x07
    movlw   lo8(__stack_top)
    movwi   0[FSR1]
    movlw   hi8(__stack_top)
    movwi   1[FSR1]

; For the linker's choice between stack and static frames: this file calls
; nothing.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __sp -\n"
