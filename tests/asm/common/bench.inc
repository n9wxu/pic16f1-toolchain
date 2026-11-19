; Harness for the hand-written reference programs: the same job as
; tests/common/bench.c (EUSART output, exit code on PORTD, SLEEP), written
; for size. A program includes this file, starts with `bench_start`, and
; places the routines it uses with the macros at the end of this file.
;
; Each program has two versions. main.S is the smallest and fastest program
; that does the job: what can be worked out beforehand is, and for most
; programs that is all of the output. runtime.S computes everything when it
; runs, by the algorithm of the C program; it is the measure of the code a
; compiler generates.
;
; Conventions of these programs (they do not follow the compiler's):
;   - Everything fits in the first page, so PCLATH is never touched.
;   - FSR1H stays 0, so FSR1L alone points into common RAM and bank 0.
;   - A routine may change W, STATUS, BSR and FSR0 unless it says otherwise.
;   - HT (0x7F) belongs to hex8.

#define STATUS  0x03
#define C       0
#define Z       2
#define FSR0L   0x04
#define FSR0H   0x05
#define FSR1L   0x06
#define FSR1H   0x07
#define INTCON  0x0B
#define HT      0x7F

; Banked registers of the PIC16F1937, as the address within the bank.
#define PIR1    0x11        /* bank 0 */
#define TXIF    4
#define TMR0    0x15        /* bank 0 */
#define PORTB   0x0D        /* bank 0 */
#define TRISB   0x0D        /* bank 1 */
#define TRISD   0x0F        /* bank 1 */
#define OPTION_REG 0x15     /* bank 1 */
#define OSCCON  0x19        /* bank 1 */
#define LATB    0x0D        /* bank 2 */
#define LATD    0x0F        /* bank 2 */
#define TXREG   0x1A        /* bank 3 */
#define SPBRGL  0x1B        /* bank 3 */
#define RCSTA   0x1D        /* bank 3 */
#define TXSTA   0x1E        /* bank 3 */
#define TRMT    1

; Configuration words: as tests/common/bench.c.
    .section .config,"a",@progbits
    .short  0x0FE4, 0x1EFF

; bench_init's work, in line. Leaves bank 3 selected.
.macro bench_init
    movlb   1
    movlw   0x70            ; 8 MHz internal oscillator
    movwf   OSCCON
    clrf    TRISD
    movlb   2
    movlw   0xFF            ; exit-code port: 0xFF while running
    movwf   LATD
    movlb   3
    movlw   51              ; 9600 baud at 8 MHz with BRGH = 1
    movwf   SPBRGL
    bsf     TXSTA, 2        ; BRGH
    bsf     RCSTA, 7        ; SPEN
    bsf     TXSTA, 5        ; TXEN
.endm

; The usual start of a program: it begins at the reset vector, with the
; initialisation, and everything else follows in the same section. (A
; program with an interrupt handler must keep words 4 up free for it; see
; timer_isr.)
.macro bench_start
    .section .reset,"ax",@progbits
    .globl  _start
_start:
    bench_init
.endm

; A whole program after its initialisation, for one whose output is known
; when it is written: send the string at \text and exit with code 0.
; Expects bank 3, which bench_init leaves, or bank 0.
.macro bench_say text
    point_fsr0 \text
1:
    moviw   FSR0++
    btfsc   STATUS, Z
    bra     3f
    movlb   0
2:
    btfss   PIR1, TXIF
    bra     2b
    movlb   3
    movwf   TXREG
    bra     1b
3:
    btfss   TXSTA, TRMT
    bra     3b
    movlb   2
    movwf   LATD            ; W is the string's zero: exit code 0
4:
    sleep
    bra     4b
.endm

; puts: send the string at FSR0, which ends up just past its zero, so that
;       strings stored one after another need FSR0 set only once.
; hex8: send W as two hex digits (left out with `bench_io hex=0`).
; putc: send the character in W.
.macro bench_io hex=1
puts:
    moviw   FSR0++
    btfsc   STATUS, Z
    return
    call    putc
    bra     puts
    .if \hex
hex8:
    movwf   HT
    swapf   HT, W
    call    hex4
    movf    HT, W
hex4:
    andlw   0x0F
    addlw   -10
    btfsc   STATUS, C
    addlw   7               ; 'A' - '9' - 1
    addlw   58              ; '0' + 10
    .endif
putc:
    movlb   0
1:
    btfss   PIR1, TXIF
    bra     1b
    movlb   3
    movwf   TXREG
    return
.endm

; exit: W is the exit code; never returns. Placed where the main code ends,
;       the program runs straight into it.
.macro bench_exit
exit:
    movlb   3
1:
    btfss   TXSTA, TRMT
    bra     1b
    movlb   2
    movwf   LATD
2:
    sleep
    bra     2b
.endm

; result16: send the string at FSR0 (written with its '='), then the 16-bit
;           value that ends just below the address in W, then a newline.
; result32: the same for a 32-bit value.
; Both leave FSR1 at the low byte of the value, so a value stored just
; below the last one printed needs no address: call result16_next or
; result32_next. A program therefore keeps its results in descending
; order of printing. `bench_result16 entry=0` leaves out result16 itself,
; for a program that only prints 32-bit values.
.macro bench_result16 entry=1
    .if \entry
result16:
    movwf   FSR1L
result16_next:
    call    puts
    .endif
result_low:
    moviw   --FSR1
    call    hex8
    moviw   --FSR1
    call    hex8
    movlw   10
    bra     putc
.endm

.macro bench_result32
result32:
    movwf   FSR1L
result32_next:
    call    puts
    moviw   --FSR1
    call    hex8
    moviw   --FSR1
    call    hex8
    bra     result_low
.endm

; FSR0 = address (of flash data or RAM).
.macro point_fsr0 at
    movlw   lo8(\at)
    movwf   FSR0L
    movlw   hi8(\at)
    movwf   FSR0H
.endm

#define INDF0   0x00
#define INDF1   0x01

; W = 0 if the flags say "equal" (Z set), else 1: the usual exit code.
.macro exit_code_from_z
    movlw   0
    btfss   STATUS, Z
    movlw   1
.endm
