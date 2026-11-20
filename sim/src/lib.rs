//! PIC16F1 enhanced mid-range simulator.
//!
//! Device facts (memory sizes, SFR addresses, bit positions) come from the
//! device database in `devices/`. Only the core register layout that the
//! instruction set itself defines is fixed in code.

pub mod cpu;
pub mod device;
pub mod disasm;
pub mod gdb;
pub mod image;

pub use cpu::{Cpu, Halt, UartMode};
pub use device::Device;
pub use image::Image;
