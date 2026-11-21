; Integer multiply and divide for 8, 16 and 32 bits: the routines the
; compiler calls for operations the core has no instruction for. (The 64-bit
; ones are in muldiv64.c.)
;
; Calling convention: the first operand is in R0.. (0x70 up, low byte
; first), the second follows it, and the result comes back in R0... So
;    8 bits:  0x70 op 0x71            -> 0x70
;   16 bits:  0x70/71 op 0x72/73      -> 0x70/71
;   32 bits:  0x70-73 op 0x74-77      -> 0x70-73
; R0-R7 (0x70-0x77), TMP0/TMP1 (0x7E/0x7F), both FSRs (0x04-0x07), W and
; the flags are free for a callee to change; anything else is preserved.
; None of these routines uses the stack: a program whose functions all have
; static frames has none.
; STATUS is 0x03: bit 0 is carry, bit 2 is zero.
;
; Multiplication is shift-and-add and stops when no bits of the multiplier
; are left. Division is restoring division, one quotient bit per round; in
; the 8- and 16-bit routines the quotient replaces the dividend bit by bit,
; and dividing by zero gives an all-ones quotient.
;
; Every routine is in a section of its own, so that a program gets only the
; ones it calls.
;
; The algorithms themselves are the macros of <picmath.inc>, which work on
; any registers; this file places them on the registers of the calling
; convention. An assembly source that wants its operands elsewhere uses the
; macros directly.

#include <picmath.inc>

#define R0   0x70
#define R1   0x71
#define R2   0x72
#define R3   0x73
#define R4   0x74
#define R5   0x75
#define R6   0x76
#define R7   0x77
#define R8   0x78
#define TMP0 0x7E
#define TMP1 0x7F
#define FSR0L 0x04
#define FSR0H 0x05
#define FSR1L 0x06
#define FSR1H 0x07

; Built twice, like crt0.S: with -DONE_PAGE for programs within one page,
; where PCLATH is never set.
#ifdef ONE_PAGE
#define PAGESEL(target)
#else
#define PAGESEL(target) movlp page(target)
#endif

;------------------------------------------------------------------------------
; 8 bits
;------------------------------------------------------------------------------

    .section .text.__mulqi3,"ax",@progbits
    .globl  __mulqi3
__mulqi3:
    pic_mul8 R0, R1, R2
    movf    R2, W
    movwf   R0
    return

; Quotient in 0x70, remainder in 0x72. The compiler relies on the
; remainder being there when it wants both.
    .section .text.__udivqi3,"ax",@progbits
    .globl  __udivqi3
__udivqi3:
    pic_udivmod8 R0, R1, R2, TMP0
    return

    .section .text.__umodqi3,"ax",@progbits
    .globl  __umodqi3
__umodqi3:
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    R2, W
    movwf   R0
    return

; Both operands to their absolute values.
    .section .text.__pic_absqi2,"ax",@progbits
    .globl  __pic_absqi2
__pic_absqi2:
    pic_abs8 R0
    pic_abs8 R1
    return

; The quotient is negative when the signs differ. TMP1 keeps the sign;
; the unsigned routine does not use it.
    .section .text.__divqi3,"ax",@progbits
    .globl  __divqi3
__divqi3:
    movf    R0, W
    xorwf   R1, W
    movwf   TMP1
    PAGESEL(__pic_absqi2)
    call    __pic_absqi2
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    R0, W
    btfsc   TMP1, 7
    sublw   0
    movwf   R0
    return

; The remainder has the sign of the dividend.
    .section .text.__modqi3,"ax",@progbits
    .globl  __modqi3
__modqi3:
    movf    R0, W
    movwf   TMP1
    PAGESEL(__pic_absqi2)
    call    __pic_absqi2
    PAGESEL(__udivqi3)
    call    __udivqi3
    movf    R2, W
    btfsc   TMP1, 7
    sublw   0
    movwf   R0
    return

