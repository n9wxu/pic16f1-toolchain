; fsm: a tokenizer and left-to-right calculator over three strings.
; See tests/programs/fsm/main.c.
;
; The two states that wait for a number (S_START and S_OP) behave alike and
; are one here; S_ERROR is a return. A failed evaluation gives 0, which is
; what the C leaves in its result variable.
#include "bench.inc"
#include <picmath.inc>

/* Results, the first printed uppermost; see result16. */
#define R1  0x74
#define R2  0x72
#define OK  0x70
#define ACC 0x76
#define CUR 0x78
#define P0  0x7A            /* product or remainder; also 2 * cur */
#define P1  0x7B
#define OP  0x7C
#define CH  0x7D
#define ST  0x7E            /* bit 0: inside a number */
#define D   0x7F            /* digit value; the divide's counter */

    bench_start
    clrf    FSR1H
    clrf    OK
    clrf    OK + 1
    point_fsr0 expr         ; each evaluation ends at the next string
    call    eval
    movwf   OK
    movf    ACC, W
    movwf   R1
    movf    ACC + 1, W
    movwf   R1 + 1
    call    eval
    lslf    OK, F
    iorwf   OK, F
    movf    ACC, W
    movwf   R2
    movf    ACC + 1, W
    movwf   R2 + 1
    call    eval
    lslf    OK, F
    iorwf   OK, F

    point_fsr0 names
    movlw   R1 + 2
    call    result16
    call    result16_next
    call    result16_next

    ; ok1 && ok2 && !ok3 && r1 == 56 && r2 == 3004 (0x0BBC)
    movlw   6
    xorwf   OK, F
    movlw   56
    xorwf   R1, W
    iorwf   R1 + 1, W
    iorwf   OK, F
    movlw   0xBC
    xorwf   R2, W
    iorwf   OK, F
    movlw   0x0B
    xorwf   R2 + 1, W
    iorwf   OK, F
    exit_code_from_z
    bench_exit

; Evaluate the string at FSR0. W = 1 and ACC = the value, or W = 0 and
; ACC = 0 for a malformed string. After a good string FSR0 is past its zero.
eval:
    clrf    ACC
    clrf    ACC + 1
    clrf    OP
    clrf    ST
.Lnext:
    moviw   FSR0++
    movwf   CH
    addlw   -'0'
    movwf   D
    addlw   -10             ; no carry: a digit
    btfsc   ST, 0
    bra     .Lnumber

    ; Waiting for a number.
    btfsc   STATUS, C
    bra     .Lspace
    movf    D, W
    movwf   CUR
    clrf    CUR + 1
    bsf     ST, 0
    bra     .Lnext
.Lspace:
    movf    CH, W
    xorlw   ' '
    btfsc   STATUS, Z
    bra     .Lnext
.Lerror:
    clrf    ACC
    clrf    ACC + 1
    retlw   0

    ; Inside a number.
.Lnumber:
    btfsc   STATUS, C
    bra     .Lend_number
    lslf    CUR, F          ; cur = cur * 10 + digit
    rlf     CUR + 1, F
    movf    CUR, W
    movwf   P0
    movf    CUR + 1, W
    movwf   P1
    lslf    CUR, F
    rlf     CUR + 1, F
    lslf    CUR, F
    rlf     CUR + 1, F
    movf    P0, W
    addwf   CUR, F
    movf    P1, W
    addwfc  CUR + 1, F
    movf    D, W
    addwf   CUR, F
    movlw   0
    addwfc  CUR + 1, F
    bra     .Lnext

.Lend_number:
    call    apply
    movf    CH, W
    btfsc   STATUS, Z
    retlw   1               ; the end of the string, after a number
    call    is_operator
    btfsc   STATUS, Z
    bra     .Loperator
    movf    CH, W
    xorlw   ' '
    btfss   STATUS, Z
    bra     .Lerror
    clrf    OP              ; a space: the value so far stands, unless an
    movf    ACC, W          ; operator follows the spaces
    movwf   CUR
    movf    ACC + 1, W
    movwf   CUR + 1
.Lspaces:
    moviw   FSR0++
    movwf   CH
    xorlw   ' '
    btfsc   STATUS, Z
    bra     .Lspaces
    movf    CH, W
    call    is_operator
    btfsc   STATUS, Z
    bra     .Loperator
    addfsr  FSR0, -1        ; not an operator: only the end may follow
    movf    CH, W
    btfss   STATUS, Z
    bra     .Lerror
    bra     .Lnext
.Loperator:
    movf    CH, W
    movwf   OP
    clrf    ST
    bra     .Lnext

; Z set if W is one of + - * /.
is_operator:
    xorlw   '+'
    btfsc   STATUS, Z
    return
    xorlw   '+' ^ '-'
    btfsc   STATUS, Z
    return
    xorlw   '-' ^ '*'
    btfsc   STATUS, Z
    return
    xorlw   '*' ^ '/'
    return

; ACC = ACC op CUR; with no operator, ACC = CUR. CUR is used up.
apply:
    movf    OP, W
    xorlw   '+'
    btfsc   STATUS, Z
    bra     .Ladd
    xorlw   '+' ^ '-'
    btfsc   STATUS, Z
    bra     .Lsub
    xorlw   '-' ^ '*'
    btfsc   STATUS, Z
    bra     .Lmul
    xorlw   '*' ^ '/'
    btfss   STATUS, Z
    bra     .Lcopy
    ; Dividing by zero gives 0xFFFF, as the C does.
    pic_udivmod16 ACC, ACC + 1,  CUR, CUR + 1,  P0, P1,  D
    return
.Lmul:
    pic_mul16 CUR, CUR + 1,  ACC, ACC + 1,  P0, P1
    movf    P0, W
    movwf   CUR
    movf    P1, W
    movwf   CUR + 1
.Lcopy:
    movf    CUR, W
    movwf   ACC
    movf    CUR + 1, W
    movwf   ACC + 1
    return
.Ladd:
    movf    CUR, W
    addwf   ACC, F
    movf    CUR + 1, W
    addwfc  ACC + 1, F
    return
.Lsub:
    movf    CUR, W
    subwf   ACC, F
    movf    CUR + 1, W
    subwfb  ACC + 1, F
    return

    bench_io
    bench_result16

    .section .rodata,"a",@progbits
expr:
    .asciz  "12 + 34 * 5 - 6 / 4"
    .asciz  "1000-1 *3+7"
    .asciz  "5 + + 2"
names:
    .asciz  "r1="
    .asciz  "r2="
    .asciz  "ok="
