//! Rust on the part itself: a call through a trait object, and the device
//! library where the simulator cannot check it: the length of a delay
//! against a timer, the data EEPROM, and the A/D converter against the
//! fixed voltage reference. For a PIC16(L)F1937 on the F1 Evaluation
//! Platform; run by tools/hwtest.py, which reads the code this leaves in
//! LATD. Build with link arguments -g and -mdebugger.
#![no_std]
#![no_main]

use core::ptr::{addr_of, addr_of_mut, read_volatile, write_volatile};
use pic16f1::device::*;
use pic16f1::hal::{Adc, Delay, Eeprom, Pin, Timer0};

pic16f1::config!(FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON);
pic16f1::config!(BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF);
pic16f1::config!(WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON);

type Led0 = Pin<'E', 0>;
type Led1 = Pin<'E', 1>;
type Led2 = Pin<'E', 2>;
type Wait = Delay<500_000>;

trait Source {
    fn next(&mut self) -> u8;
}
struct Counter(u8);
impl Source for Counter {
    fn next(&mut self) -> u8 {
        let v = self.0;
        self.0 += 1;
        v
    }
}
struct Fixed;
impl Source for Fixed {
    fn next(&mut self) -> u8 {
        0
    }
}

// What the checks measured, for the report.
#[no_mangle]
pub static mut timer_ticks: u8 = 0;
#[no_mangle]
pub static mut reference: u16 = 0;
#[no_mangle]
pub static mut stored: [u8; 4] = [0; 4];
static mut USE_COUNTER: bool = true;

#[no_mangle]
#[inline(never)]
pub extern "C" fn hw_done() -> ! {
    loop {
        Led2::toggle();
        Wait::ms(250);
    }
}

pic16f1::entry!(main);
fn main() -> ! {
    let mut failed = 0;
    TRISD.write(0);
    LATD.write(0xFF);
    Led0::digital();
    Led1::digital();
    Led2::digital();
    Led0::output();
    Led1::output();
    Led2::output();
    Led0::set();
    Led1::clear();
    if !Led0::driven() || Led1::driven() {
        failed = 1;
    }

    // A call through a table of functions.
    let mut counter = Counter(0x30);
    let mut fixed = Fixed;
    let source: &mut dyn Source =
        if unsafe { read_volatile(addr_of!(USE_COUNTER)) } { &mut counter } else { &mut fixed };
    if source.next() != 0x30 || source.next() != 0x31 {
        failed = 2;
    }

    // 100 ms against timer 0: 125,000 instruction cycles a second, divided
    // by 256, is a count every 2.048 ms, so about 49 counts.
    Timer0::begin(7, false);
    Wait::ms(100);
    let ticks = Timer0::count();
    unsafe { write_volatile(addr_of_mut!(timer_ticks), ticks) };
    if !(47..=52).contains(&ticks) {
        failed = 3;
    }

    // The EEPROM keeps what is written.
    for i in 0..4u8 {
        Eeprom::update(0x50 + i, source.next());
    }
    for i in 0..4u8 {
        let v = Eeprom::read(0x50 + i);
        unsafe { write_volatile(addr_of_mut!(stored[i as usize]), v) };
        if v != 0x32 + i {
            failed = 4;
        }
    }

    // The 1.024 V reference measured against the supply: between 1.8 V and
    // 5.5 V that is a reading of 190 to 582.
    Adc::begin();
    Adc::fixed_reference_on(1);
    let r = Adc::read(Adc::FIXED_REFERENCE);
    unsafe { write_volatile(addr_of_mut!(reference), r) };
    if !(190..=582).contains(&r) {
        failed = 5;
    }

    LATD.write(if failed != 0 { failed } else { 0x5A });
    hw_done()
}
