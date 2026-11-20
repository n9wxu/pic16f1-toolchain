// @NAME@: a starting point for the @DEVICE_UPPER@, in C++.
//
// Counts on the four LEDs of the F1 Evaluation Platform (RE0, RE1, RE2 and
// RD1) about twice a second. Change it into your program.

// Configuration: internal oscillator, watchdog off, MCLR and low-voltage
// programming on. (Erased configuration words select an external clock,
// and the part does not run.)
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON

// The oscillator's frequency, for the delays: 500 kHz is what the internal
// oscillator runs at out of reset.
#define _XTAL_FREQ 500000UL
#include <pic/pic.hpp>

// The four LEDs, each a pin known at compile time: an operation on one is
// a single instruction.
using Led0 = pic::Pin<'E', 0>;
using Led1 = pic::Pin<'E', 1>;
using Led2 = pic::Pin<'E', 2>;
using Led3 = pic::Pin<'D', 1>;

template <typename... Pins> static void each_output()
{
    (Pins::digital(), ...);
    (Pins::output(), ...);
}

static void show(uint8_t n)
{
    Led0::write(n & 1);
    Led1::write(n & 2);
    Led2::write(n & 4);
    Led3::write(n & 8);
}

int main()
{
    each_output<Led0, Led1, Led2, Led3>();
    for (uint8_t count = 0;; count++) {
        show(count);
        pic::delay_ms(500);
    }
}
