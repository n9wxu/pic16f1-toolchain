/* @NAME@: a starting point for the @DEVICE_UPPER@.
 *
 * Counts on the four LEDs of the F1 Evaluation Platform (RE0, RE1, RE2 and
 * RD1) about twice a second. Change it into your program. */

/* Configuration: internal oscillator, watchdog off, MCLR and low-voltage
 * programming on. (Erased configuration words select an external clock,
 * and the part does not run.) */
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON

#include <xc.h>
#include <stdint.h>

/* The oscillator's frequency, for the delays: 500 kHz is what the internal
 * oscillator runs at out of reset. */
#define _XTAL_FREQ 500000UL

static uint8_t count;

static void show(uint8_t n)
{
    /* The low three bits go to RE0-RE2, bit 3 to RD1. LATE is written
     * whole; of LATD only the one bit, through the bit-field view. */
    LATE = n & 0x07;
    LATDbits.LATD1 = (n >> 3) & 1;
}

void main(void)
{
    ANSELD = 0;                 /* the LED pins are digital ... */
    ANSELE = 0;
    LATD = 0;
    LATE = 0;
    TRISDbits.TRISD1 = 0;       /* ... and outputs */
    TRISE = 0xF8;

    for (;;) {
        show(count);
        count++;
        __delay_ms(500);
    }
}
