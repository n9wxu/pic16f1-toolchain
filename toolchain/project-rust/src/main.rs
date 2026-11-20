//! @NAME@: a starting point for the @DEVICE_UPPER@, in Rust.
//!
//! Counts on the four LEDs of the F1 Evaluation Platform (RE0, RE1, RE2 and
//! RD1) about twice a second. Change it into your program.
#![no_std]
#![no_main]

use pic16f1::hal::{Delay, Pin};

// Configuration: internal oscillator, watchdog off, MCLR and low-voltage
// programming on. (Erased configuration words select an external clock,
// and the part does not run.)
pic16f1::config!(FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON);
pic16f1::config!(BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF);
pic16f1::config!(WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON);

// The four LEDs, each a pin known at compile time: an operation on one is
// a single instruction.
type Led0 = Pin<'E', 0>;
type Led1 = Pin<'E', 1>;
type Led2 = Pin<'E', 2>;
type Led3 = Pin<'D', 1>;

// The oscillator's frequency, for the delays: 500 kHz is what the internal
// oscillator runs at out of reset.
type Wait = Delay<500_000>;

fn show(n: u8) {
    Led0::write(n & 1 != 0);
    Led1::write(n & 2 != 0);
    Led2::write(n & 4 != 0);
    Led3::write(n & 8 != 0);
}

pic16f1::entry!(main);
fn main() -> ! {
    Led0::digital();
    Led1::digital();
    Led2::digital();
    Led3::digital();
    Led0::output();
    Led1::output();
    Led2::output();
    Led3::output();
    let mut count: u8 = 0;
    loop {
        show(count);
        count = count.wrapping_add(1);
        Wait::ms(500);
    }
}
