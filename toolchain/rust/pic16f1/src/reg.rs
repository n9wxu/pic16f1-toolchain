//! Special function registers: a byte or a 16-bit pair at a fixed address
//! in data memory, read and written with volatile accesses, and fields of
//! one or more bits within a byte.
use core::ptr::{read_volatile, write_volatile};

/// A register of one byte.
#[derive(Clone, Copy)]
pub struct Reg8(u16);

impl Reg8 {
    pub const fn at(address: u16) -> Self {
        Reg8(address)
    }
    pub const fn address(self) -> u16 {
        self.0
    }
    #[inline(always)]
    pub fn read(self) -> u8 {
        unsafe { read_volatile(self.0 as *const u8) }
    }
    #[inline(always)]
    pub fn write(self, value: u8) {
        unsafe { write_volatile(self.0 as *mut u8, value) }
    }
    /// Read, change, write.
    #[inline(always)]
    pub fn modify(self, f: impl FnOnce(u8) -> u8) {
        self.write(f(self.read()))
    }
    /// With a constant `mask` of one bit this is a single `BSF`.
    #[inline(always)]
    pub fn set_bits(self, mask: u8) {
        self.write(self.read() | mask)
    }
    /// With a constant `mask` of one bit this is a single `BCF`.
    #[inline(always)]
    pub fn clear_bits(self, mask: u8) {
        self.write(self.read() & !mask)
    }
    #[inline(always)]
    pub fn toggle_bits(self, mask: u8) {
        self.write(self.read() ^ mask)
    }
}

/// A 16-bit register pair, low byte first.
#[derive(Clone, Copy)]
pub struct Reg16(u16);

impl Reg16 {
    pub const fn at(address: u16) -> Self {
        Reg16(address)
    }
    #[inline(always)]
    pub fn read(self) -> u16 {
        unsafe { read_volatile(self.0 as *const u16) }
    }
    #[inline(always)]
    pub fn write(self, value: u16) {
        unsafe { write_volatile(self.0 as *mut u16, value) }
    }
}

/// One or more adjacent bits of a register.
#[derive(Clone, Copy)]
pub struct Field {
    reg: Reg8,
    shift: u8,
    mask: u8,
}

impl Field {
    pub const fn new(reg: Reg8, bit: u8, width: u8) -> Self {
        Field { reg, shift: bit, mask: (((1u16 << width) - 1) as u8) << bit }
    }
    /// The value of the field, shifted down.
    #[inline(always)]
    pub fn read(self) -> u8 {
        (self.reg.read() & self.mask) >> self.shift
    }
    /// Writes the field and leaves the rest of the register as it was.
    #[inline(always)]
    pub fn write(self, value: u8) {
        self.reg.write((self.reg.read() & !self.mask) | ((value << self.shift) & self.mask))
    }
    /// Any bit of the field is 1.
    #[inline(always)]
    pub fn is_set(self) -> bool {
        self.reg.read() & self.mask != 0
    }
    /// All bits of the field to 1: `BSF` for a one-bit field.
    #[inline(always)]
    pub fn set(self) {
        self.reg.set_bits(self.mask)
    }
    /// All bits of the field to 0: `BCF` for a one-bit field.
    #[inline(always)]
    pub fn clear(self) {
        self.reg.clear_bits(self.mask)
    }
    #[inline(always)]
    pub fn toggle(self) {
        self.reg.toggle_bits(self.mask)
    }
}
