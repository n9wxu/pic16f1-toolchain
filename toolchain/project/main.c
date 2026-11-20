/* @NAME@: a starting point for the @DEVICE_UPPER@.
 *
 * Counts on the four LEDs of the F1 Evaluation Platform (RE0, RE1, RE2 and
 * RD1) about twice a second. Change it into your program. */
#include <xc.h>
#include <stdint.h>

/* Configuration words: internal oscillator, watchdog off, MCLR and
 * low-voltage programming on. (Erased configuration words select an
 * external clock, and the part does not run.) This toolchain takes them
 * this way and not from #pragma config. */
PIC_CONFIG_WORDS(_CONFIG1_FOSC_INTOSC & _CONFIG1_WDTE_OFF & _CONFIG1_PWRTE_ON &
                     _CONFIG1_MCLRE_ON & _CONFIG1_BOREN_OFF & _CONFIG1_CLKOUTEN_OFF &
                     _CONFIG1_IESO_OFF & _CONFIG1_FCMEN_OFF,
                 _CONFIG2_WRT_OFF & _CONFIG2_PLLEN_OFF & _CONFIG2_STVREN_ON &
                     _CONFIG2_BORV_LO & _CONFIG2_LVP_ON);

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
