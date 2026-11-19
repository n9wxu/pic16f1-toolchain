; The <picmath.inc> macros placed on registers of this file's own choosing:
; a block of banked RAM (14 bytes, so the linker keeps it in one bank), and
; for one routine the core registers FSR0L and FSR0H. Products and
; remainders share four bytes. main.c fills the operands, calls a routine, and compares the result
; with what the compiler computes.

#include <picmath.inc>

    .section .bss.pm_regs,"aw",@nobits
    .globl  pm_regs
pm_regs:
    .space  14

#define A0   pm_regs+0
#define A1   pm_regs+1
#define A2   pm_regs+2
#define A3   pm_regs+3
#define B0   pm_regs+4
#define B1   pm_regs+5
#define B2   pm_regs+6
#define B3   pm_regs+7
#define Q0   pm_regs+8
#define Q1   pm_regs+9
#define Q2   pm_regs+10
#define Q3   pm_regs+11
#define P0   Q0
#define P1   Q1
#define P2   Q2
#define P3   Q3
#define CNT  pm_regs+12
#define SIGN pm_regs+13
#define FSR0L 0x04
#define FSR0H 0x05


    .section .text.pm_mul8,"ax",@progbits
    .globl  pm_mul8
pm_mul8:
    movlb   bank(pm_regs)
    pic_mul8 A0, B0, P0
    return

    .section .text.pm_udivmod8,"ax",@progbits
    .globl  pm_udivmod8
pm_udivmod8:
    movlb   bank(pm_regs)
    pic_udivmod8 A0, B0, Q0, CNT
    return

    .section .text.pm_divmod8,"ax",@progbits
    .globl  pm_divmod8
pm_divmod8:
    movlb   bank(pm_regs)
    pic_divmod8 A0, B0, Q0, CNT, SIGN
    return

    .section .text.pm_mul16,"ax",@progbits
    .globl  pm_mul16
pm_mul16:
    movlb   bank(pm_regs)
    pic_mul16 A0, A1,  B0, B1,  P0, P1
    return

; The same, with the product built in FSR0 and the multiplier's bytes
; apart from each other.
    .section .text.pm_mul16_fsr,"ax",@progbits
    .globl  pm_mul16_fsr
pm_mul16_fsr:
    movlb   bank(pm_regs)
    movf    B1, W
    movwf   SIGN
    pic_mul16 A0, A1,  B0, SIGN,  FSR0L, FSR0H
    movf    FSR0L, W
    movwf   P0
    movf    FSR0H, W
    movwf   P1
    return

    .section .text.pm_udivmod16,"ax",@progbits
    .globl  pm_udivmod16
pm_udivmod16:
    movlb   bank(pm_regs)
    pic_udivmod16 A0, A1,  B0, B1,  Q0, Q1,  CNT
    return

    .section .text.pm_divmod16,"ax",@progbits
    .globl  pm_divmod16
pm_divmod16:
    movlb   bank(pm_regs)
    pic_divmod16 A0, A1,  B0, B1,  Q0, Q1,  CNT, SIGN
    return

    .section .text.pm_mul32,"ax",@progbits
    .globl  pm_mul32
pm_mul32:
    movlb   bank(pm_regs)
    pic_mul32 A0, A1, A2, A3,  B0, B1, B2, B3,  P0, P1, P2, P3
    return

    .section .text.pm_udivmod32,"ax",@progbits
    .globl  pm_udivmod32
pm_udivmod32:
    movlb   bank(pm_regs)
    pic_udivmod32 A0, A1, A2, A3,  B0, B1, B2, B3,  Q0, Q1, Q2, Q3,  CNT
    return

    .section .text.pm_divmod32,"ax",@progbits
    .globl  pm_divmod32
pm_divmod32:
    movlb   bank(pm_regs)
    pic_divmod32 A0, A1, A2, A3,  B0, B1, B2, B3,  Q0, Q1, Q2, Q3,  CNT, SIGN
    return

    .section .text.pm_neg,"ax",@progbits
    .globl  pm_neg
pm_neg:
    movlb   bank(pm_regs)
    pic_neg8  B0
    pic_neg16 Q0, Q1
    pic_neg32 A0, A1, A2, A3
    return

    .section .text.pm_abs,"ax",@progbits
    .globl  pm_abs
pm_abs:
    movlb   bank(pm_regs)
    pic_abs8  B0
    pic_abs16 Q0, Q1
    pic_abs32 A0, A1, A2, A3
    return

    .section .text.pm_shl32,"ax",@progbits
    .globl  pm_shl32
pm_shl32:
    movlb   bank(pm_regs)
    pic_shl32 A0, A1, A2, A3,  CNT
    return

    .section .text.pm_lsr32,"ax",@progbits
    .globl  pm_lsr32
pm_lsr32:
    movlb   bank(pm_regs)
    pic_lsr32 A0, A1, A2, A3,  CNT
    return

    .section .text.pm_asr32,"ax",@progbits
    .globl  pm_asr32
pm_asr32:
    movlb   bank(pm_regs)
    pic_asr32 A0, A1, A2, A3,  CNT
    return

    .section .text.pm_shl64,"ax",@progbits
    .globl  pm_shl64
pm_shl64:
    movlb   bank(pm_regs)
    pic_shl64 A0, A1, A2, A3, B0, B1, B2, B3,  CNT
    return

    .section .text.pm_lsr64,"ax",@progbits
    .globl  pm_lsr64
pm_lsr64:
    movlb   bank(pm_regs)
    pic_lsr64 A0, A1, A2, A3, B0, B1, B2, B3,  CNT
    return

    .section .text.pm_asr64,"ax",@progbits
    .globl  pm_asr64
pm_asr64:
    movlb   bank(pm_regs)
    pic_asr64 A0, A1, A2, A3, B0, B1, B2, B3,  CNT
    return
