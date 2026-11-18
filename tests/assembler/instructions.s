    nop
    reset
    return
    retfie
    callw
    brw
    moviw ++FSR0
    moviw --FSR1
    moviw FSR0++
    moviw FSR1--
    movwi ++FSR1
    movwi FSR0--
    movlb 31
    sleep
    clrwdt
    movwf 0x25
    clrw
    clrf 0x7f
    subwf 0x20, w
    decf 0x21, f
    iorwf 0x22, w
    andwf 0x23, f
    xorwf 0x24, f
    addwf 0x25, w
    movf 0x26, w
    comf 0x27, f
    incf 0x28, f
    decfsz 0x29, f
    rrf 0x2a, f
    rlf 0x2b, w
    swapf 0x2c, f
    incfsz 0x2d, w
    bcf 0x03, 0
    bsf 0x0b, 7
    btfsc 0x30, 3
    btfss 0x31, 5
    call 0x123
    goto 0x7ff
    movlw 0xa5
    addfsr FSR1, -32
    addfsr FSR0, 31
    movlp 0x7f
    retlw 0x42
    lslf 0x32, f
    lsrf 0x33, w
    asrf 0x34, f
    iorlw 1
    andlw 2
    xorlw 3
    subwfb 0x35, f
    sublw 4
    addwfc 0x36, w
    addlw 5
    moviw -3[FSR0]
    moviw 31[FSR1]
    movwi -32[FSR1]
    movwi 0[FSR0]
