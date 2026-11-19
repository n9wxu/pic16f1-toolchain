; setjmp and longjmp.
;
; Return addresses are not in memory on this core but on its 16-level
; hardware stack, and the enhanced mid-range core lets a program write that
; stack's pointer (STKPTR) and top entry (TOSL, TOSH). longjmp only ever
; goes back to a function that is still running, so every entry below the
; one setjmp was called with is still what it was: longjmp sets the pointer
; back and returns. The entry setjmp itself returned through has been used
; again since, so that one is kept in the buffer and written back.
;
; The buffer (jmp_buf, 10 bytes):
;   0, 1   the return address of the call to setjmp (TOSL, TOSH)
;   2      the stack pointer cell __sp, low byte
;   3      low four bits: __sp high byte (which is always 0x2n, RAM being
;          at 0x2000 in the address space the FSRs use);
;          high four bits: STKPTR
;   4-9    the registers a function preserves, 0x78-0x7D. A function that
;          longjmp skips over never gets to restore the ones it saved.
;
; Restoring __sp also gives back every variable-length array and alloca
; made since. Variables with static storage, which is where a function with
; a static frame keeps its own, are left as they are; C makes the ones
; changed since setjmp indeterminate anyway unless they are volatile.
;
; Not for leaving an interrupt handler: that runs on a stack of its own and
; the core's shadow registers would be left behind.
;
; One library serves 32- and 64-bank devices, whose bank selection is
; encoded differently. The stack registers are in the last bank of either:
; writing 0x3F to BSR selects it on both, since a 32-bank device has only
; five bits there. __sp is reached through FSR1.

#define STATUS 0x03
#define FSR0L 0x04
#define FSR0H 0x05
#define FSR1L 0x06
#define FSR1H 0x07
#define BSR   0x08
#define R0    0x70
#define R1    0x71
#define R2    0x72
#define R3    0x73
#define TMP0  0x7E
; In the last bank.
#define STKPTR 0x6D
#define TOSL   0x6E
#define TOSH   0x6F

; int setjmp(jmp_buf env): env in R0/R1, result in R0/R1.
    .section .text.setjmp,"ax",@progbits
    .globl  setjmp
setjmp:
    movf    R0, W
    movwf   FSR0L
    movf    R1, W
    movwf   FSR0H
    movlw   0x3f
    movwf   BSR
    movf    TOSL, W
    movwi   FSR0++
    movf    TOSH, W
    movwi   FSR0++
    swapf   STKPTR, W
    andlw   0xf0
    movwf   TMP0
    movlw   lo8(__sp)
    movwf   FSR1L
    movlw   hi8(__sp)
    movwf   FSR1H
    moviw   0[FSR1]
    movwi   FSR0++
    moviw   1[FSR1]
    andlw   0x0f
    iorwf   TMP0, W
    movwi   FSR0++
    movf    0x78, W
    movwi   FSR0++
    movf    0x79, W
    movwi   FSR0++
    movf    0x7a, W
    movwi   FSR0++
    movf    0x7b, W
    movwi   FSR0++
    movf    0x7c, W
    movwi   FSR0++
    movf    0x7d, W
    movwi   FSR0++
    clrf    R0
    clrf    R1
    return

; void longjmp(jmp_buf env, int val): env in R0/R1, val in R2/R3. Returns
; from the call to setjmp that filled env, with val, or 1 if val is 0.
    .section .text.longjmp,"ax",@progbits
    .globl  longjmp
longjmp:
    movf    R0, W
    movwf   FSR0L
    movf    R1, W
    movwf   FSR0H
    movf    R2, W
    iorwf   R3, W
    btfsc   STATUS, 2
    incf    R2, F
    movlw   0x3f
    movwf   BSR
    moviw   3[FSR0]
    movwf   TMP0
    swapf   TMP0, W
    andlw   0x0f
    movwf   STKPTR
    moviw   0[FSR0]
    movwf   TOSL
    moviw   1[FSR0]
    movwf   TOSH
    movlw   lo8(__sp)
    movwf   FSR1L
    movlw   hi8(__sp)
    movwf   FSR1H
    moviw   2[FSR0]
    movwi   0[FSR1]
    movf    TMP0, W
    andlw   0x0f
    iorlw   0x20
    movwi   1[FSR1]
    moviw   4[FSR0]
    movwf   0x78
    moviw   5[FSR0]
    movwf   0x79
    moviw   6[FSR0]
    movwf   0x7a
    moviw   7[FSR0]
    movwf   0x7b
    moviw   8[FSR0]
    movwf   0x7c
    moviw   9[FSR0]
    movwf   0x7d
    movf    R2, W
    movwf   R0
    movf    R3, W
    movwf   R1
    return

; For the linker's choice between stack and static frames: neither calls
; anything.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F setjmp -\n"
    .ascii  "F longjmp -\n"
