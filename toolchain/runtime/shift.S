; Shifts of 32- and 64-bit values by a run-time amount.
;
; Calling convention: the value is in R0.. (0x70 up, low byte first) and
; comes back there. For the 32-bit routines the 16-bit count follows in
; R4:R5 (0x74). For the 64-bit routines the value fills R0-R7, so the count
; is the first stack argument, at 0[FSR1]. Only the low byte of the count
; is used. 0x7E is the compiler's scratch byte TMP0; STATUS is 0x03.

    .text

    .globl  __ashlsi3
__ashlsi3:
    movf    0x74, W
    btfsc   0x03, 2
    return
.Lshl32:
    lslf    0x70, F
    rlf     0x71, F
    rlf     0x72, F
    rlf     0x73, F
    decfsz  0x74, F
    bra     .Lshl32
    return

    .globl  __lshrsi3
__lshrsi3:
    movf    0x74, W
    btfsc   0x03, 2
    return
.Llsr32:
    lsrf    0x73, F
    rrf     0x72, F
    rrf     0x71, F
    rrf     0x70, F
    decfsz  0x74, F
    bra     .Llsr32
    return

    .globl  __ashrsi3
__ashrsi3:
    movf    0x74, W
    btfsc   0x03, 2
    return
.Lasr32:
    asrf    0x73, F
    rrf     0x72, F
    rrf     0x71, F
    rrf     0x70, F
    decfsz  0x74, F
    bra     .Lasr32
    return

    .globl  __ashldi3
__ashldi3:
    moviw   0[FSR1]
    movwf   0x7E
    movf    0x7E, W
    btfsc   0x03, 2
    return
.Lshl64:
    lslf    0x70, F
    rlf     0x71, F
    rlf     0x72, F
    rlf     0x73, F
    rlf     0x74, F
    rlf     0x75, F
    rlf     0x76, F
    rlf     0x77, F
    decfsz  0x7E, F
    bra     .Lshl64
    return

    .globl  __lshrdi3
__lshrdi3:
    moviw   0[FSR1]
    movwf   0x7E
    movf    0x7E, W
    btfsc   0x03, 2
    return
.Llsr64:
    lsrf    0x77, F
    rrf     0x76, F
    rrf     0x75, F
    rrf     0x74, F
    rrf     0x73, F
    rrf     0x72, F
    rrf     0x71, F
    rrf     0x70, F
    decfsz  0x7E, F
    bra     .Llsr64
    return

    .globl  __ashrdi3
__ashrdi3:
    moviw   0[FSR1]
    movwf   0x7E
    movf    0x7E, W
    btfsc   0x03, 2
    return
.Lasr64:
    asrf    0x77, F
    rrf     0x76, F
    rrf     0x75, F
    rrf     0x74, F
    rrf     0x73, F
    rrf     0x72, F
    rrf     0x71, F
    rrf     0x70, F
    decfsz  0x7E, F
    bra     .Lasr64
    return