;------------------------------------------------------------------------------
; 16 bits
;------------------------------------------------------------------------------

; (With -DFAST_MATH, for libpicfast.a, this and the two 32-bit routines
; marked the same way are left out: fastmath.S has them.)
#ifndef FAST_MATH
    .section .text.__mulhi3,"ax",@progbits
    .globl  __mulhi3
__mulhi3:
    pic_mul16 R0, R1,  R2, R3,  R4, R5
    movf    R4, W
    movwf   R0
    movf    R5, W
    movwf   R1
    return
#endif

; Quotient in 0x70/71, remainder in 0x74/75. The compiler relies on the
; remainder being there when it wants both.
    .section .text.__udivhi3,"ax",@progbits
    .globl  __udivhi3
__udivhi3:
    pic_udivmod16 R0, R1,  R2, R3,  R4, R5,  TMP0
    return

    .section .text.__umodhi3,"ax",@progbits
    .globl  __umodhi3
__umodhi3:
    PAGESEL(__udivhi3)
    call    __udivhi3
    movf    R4, W
    movwf   R0
    movf    R5, W
    movwf   R1
    return

; Both operands to their absolute values; __pic_neghi negates the first.
    .section .text.__pic_abshi2,"ax",@progbits
    .globl  __pic_abshi2
    .globl  __pic_neghi
__pic_abshi2:
    pic_abs16 R2, R3
    btfss   R1, 7
    return
__pic_neghi:
    pic_neg16 R0, R1
    return

    .section .text.__divhi3,"ax",@progbits
    .globl  __divhi3
__divhi3:
    movf    R1, W
    xorwf   R3, W
    movwf   TMP1
    PAGESEL(__pic_abshi2)
    call    __pic_abshi2
    PAGESEL(__udivhi3)
    call    __udivhi3
    btfss   TMP1, 7
    return
    PAGESEL(__pic_neghi)
    goto    __pic_neghi

    .section .text.__modhi3,"ax",@progbits
    .globl  __modhi3
__modhi3:
    movf    R1, W
    movwf   TMP1
    PAGESEL(__pic_abshi2)
    call    __pic_abshi2
    PAGESEL(__umodhi3)
    call    __umodhi3
    btfss   TMP1, 7
    return
    PAGESEL(__pic_neghi)
    goto    __pic_neghi

;------------------------------------------------------------------------------
; 32 bits. Four more bytes are needed than R0-R7 give: FSR0 and TMP0/TMP1.
;------------------------------------------------------------------------------

#ifndef FAST_MATH
    .section .text.__mulsi3,"ax",@progbits
    .globl  __mulsi3
__mulsi3:
    ; product in FSR0L, FSR0H, TMP0, TMP1
    pic_mul32 R0, R1, R2, R3,  R4, R5, R6, R7,  FSR0L, FSR0H, TMP0, TMP1
    PAGESEL(__pic_movsi)
    goto    __pic_movsi
#endif

; R0-R3 = FSR0L, FSR0H, TMP0, TMP1: where the product and the quotient
; are built.
    .section .text.__pic_movsi,"ax",@progbits
    .globl  __pic_movsi
__pic_movsi:
    movf    FSR0L, W
    movwf   R0
    movf    FSR0H, W
    movwf   R1
    movf    TMP0, W
    movwf   R2
    movf    TMP1, W
    movwf   R3
    return

; The 32-bit divide is the one that lines the divisor up first, so that a
; large divisor takes few rounds. It leaves the remainder where the
; dividend was (R0-R3), which makes it the remainder routine as it stands,
; and the quotient in FSR0L, FSR0H, TMP0, TMP1. The round counter is FSR1L:
; FSR1 is free here like FSR0. Dividing by zero gives a quotient of zero and
; the dividend as the remainder.
#ifndef FAST_MATH
    .section .text.__umodsi3,"ax",@progbits
    .globl  __umodsi3
