; structs: a linked list of eight nodes built, reversed and walked, with an
; operation chosen from a table for each node.
; See tests/programs/structs/main.c.
;
; A node is four bytes: id, value (two bytes), next. Every node is in bank
; 0, so a pointer is one byte, and 0 is the null pointer. The table of
; function pointers is a computed jump.
#include "bench.inc"

#define POOL 0x20
#define ID    0
#define VALUE 1
#define NEXT  3

/* Results, the first printed uppermost; see result16. */
#define ACC   0x72
#define ORDER 0x70
#define V0  0x74
#define V1  0x75
#define HEAD 0x76
#define PREV 0x77
#define I   0x78
#define T0  0x79
#define T1  0x7A

    bench_start
    clrf    FSR1H

    ; build: node i has id i and value 1000 + 321 * i, and points at the
    ; node before it.
    movlw   POOL
    movwf   FSR1L
    clrf    HEAD
    clrf    I
    movlw   lo8(1000)
    movwf   V0
    movlw   hi8(1000)
    movwf   V1
1:
    movf    I, W
    movwi   ID[FSR1]
    movf    V0, W
    movwi   VALUE[FSR1]
    movf    V1, W
    movwi   VALUE + 1[FSR1]
    movf    HEAD, W
    movwi   NEXT[FSR1]
    movf    FSR1L, W
    movwf   HEAD
    addfsr  FSR1, 4
    movlw   lo8(321)
    addwf   V0, F
    movlw   hi8(321)
    addwfc  V1, F
    incf    I, F
    btfss   I, 3
    bra     1b

    ; reverse
    clrf    PREV
    movf    HEAD, W
2:
    btfsc   STATUS, Z
    bra     3f
    movwf   FSR1L
    moviw   NEXT[FSR1]
    movwf   HEAD
    movf    PREV, W
    movwi   NEXT[FSR1]
    movf    FSR1L, W
    movwf   PREV
    movf    HEAD, W
    bra     2b
3:

    ; walk: acc = ops[id & 3](acc, value); order = order * 3 + id
    clrf    ACC
    clrf    ACC + 1
    clrf    ORDER
    clrf    ORDER + 1
    movf    PREV, W
4:
    btfsc   STATUS, Z
    bra     9f
    movwf   FSR1L
    moviw   VALUE[FSR1]
    movwf   V0
    moviw   VALUE + 1[FSR1]
    movwf   V1
    moviw   ID[FSR1]
    andlw   3
    brw
    bra     6f              ; op_add
    bra     7f              ; op_sub
    bra     5f              ; op_xor
    movlw   3               ; op_rot: acc rotated left by three, then xor
    movwf   I
.Lrot:
    lslf    ACC, F
    rlf     ACC + 1, F
    btfsc   STATUS, C
    incf    ACC, F
    decfsz  I, F
    bra     .Lrot
5:
    movf    V0, W
    xorwf   ACC, F
    movf    V1, W
    xorwf   ACC + 1, F
    bra     8f
6:
    movf    V0, W
    addwf   ACC, F
    movf    V1, W
    addwfc  ACC + 1, F
    bra     8f
7:
    movf    V0, W
    subwf   ACC, F
    movf    V1, W
    subwfb  ACC + 1, F
8:
    lslf    ORDER, W        ; T = 2 * order
    movwf   T0
    rlf     ORDER + 1, W
    movwf   T1
    moviw   ID[FSR1]        ; T += id
    addwf   T0, F
    movlw   0
    addwfc  T1, F
    movf    T0, W           ; order += T
    addwf   ORDER, F
    movf    T1, W
    addwfc  ORDER + 1, F
    moviw   NEXT[FSR1]
    bra     4b
9:

    point_fsr0 names
    movlw   ACC + 2
    call    result16
    call    result16_next

    movlw   0x64            ; order == 0x0664
    xorwf   ORDER, F
    movlw   0x06
    xorwf   ORDER + 1, W
    iorwf   ORDER, F
    exit_code_from_z
    bench_exit

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
names:
    .asciz  "acc="
    .asciz  "order="
