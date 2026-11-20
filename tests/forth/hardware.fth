\ Forth on the part itself: what the simulator cannot check. The length of
\ a delay against a timer, the data EEPROM, and the A/D converter against
\ the fixed voltage reference, all through the registers by their data
\ sheet names. For a PIC16(L)F1937 on the F1 Evaluation Platform; run by
\ tools/hwtest.py, which reads the code this leaves in LATD.

CONFIG" FOSC=INTOSC, WDTE=OFF, PWRTE=ON, MCLRE=ON, BOREN=OFF, CLKOUTEN=OFF, IESO=OFF, FCMEN=OFF"
CONFIG" WRT=OFF, PLLEN=OFF, STVREN=ON, BORV=LO, LVP=ON"
500000 CLOCK-HZ

VARIABLE failed
VARIABLE timer_ticks
VARIABLE reference

: fail ( number -- )  failed @ IF DROP ELSE failed ! THEN ;

: ee@ ( address -- byte )
   EEADRL C!  EECON1 EEPGD BIT-CLR  EECON1 CFGS BIT-CLR  EECON1 RD BIT-SET  EEDATL C@ ;
: ee! ( byte address -- )
   EEADRL C!  EEDATL C!  EECON1 EEPGD BIT-CLR  EECON1 CFGS BIT-CLR  EECON1 WREN BIT-SET
   $55 EECON2 C!  $AA EECON2 C!  EECON1 WR BIT-SET
   BEGIN EECON1 WR BIT? 0= UNTIL  EECON1 WREN BIT-CLR ;
: ee-update ( byte address -- )  2DUP ee@ = IF 2DROP ELSE ee! THEN ;

: adc-on ( -- )  $F0 ADCON1 C!  ADCON0 ADON BIT-SET ;
: reference-on ( -- )  $81 FVRCON C!  BEGIN FVRCON FVRRDY BIT? UNTIL ;
: adc@ ( channel -- value )
   2* 2* 1 OR ADCON0 C!  8 0 DO NOP LOOP
   ADCON0 GO_nDONE BIT-SET  BEGIN ADCON0 GO_nDONE BIT? 0= UNTIL  ADRESL @ ;

: hw_done  BEGIN LATE 2 BIT-TOGGLE 250 MS AGAIN ; EXPORT

: MAIN
   0 TRISD C!  $FF LATD C!  0 ANSELE C!  $F8 TRISE C!
   LATE 0 BIT-SET  LATE 1 BIT-CLR
   LATE 0 BIT?  LATE 1 BIT? 0= AND  0= IF 1 fail THEN

   \ 100 ms against timer 0: 125,000 instruction cycles a second, divided
   \ by 256, is a count every 2.048 ms, so about 49 counts.
   OPTION_REG C@ $C0 AND 7 OR OPTION_REG C!  0 TMR0 C!
   100 MS  TMR0 C@ DUP timer_ticks !
   47 53 WITHIN 0= IF 3 fail THEN

   \ The EEPROM keeps what is written.
   4 0 DO  I $32 + I $70 + ee-update  LOOP
   4 0 DO  I $70 + ee@ I $32 + <> IF 4 fail THEN  LOOP

   \ The 1.024 V reference measured against the supply: between 1.8 V and
   \ 5.5 V that is a reading of 190 to 582.
   adc-on reference-on  31 adc@ DUP reference !
   190 583 WITHIN 0= IF 5 fail THEN

   failed @ ?DUP 0= IF $5A THEN LATD C!
   hw_done ;
