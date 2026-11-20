\ Forth on the PIC16F1: each part checks itself, and the program reports
\ through LATD as the C tests do ($5A if everything held, otherwise the
\ number of the first failure). For scripts/test-forth.sh.

CONFIG" FOSC=INTOSC, WDTE=OFF, MCLRE=ON, LVP=ON"
8000000 CLOCK-HZ

VARIABLE failed
VARIABLE seed-cell
CVARIABLE ticks
VARIABLE total
16 BUFFER: scratch
CREATE primes 2 , 3 , 5 , 7 , 11 ,
ROM-CREATE squares 0 C, 1 C, 4 C, 9 C, 16 C, 25 C,
42 CONSTANT answer
answer 2 * 1+ CONSTANT eighty-five

: check ( flag number -- )  SWAP IF DROP EXIT THEN  failed @ 0= IF failed ! EXIT THEN  DROP ;
: seed ( -- 7 )  seed-cell @ ;

: square ( n -- n*n )  DUP * ;
: cube   ( n -- n^3 )  DUP square * ;
: between ( n lo hi -- flag )  1+ WITHIN ;
: sum-to ( n -- sum )  0 SWAP 1+ 0 DO I + LOOP ;
: factorial ( n -- n! )  DUP 1 > IF DUP 1- RECURSE * THEN ;
: gcd ( a b -- gcd )  BEGIN ?DUP WHILE TUCK MOD REPEAT ;
: count-bits ( x -- n )  0 SWAP BEGIN DUP WHILE DUP 1 AND ROT + SWAP 1 RSHIFT REPEAT DROP ;
: classify ( n -- code )  DUP 10 < IF DROP 1 ELSE 100 < IF 2 ELSE 3 THEN THEN ;
: double ( n -- 2n )  2* ;
: triple ( n -- 3n )  DUP 2* + ;
: apply ( n xt -- m )  EXECUTE ;
: first-over ( limit -- index )  10 0 DO I square OVER > IF DROP I UNLOOP EXIT THEN LOOP DROP 99 ;

INTERRUPT: tick  INTCON TMR0IF BIT? IF INTCON TMR0IF BIT-CLR  1 ticks C@ + ticks C! THEN ;

: MAIN
   7 seed-cell !

   \ 1x: the stack words
   1 2 SWAP        1 = SWAP 2 = AND                      10 check
   1 2 OVER        1 = SWAP 2 = AND SWAP 1 = AND          11 check
   1 2 3 ROT       1 = SWAP 3 = AND SWAP 2 = AND          12 check
   1 2 NIP         2 =                                    13 check
   1 2 TUCK        2 = SWAP 1 = AND SWAP 2 = AND          14 check
   5 ?DUP + 10 =   0 ?DUP 0= AND                          15 check
   1 2 2DUP + + + 6 =                                     16 check
   3 >R 4 R@ + R> + 10 =                                  17 check

   \ 2x: arithmetic
   seed 1000 * 7000 =   seed -3 * -21 = AND               20 check
   -2100 seed / -300 =  -2100 1000 MOD -100 = AND         21 check
   60000 seed U/ 8571 = 60000 1000 UMOD 0= AND            22 check
   seed NEGATE -7 =  -7 ABS 7 = AND  seed INVERT -8 = AND 23 check
   seed 2* 14 =  seed 2/ 3 = AND  seed 1+ 8 = AND  seed 1- 6 = AND  24 check
   $1234 $00FF AND $34 =  $1200 $34 OR $1234 = AND  $FFFF $1234 XOR $EDCB = AND  25 check
   seed 4 LSHIFT 112 =  $8000 seed RSHIFT $0100 = AND  seed 9 LSHIFT $0E00 = AND 26 check
   17 5 /MOD 3 = SWAP 2 = AND   3 9 MIN 3 = AND  3 9 MAX 9 = AND               27 check
   seed 300 + 307 =  seed 300 - -293 = AND  seed 1 + 8 = AND                    28 check

   \ 3x: comparisons
   seed 7 =   seed 8 <> AND   seed 0= 0= AND   0 0= AND             30 check
   -1 0<   seed 0< 0= AND                                           31 check
   -5 seed <   seed -5 > AND   seed seed < 0= AND                   32 check
   seed 60000 U<   60000 seed U> AND   -1 seed U< 0= AND            33 check
   seed 5 9 between   seed 8 9 between 0= AND                       34 check

   \ 4x: variables, tables in RAM and in flash
   1234 total !  total @ 1234 =   66 total +!  total @ 1300 = AND   40 check
   -1 total +!  total @ 1299 =                                      41 check
   primes 3 2 * + @ 7 =   primes @ 2 = AND                          42 check
   99 primes !  primes @ 99 =                                       43 check
   squares 5 + C@ 25 =   squares $8000 U< 0= AND                    44 check
   scratch 16 65 FILL  scratch 15 + C@ 65 =  scratch C@ 65 = AND    45 check
   squares scratch 6 CMOVE  scratch 4 + C@ 16 =  scratch 6 + C@ 65 = AND  46 check
   answer 42 =  eighty-five 85 = AND                                47 check

   \ 5x: control structures
   10 sum-to 55 =   100 sum-to 5050 = AND                           50 check
   0 10 0 DO I 2 MOD IF I + THEN LOOP 25 =                          51 check
   0 20 0 DO I + 5 +LOOP 30 =   0 0 10 DO I + -2 +LOOP 30 = AND     52 check
   0 3 0 DO 4 0 DO J 10 * I + + LOOP LOOP 138 =                     53 check
   0 10 0 DO I 5 = IF LEAVE THEN 1+ LOOP 5 =                        54 check
   0 BEGIN 1+ DUP 10 = UNTIL 10 =                                   55 check
   50 first-over 8 =  1000 first-over 99 = AND                      56 check
   5 classify 1 =  50 classify 2 = AND  500 classify 3 = AND        57 check

   \ 6x: definitions that call definitions
   seed square 49 =  3 cube 27 = AND                                60 check
   7 factorial 5040 =                                               61 check
   1071 462 gcd 21 =                                                62 check
   $F0F1 count-bits 9 =                                             63 check
   seed ['] double apply 14 =  seed ['] triple apply 21 = AND       64 check

   \ 7x: registers and bits, known when the program is compiled
   0 ANSELE C!  0 TRISE C!  0 LATE C!
   LATE 0 BIT-SET  LATE 1 BIT-SET  LATE 1 BIT-TOGGLE  LATE 2 BIT-TOGGLE
   LATE C@ 7 AND 5 =                                                70 check
   LATE 0 BIT?  LATE 1 BIT? 0= AND                                  71 check
   LATE 0 BIT-CLR  LATE C@ 7 AND 4 =                                72 check
   seed LATE C!  LATE C@ 7 AND 7 =                                  73 check

   \ 8x: the serial port
   $70 OSCCON C!  SERIAL-INIT
   ." forth " seed . -123 . 60000 U. $A5 H. CR
   S" ok" TYPE  [CHAR] ! EMIT  CR

   \ 9x: an interrupt handler, and a delay it interrupts
   $08 OPTION_REG C!  INTCON TMR0IE BIT-SET  EI
   BEGIN ticks C@ 3 < 0= UNTIL
   2 MS  seed MS
   DI  ticks C@ 3 > 0=  0=                                          90 check

   0 TRISD C!
   failed @ ?DUP 0= IF $5A THEN LATD C!
   SLEEP ;
