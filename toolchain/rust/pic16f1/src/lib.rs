//! Registers and a small hardware library for PIC16F1 microcontrollers.
//!
//! For programs built with the PIC16F1 LLVM toolchain, where rustc runs for
//! a stand-in target and the toolchain's own code generator makes the
//! program (see `docs/languages.md` in the toolchain's repository).
//!
//! ```ignore
//! #![no_std]
//! #![no_main]
//! use pic16f1::hal::{Delay, Pin};
//!
//! pic16f1::config!(FOSC = INTOSC, WDTE = OFF, MCLRE = ON, LVP = ON);
//!
//! type Led = Pin<'E', 0>;
//! type Wait = Delay<500_000>;         // the oscillator, in Hz
//!
//! pic16f1::entry!(main);
//! fn main() -> ! {
//!     Led::digital();
//!     Led::output();
//!     loop {
//!         Led::toggle();
//!         Wait::ms(500);
//!     }
//! }
//! ```
//!
//! [`device`] has every register of the device chosen by a cargo feature
//! (`pic16f1937` by default), [`reg`] the types they are made of, and
//! [`hal`] pins, delays, the serial port, the A/D converter, the EEPROM
//! and timer 0 for the PIC16F193x.
#![no_std]

pub mod reg;

mod devices {
    #[cfg(feature = "pic16f1032")]
    pub mod pic16f1032;
    #[cfg(feature = "pic16f1064")]
    pub mod pic16f1064;
    #[cfg(feature = "pic16f15276")]
    pub mod pic16f15276;
    #[cfg(feature = "pic16f1619")]
    pub mod pic16f1619;
    #[cfg(feature = "pic16f1829")]
    pub mod pic16f1829;
    #[cfg(feature = "pic16f18877")]
    pub mod pic16f18877;
    #[cfg(feature = "pic16f1937")]
    pub mod pic16f1937;
}

/// The registers of the device.
#[cfg(feature = "pic16f1937")]
pub use devices::pic16f1937 as device;
#[cfg(all(feature = "pic16f1829", not(feature = "pic16f1937")))]
pub use devices::pic16f1829 as device;
#[cfg(all(feature = "pic16f1619", not(feature = "pic16f1937")))]
pub use devices::pic16f1619 as device;
#[cfg(all(feature = "pic16f18877", not(feature = "pic16f1937")))]
pub use devices::pic16f18877 as device;
#[cfg(all(feature = "pic16f15276", not(feature = "pic16f1937")))]
pub use devices::pic16f15276 as device;
#[cfg(all(feature = "pic16f1032", not(feature = "pic16f1937")))]
pub use devices::pic16f1032 as device;
#[cfg(all(feature = "pic16f1064", not(feature = "pic16f1937")))]
pub use devices::pic16f1064 as device;

#[cfg(any(feature = "pic16f1937", feature = "pic16f1032"))]
pub mod hal;

/// Stops the processor: `SLEEP`, for good.
#[cfg(feature = "panic-halt")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {
        sleep();
    }
}

extern "C" {
    fn __delay_loops(loops: u32);
}

/// At least `cycles` instruction cycles, and up to a dozen more (never
/// fewer than 16). Interrupts lengthen it.
#[inline(always)]
pub fn delay_cycles(cycles: u32) {
    // The toolchain's delay loop takes ten cycles a turn, and seven to
    // call and return.
    unsafe { __delay_loops(if cycles > 16 { (cycles - 7) / 10 } else { 0 }) }
}

extern "C" {
    fn __pic16f1_sleep();
    fn __pic16f1_nop();
    fn __pic16f1_clrwdt();
}

/// The `SLEEP` instruction.
#[inline(always)]
pub fn sleep() {
    unsafe { __pic16f1_sleep() }
}
/// The `NOP` instruction.
#[inline(always)]
pub fn nop() {
    unsafe { __pic16f1_nop() }
}
/// The `CLRWDT` instruction.
#[inline(always)]
pub fn clear_watchdog() {
    unsafe { __pic16f1_clrwdt() }
}

/// Makes `$main`, a `fn() -> !`, the function the startup code calls.
#[macro_export]
macro_rules! entry {
    ($main:path) => {
        const _: () = {
            #[export_name = "main"]
            pub extern "C" fn __pic16f1_entry() -> ! {
                let f: fn() -> ! = $main;
                f()
            }
        };
    };
}

/// Settings of the device's configuration words, by the names the data
/// sheet and XC8's `#pragma config` use:
/// `pic16f1::config!(FOSC = INTOSC, WDTE = OFF);`. The linker makes the
/// words, and refuses a name or value the device does not have. A setting
/// not given keeps its erased value. May be used more than once, each
/// time on a line of its own.
#[macro_export]
macro_rules! config {
    ($n0:ident = $v0:tt $(, $n:ident = $v:tt)* $(,)?) => {
        const _: () = {
            const TEXT: &str = concat!(
                file!(), ":", line!(), ": ",
                stringify!($n0), "=", stringify!($v0)
                $(, ",", stringify!($n), "=", stringify!($v))*,
                "\0"
            );
            #[link_section = ".pic16f1.config"]
            #[used]
            static RECORD: [u8; TEXT.len()] = $crate::__bytes(TEXT);
        };
    };
}

/// Eight bytes of initial data EEPROM content, each use following the one
/// before from the start of the EEPROM (one use to a line), as XC8's
/// `__EEPROM_DATA`.
#[macro_export]
macro_rules! eeprom_data {
    ($($b:expr),* $(,)?) => {
        const _: () = {
            #[link_section = ".eeprom"]
            #[used]
            static DATA: [u16; 8] = [$(($b) as u8 as u16),*];
        };
    };
}

#[doc(hidden)]
pub const fn __bytes<const N: usize>(text: &str) -> [u8; N] {
    let bytes = text.as_bytes();
    let mut out = [0u8; N];
    let mut i = 0;
    while i < N {
        out[i] = bytes[i];
        i += 1;
    }
    out
}

/// Interrupts on (`GIE`).
#[inline(always)]
pub fn enable_interrupts() {
    device::intcon::GIE.set();
}
/// Interrupts off.
#[inline(always)]
pub fn disable_interrupts() {
    device::intcon::GIE.clear();
}
/// Runs `f` with interrupts off, and puts them back as they were.
#[inline(always)]
pub fn critical<R>(f: impl FnOnce() -> R) -> R {
    let was = device::intcon::GIE.is_set();
    device::intcon::GIE.clear();
    let r = f();
    if was {
        device::intcon::GIE.set();
    }
    r
}
