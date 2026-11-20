; Single instructions as functions, for languages whose compiler here
; cannot place an instruction in line (Rust and Zig are compiled for a
; stand-in target, whose inline assembly is not this processor's).
    .section .text.__pic16f1_sleep,"ax",@progbits
    .globl  __pic16f1_sleep
__pic16f1_sleep:
    sleep
    return

    .section .text.__pic16f1_nop,"ax",@progbits
    .globl  __pic16f1_nop
__pic16f1_nop:
    return

    .section .text.__pic16f1_clrwdt,"ax",@progbits
    .globl  __pic16f1_clrwdt
__pic16f1_clrwdt:
    clrwdt
    return

; These call nothing, and change no register.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __pic16f1_sleep -\nF __pic16f1_nop -\nF __pic16f1_clrwdt -\n"
