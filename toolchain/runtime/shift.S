; Shifts of 32- and 64-bit values by a run-time amount.
;
; Calling convention: the value is in R0.. (0x70 up, low byte first) and
; comes back there. For the 32-bit routines the 16-bit count follows in
; R4:R5 (0x74). For the 64-bit routines the value fills R0-R7, so the count
; is the first stack argument, at 0[FSR1]. Only the low byte of the count
; is used. 0x7E is the compiler's scratch byte TMP0.
;
; The loops are the macros of <picmath.inc>, which work on any registers;
; this file places them on the registers of the calling convention.

#include <picmath.inc>

#define R0   0x70
#define R1   0x71
#define R2   0x72
#define R3   0x73
#define R4   0x74
#define R5   0x75
#define R6   0x76
#define R7   0x77
#define TMP0 0x7E

    .text

    .globl  __ashlsi3
__ashlsi3:
    pic_shl32 R0, R1, R2, R3,  R4
    return

    .globl  __lshrsi3
__lshrsi3:
    pic_lsr32 R0, R1, R2, R3,  R4
    return

    .globl  __ashrsi3
__ashrsi3:
    pic_asr32 R0, R1, R2, R3,  R4
    return

    .globl  __ashldi3
__ashldi3:
    moviw   0[FSR1]
    movwf   TMP0
    pic_shl64 R0, R1, R2, R3, R4, R5, R6, R7,  TMP0
    return

    .globl  __lshrdi3
__lshrdi3:
    moviw   0[FSR1]
    movwf   TMP0
    pic_lsr64 R0, R1, R2, R3, R4, R5, R6, R7,  TMP0
    return

    .globl  __ashrdi3
__ashrdi3:
    moviw   0[FSR1]
    movwf   TMP0
    pic_asr64 R0, R1, R2, R3, R4, R5, R6, R7,  TMP0
    return

; For the linker's choice between stack and static frames (see "Static
; frames" in docs/toolchain.md): what this file's code calls. An object
; file without this section is taken to call everything it refers to.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __ashlsi3 -\n"