__umodsi3:
    pic_udivmod32_norm R0, R1, R2, R3,  R4, R5, R6, R7,  FSR0L, FSR0H, TMP0, TMP1,  FSR1L
    return
#endif

    .section .text.__udivsi3,"ax",@progbits
    .globl  __udivsi3
__udivsi3:
    PAGESEL(__umodsi3)
    call    __umodsi3
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

; Quotient in 0x70-73 and remainder in 0x74-77: what the compiler calls
; when it wants both.
    .section .text.__udivmodsi4,"ax",@progbits
    .globl  __udivmodsi4
__udivmodsi4:
    PAGESEL(__umodsi3)
    call    __umodsi3
    movf    R0, W
    movwf   R4
    movf    R1, W
    movwf   R5
    movf    R2, W
    movwf   R6
    movf    R3, W
    movwf   R7
    PAGESEL(__pic_movsi)
    goto    __pic_movsi

; Both operands to their absolute values; __pic_negsi negates the first.
    .section .text.__pic_abssi2,"ax",@progbits
    .globl  __pic_abssi2
    .globl  __pic_negsi
__pic_abssi2:
    pic_abs32 R4, R5, R6, R7
    btfss   R3, 7
    return
__pic_negsi:
    pic_neg32 R0, R1, R2, R3
    return

; The sign waits in FSR1H: the unsigned routine uses every other free byte.
    .section .text.__divsi3,"ax",@progbits
    .globl  __divsi3
__divsi3:
    movf    R3, W
    xorwf   R7, W
    movwf   FSR1H
    PAGESEL(__pic_abssi2)
    call    __pic_abssi2
    PAGESEL(__udivsi3)
    call    __udivsi3
    btfss   FSR1H, 7
    return
    PAGESEL(__pic_negsi)
    goto    __pic_negsi

    .section .text.__modsi3,"ax",@progbits
    .globl  __modsi3
__modsi3:
    movf    R3, W
    movwf   FSR1H
    PAGESEL(__pic_abssi2)
    call    __pic_abssi2
    PAGESEL(__umodsi3)
    call    __umodsi3
    btfss   FSR1H, 7
    return
    PAGESEL(__pic_negsi)
    goto    __pic_negsi

; For the linker's choice between stack and static frames (see "Static
; frames" in docs/toolchain.md): what this file's code calls. An object
; file without this section is taken to call everything it refers to.
    .section .pic16f1.frames,"",@progbits
; A routine that calls nothing needs no line. C is a call, which takes a
; level of the hardware stack; J is a jump to a routine that returns for
; this one, which takes none. The linker's count of stack levels goes by
; these.
#ifndef FAST_MATH
    .ascii  "F __mulhi3 -\n"
#endif
    .ascii  "F __umodqi3 -\nC __udivqi3\n"
    .ascii  "F __divqi3 -\nC __pic_absqi2\nC __udivqi3\n"
    .ascii  "F __modqi3 -\nC __pic_absqi2\nC __udivqi3\n"
    .ascii  "F __umodhi3 -\nC __udivhi3\n"
    .ascii  "F __divhi3 -\nC __pic_abshi2\nC __udivhi3\nJ __pic_neghi\n"
    .ascii  "F __modhi3 -\nC __pic_abshi2\nC __umodhi3\nJ __pic_neghi\n"
#ifndef FAST_MATH
    .ascii  "F __mulsi3 -\nJ __pic_movsi\n"
#endif
    .ascii  "F __udivsi3 -\nC __umodsi3\nJ __pic_movsi\n"
    .ascii  "F __udivmodsi4 -\nC __umodsi3\nJ __pic_movsi\n"
    .ascii  "F __divsi3 -\nC __pic_abssi2\nC __udivsi3\nJ __pic_negsi\n"
    .ascii  "F __modsi3 -\nC __pic_abssi2\nC __umodsi3\nJ __pic_negsi\n"
