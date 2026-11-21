// What runs a sketch: the part's configuration, and main.
#include "Arduino.h"

// The internal oscillator (with its PLL for 32 MHz), no watchdog, the
// MCLR pin a reset, low-voltage programming left possible.
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#if F_CPU == 32000000UL
#pragma config WRT = OFF, PLLEN = ON, STVREN = ON, BORV = LO, LVP = ON
#else
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON
#endif

int main()
{
    init();
    setup();
    for (;;)
        loop();
}
