; sieve: the primes below 256 with a 32-byte bit array.
; See tests/programs/sieve/main.c.
#include "bench.inc"

#define COUNT 0x72          /* results as 16-bit values: high bytes zero; */
#define COUNTH 0x73         /* the first printed uppermost */
#define LAST 0x70
#define LASTH 0x71
#define I   0x74
#define J   0x75
#define N   0x76
#define M   0x77
#define BITS 0x20           /* composite[32], in bank 0 */

    bench_start
    clrf    FSR1H
    movlw   BITS            ; composite[] = 0
    movwf   FSR1L
    movlw   32
    movwf   N
    movlw   0
1:
    movwi   FSR1++
    decfsz  N, F
    bra     1b
    clrf    COUNT
    clrf    COUNTH
    clrf    LAST
    clrf    LASTH

    movlw   2
    movwf   I
2:
    movf    I, W
    call    locate
    andwf   INDF1, W
    btfss   STATUS, Z
    bra     5f
    clrf    J               ; j = i * i, by adding i to itself i times
    movf    I, W
    movwf   N
3:
    addwf   J, F
    decfsz  N, F
    bra     3b
4:
    movf    J, W            ; set(j)
    call    locate
    iorwf   INDF1, F
    movf    I, W            ; j += i, until it passes 255
    addwf   J, F
    btfss   STATUS, C
    bra     4b
5:
    incf    I, F
    btfss   I, 4            ; i < 16
    bra     2b

    movlw   2
    movwf   I
6:
    movf    I, W
    call    locate
    andwf   INDF1, W
    btfss   STATUS, Z
    bra     7f
    incf    COUNT, F
    movf    I, W
    movwf   LAST
7:
    incfsz  I, F
    bra     6b

    point_fsr0 names
    movlw   COUNT + 2
    call    result16
    call    result16_next

    movlw   54              ; count == 54 && last == 251
    xorwf   COUNT, F
    movlw   251
    xorwf   LAST, W
    iorwf   COUNT, F
    exit_code_from_z
    bench_exit

; For bit number W: FSR1 at its byte of composite[], W = its mask.
locate:
    movwf   N
    lsrf    N, W            ; BITS + n / 8
    movwf   FSR1L
    lsrf    FSR1L, F
    lsrf    FSR1L, W
    addlw   BITS
    movwf   FSR1L
    movlw   1               ; 1 << (n & 7)
    btfsc   N, 1
    movlw   4
    movwf   M
    btfsc   N, 0
    addwf   M, F
    btfsc   N, 2
    swapf   M, F
    movf    M, W
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "count="
    .asciz  "last="
