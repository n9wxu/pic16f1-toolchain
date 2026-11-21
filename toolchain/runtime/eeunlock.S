; The unlock of a write to the data EEPROM or to flash, for the parts whose
; EECON1 and EECON2 (PMCON1 and PMCON2 on parts without EEPROM) are at
; 0x195 and 0x196: the PIC16F193x, 182x, 151x and their kind.
;
; void __eecon_unlock(unsigned int keys)
;
; The part starts the write only if 0x55 and then 0xAA are written to
; EECON2 and WR is set, with exactly these five instructions and nothing
; between them (measured on a PIC16LF1937: a bank selection between the
; two is enough to make it ignore the write). So they are here, in
; assembly, and not left to what a compiler makes of three statements.
;
; The two values arrive as the caller's argument, low byte first, in R0 and
; R1; this file has no 0x55 and no 0xAA in it. A caller keeps them in
; variables that it sets just before a write and clears just after, so that
; a program that has lost its way and lands here writes nothing.
; Interrupts must be off: the caller's business, as is waiting for WR.

#define R0     0x70
#define R1     0x71
#define EECON1 0x15         /* in bank 3 */
#define EECON2 0x16

    .section .text.__eecon_unlock,"ax",@progbits
    .globl  __eecon_unlock
__eecon_unlock:
    movlb   3
    movf    R0, W
    movwf   EECON2
    movf    R1, W
    movwf   EECON2
    bsf     EECON1, 1       ; WR
    return

; For the linker's choice between stack and static frames: this file calls
; nothing.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F __eecon_unlock -\n"
