//! The device library `pic16f1::hal` on the simulator: pins, delays, the
//! serial port and a timer interrupt. (The A/D converter and the EEPROM
//! are checked on the part: tests/rust/hardware.) Prints to the serial
//! port and reports through LATD. For scripts/test-rust.sh.
#![no_std]
#![no_main]
#![feature(abi_avr_interrupt)]

use core::ptr::{addr_of, addr_of_mut, read_volatile, write_volatile};
use pic16f1::device::*;
use pic16f1::hal::{set_clock, Clock, Delay, Pin, Timer0, Uart};

pic16f1::config!(FOSC = INTOSC, WDTE = OFF, MCLRE = ON, LVP = ON);

type Led0 = Pin<'E', 0>;
type Led1 = Pin<'E', 1>;
type Led3 = Pin<'D', 1>;
type Serial = Uart<8_000_000>;
type Wait = Delay<8_000_000>;

static mut TICKS: u8 = 0;
fn ticks() -> u8 {
    unsafe { read_volatile(addr_of!(TICKS)) }
}

#[no_mangle]
pub unsafe extern "avr-interrupt" fn isr() {
    if Timer0::wrapped() {
        write_volatile(addr_of_mut!(TICKS), ticks().wrapping_add(1));
    }
}

pic16f1::entry!(main);
fn main() -> ! {
    let mut failed = 0;
    set_clock(Clock::MHz8);
    Led0::digital();
    Led1::digital();
    Led0::output();
    Led1::output();
    Led3::output();
    Led0::set();
    Led1::clear();
    Led1::toggle();
    Led3::write(true);
    // (LATE3 has no latch; the device data says the bit reads 1.)
    if LATE.read() & 7 != 0x03 || !Led0::driven() || !Led3::driven() || TRISE.read() & 3 != 0 || TRISD.read() & 2 != 0 {
        failed = 1;
    }
    Led0::write(false);
    if LATE.read() & 7 != 0x02 {
        failed = 2;
    }

    Serial::begin(9600);
    if SP1BRG.read() != 207 {
        failed = 3; // 8 MHz / (4 * 9600) - 1, rounded
    }
    Serial::write_str("pins ");
    Serial::print(12345);
    Serial::write(b' ');
    Serial::print_hex(0xA5);
    Serial::write(b'\n');
    Serial::flush();

    Timer0::begin(0, true);
    pic16f1::enable_interrupts();
    while ticks() < 2 {}
    pic16f1::critical(|| {
        let before = ticks();
        Wait::ms(1); // 2,000 cycles: the timer wraps several times
        if ticks() != before {
            failed = 4;
        }
    });
    if !intcon::GIE.is_set() {
        failed = 5;
    }
    pic16f1::disable_interrupts();

    TRISD.write(0);
    LATD.write(if failed != 0 { failed } else { 0x5A });
    loop {
        pic16f1::sleep();
    }
}
