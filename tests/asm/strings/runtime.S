; strings: copy, length, reverse, compare, and conversion to decimal.
; See tests/programs/strings/main.c.
;
; The buffers are in bank 0, where an address is one byte and FSRxH is 0.
; FSR1H is set only around the comparisons with strings in flash.
#include "bench.inc"

#define BUF 0x20            /* 24 bytes */
#define NUM 0x38            /* 8 bytes */

#define LEN 0x70            /* a result, as a 16-bit value */
#define V0  0x72            /* the number being converted; then quotient */
#define V1  0x73
#define R   0x74            /* remainder */
#define K   0x75
#define T   0x76
#define BAD 0x77            /* the exit code */

    bench_start
    clrf    FSR1H
    clrf    BAD
    clrf    LEN
    clrf    LEN + 1

    ; my_strcpy(buf, "enhanced mid-range")
    point_fsr0 text
    movlw   BUF
    movwf   FSR1L
1:
    moviw   FSR0++
    movwi   FSR1++
    btfss   STATUS, Z
    bra     1b

    ; my_strlen(buf)
    movlw   BUF
    movwf   FSR1L
2:
    moviw   FSR1++
    btfsc   STATUS, Z
    bra     3f
    incf    LEN, F
    bra     2b
3:
    point_fsr0 names
    movlw   LEN + 2
    call    result16

    movlw   BUF
    call    reverse
    call    put_buf
    movlw   10
    call    putc
    movlw   BUF
    call    reverse

    ; buf against the text, then "abc" against "abd" and the other way
    ; round: the strings follow one another in flash.
    movlw   BUF
    movwf   FSR0L           ; FSR0H is 0 after reverse
    movlw   lo8(text)
    movwf   FSR1L
    movlw   hi8(text)
    movwf   FSR1H
    call    compare         ; FSR1 ends at "abc"
    btfss   STATUS, Z
    bsf     BAD, 0
    movf    FSR1L, W        ; a = "abc", b = "abd"
    movwf   FSR0L
    movf    FSR1H, W
    movwf   FSR0H
    addfsr  FSR1, 4
    call    compare         ; stops after the third letters
    movwf   T
    btfss   T, 7            ; must be negative
    bsf     BAD, 0
    addfsr  FSR0, 1         ; a = "abd", b = "abc"
    addfsr  FSR1, -7
    call    compare
    movwf   T
    btfsc   T, 7            ; must be positive
    bsf     BAD, 0
    movf    T, F
    btfsc   STATUS, Z
    bsf     BAD, 0
    clrf    FSR1H

    clrf    V0              ; 0
    clrf    V1
    call    put_number
    movlw   ' '
    call    putc
    movlw   lo8(1937)
    movwf   V0
    movlw   hi8(1937)
    movwf   V1
    call    put_number
    movlw   ' '
    call    putc
    movlw   0xFF            ; 65535
    movwf   V0
    movwf   V1
    call    put_number
    movlw   10
    call    putc

    movlw   NUM             ; num against "65535"
    movwf   FSR0L
    clrf    FSR0H
    movlw   lo8(digits)
    movwf   FSR1L
    movlw   hi8(digits)
    movwf   FSR1H
    call    compare
    btfss   STATUS, Z
    bsf     BAD, 0
    movf    BAD, W
    bench_exit

; my_strcmp: W = the first differing character of the string at FSR0 less
; the one at FSR1, or 0 (and Z set) if the strings are the same. Both
; pointers end just past the characters last compared.
compare:
    moviw   FSR0++
    movwf   T
    moviw   FSR1++
    subwf   T, W
    btfss   STATUS, Z
    return
    movf    T, F
    btfss   STATUS, Z
    bra     compare
    retlw   0

; utoa(V, num), then send num.
put_number:
    movlw   NUM
    movwf   FSR1L
1:
    clrf    R               ; V = V / 10, R = V % 10
    movlw   16
    movwf   K
2:
    lslf    V0, F
    rlf     V1, F
    rlf     R, F
    movlw   10
    subwf   R, W
    btfss   STATUS, C
    bra     3f
    movwf   R
    bsf     V0, 0
3:
    decfsz  K, F
    bra     2b
    movf    R, W
    addlw   '0'
    movwi   FSR1++
    movf    V0, W
    iorwf   V1, W
    btfss   STATUS, Z
    bra     1b
    clrf    INDF1
    movlw   NUM
    call    reverse
    movlw   NUM
    bra     put_at
put_buf:
    movlw   BUF
put_at:
    movwf   FSR0L
    clrf    FSR0H
    goto    puts

; Reverse the string at address W (in bank 0) in place. Leaves FSR0H = 0.
reverse:
    movwf   FSR0L
    clrf    FSR0H
    movwf   FSR1L
1:
    moviw   FSR1++          ; find the end
    btfss   STATUS, Z
    bra     1b
    addfsr  FSR1, -1
2:
    movf    FSR0L, W        ; while at least two characters lie between
    subwf   FSR1L, W
    addlw   -2
    btfss   STATUS, C
    return
    moviw   --FSR1
    movwf   T
    movf    INDF0, W
    movwf   INDF1
    movf    T, W
    movwi   FSR0++
    bra     2b

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "len="
text:
    .asciz  "enhanced mid-range"
    .asciz  "abc"
    .asciz  "abd"
digits:
    .asciz  "65535"
