//! Pins, delays, the serial port, the A/D converter, the data EEPROM and
//! timer 0 of the PIC16F193x (written for the PIC16F1937). The same set
//! as the C++ library `<pic/pic.hpp>`. Nothing here takes RAM, and nothing
//! takes code unless it is used.
use crate::device::*;
use crate::reg::Reg8;

const fn port_reg(port: char, a: Reg8, b: Reg8, c: Reg8, d: Reg8, e: Reg8) -> Reg8 {
    match port {
        'A' => a,
        'B' => b,
        'C' => c,
        'D' => d,
        'E' => e,
        _ => panic!("the PIC16F193x has ports A to E"),
    }
}

/// One pin, known at compile time: `Pin<'E', 0>` is RE0. Every operation
/// is one bit instruction on a register.
pub struct Pin<const PORT: char, const BIT: u8>;

impl<const PORT: char, const BIT: u8> Pin<PORT, BIT> {
    const MASK: u8 = {
        assert!(BIT < 8, "a port has eight pins");
        1 << BIT
    };
    const PORT_REG: Reg8 = port_reg(PORT, PORTA, PORTB, PORTC, PORTD, PORTE);
    const TRIS: Reg8 = port_reg(PORT, TRISA, TRISB, TRISC, TRISD, TRISE);
    const LAT: Reg8 = port_reg(PORT, LATA, LATB, LATC, LATD, LATE);
    // Port C has no analog inputs, so no ANSELC: naming it for a pin of
    // port C is refused when the program is compiled.
    const ANSEL: Reg8 = match PORT {
        'C' => panic!("port C has no analog inputs"),
        _ => port_reg(PORT, ANSELA, ANSELB, ANSELA, ANSELD, ANSELE),
    };

    #[inline(always)]
    pub fn output() {
        Self::TRIS.clear_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn input() {
        Self::TRIS.set_bits(Self::MASK)
    }
    /// A pin with an analog function reads 0 until it is made digital.
    #[inline(always)]
    pub fn digital() {
        Self::ANSEL.clear_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn analog() {
        Self::ANSEL.set_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn set() {
        Self::LAT.set_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn clear() {
        Self::LAT.clear_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn toggle() {
        Self::LAT.toggle_bits(Self::MASK)
    }
    #[inline(always)]
    pub fn write(on: bool) {
        if on {
            Self::set()
        } else {
            Self::clear()
        }
    }
    #[inline(always)]
    pub fn read() -> bool {
        Self::PORT_REG.read() & Self::MASK != 0
    }
    /// What the pin is driven to, as opposed to what it reads.
    #[inline(always)]
    pub fn driven() -> bool {
        Self::LAT.read() & Self::MASK != 0
    }
}

/// The internal oscillator's settings.
#[derive(Clone, Copy)]
#[repr(u8)]
pub enum Clock {
    /// As out of reset.
    Hz500k = 0x38,
    MHz1 = 0x58,
    MHz2 = 0x60,
    MHz4 = 0x68,
    MHz8 = 0x70,
    MHz16 = 0x78,
}
#[inline(always)]
pub fn set_clock(clock: Clock) {
    OSCCON.write(clock as u8)
}

/// Delays for an oscillator of `XTAL` Hz: `type Wait = Delay<8_000_000>;`
/// then `Wait::ms(500)`. At least the time asked for, and up to a dozen
/// instruction cycles more; interrupts lengthen it.
pub struct Delay<const XTAL: u32>;

impl<const XTAL: u32> Delay<XTAL> {
    #[inline(always)]
    pub fn ms(ms: u16) {
        crate::delay_cycles(ms as u32 * (XTAL / 4000))
    }
    #[inline(always)]
    pub fn us(us: u32) {
        crate::delay_cycles(us * (XTAL / 4000) / 1000)
    }
}

/// The serial port (EUSART, pins RC6 = TX and RC7 = RX) for an oscillator
/// of `XTAL` Hz: 8 data bits, no parity, one stop bit, polled.
pub struct Uart<const XTAL: u32>;

impl<const XTAL: u32> Uart<XTAL> {
    pub fn begin(baud: u32) {
        let divisor = ((XTAL / 4 + baud / 2) / baud - 1) as u16;
        baudcon::BRG16.set();
        txsta::BRGH.set();
        SP1BRGL.write(divisor as u8);
        SP1BRGH.write((divisor >> 8) as u8);
        trisc::TRISC7.set();
        rcsta::SPEN.set();
        txsta::TXEN.set();
        rcsta::CREN.set();
    }
    #[inline(always)]
    pub fn can_write() -> bool {
        pir1::TXIF.is_set()
    }
    pub fn write(byte: u8) {
        while !pir1::TXIF.is_set() {}
        TXREG.write(byte)
    }
    pub fn write_str(text: &str) {
        for &b in text.as_bytes() {
            Self::write(b)
        }
    }
    /// Until the last byte has left the pin.
    pub fn flush() {
        while !txsta::TRMT.is_set() {}
    }
    #[inline(always)]
    pub fn available() -> bool {
        pir1::RCIF.is_set()
    }
    /// Waits for a byte. An overrun, which stops the receiver, is cleared.
    pub fn read() -> u8 {
        if rcsta::OERR.is_set() {
            rcsta::CREN.clear();
            rcsta::CREN.set();
        }
        while !pir1::RCIF.is_set() {}
        RCREG.read()
    }
    /// A number in decimal, without `core::fmt` (which is several
    /// thousand words).
    pub fn print(mut value: u16) {
        let mut digits = [0u8; 5];
        let mut n = 0;
        loop {
            digits[n] = b'0' + (value % 10) as u8;
            n += 1;
            value /= 10;
            if value == 0 {
                break;
            }
        }
        while n > 0 {
            n -= 1;
            Self::write(digits[n]);
        }
    }
    pub fn print_hex(value: u8) {
        const HEX: &[u8; 16] = b"0123456789ABCDEF";
        Self::write(HEX[(value >> 4) as usize]);
        Self::write(HEX[(value & 15) as usize]);
    }
}

/// The A/D converter, 10 bits.
pub struct Adc;

impl Adc {
    pub const TEMPERATURE: u8 = 29;
    pub const DAC: u8 = 30;
    pub const FIXED_REFERENCE: u8 = 31;

    /// Right-justified results, the converter's own RC clock (which runs
    /// at any oscillator setting), VDD and VSS as references.
    pub fn begin() {
        ADCON1.write(0xF0);
        adcon0::ADON.set();
    }
    /// The fixed voltage reference as an input to measure against: 1 gives
    /// 1.024 V, 2 gives 2.048 V, 3 gives 4.096 V. Waits until it is stable.
    pub fn fixed_reference_on(gain: u8) {
        fvrcon::ADFVR.write(gain);
        fvrcon::FVREN.set();
        while !fvrcon::FVRRDY.is_set() {}
    }
    /// Channel 0-13 is pin AN0-AN13, which must be an analog input.
    pub fn read(channel: u8) -> u16 {
        adcon0::CHS.write(channel);
        // The sampling capacitor needs about 5 us to follow the new input.
        for _ in 0..8 {
            crate::nop();
        }
        adcon0::GO_nDONE.set();
        while adcon0::GO_nDONE.is_set() {}
        ADRES.read()
    }
}

/// The data EEPROM, 256 bytes.
pub struct Eeprom;

impl Eeprom {
    pub fn read(address: u8) -> u8 {
        EEADRL.write(address);
        eecon1::EEPGD.clear();
        eecon1::CFGS.clear();
        eecon1::RD.set();
        EEDATL.read()
    }
    /// Takes about 4 ms and waits for it. Interrupts are off for the
    /// instructions that unlock the write.
    pub fn write(address: u8, value: u8) {
        EEADRL.write(address);
        EEDATL.write(value);
        eecon1::EEPGD.clear();
        eecon1::CFGS.clear();
        eecon1::WREN.set();
        crate::critical(|| {
            EECON2.write(0x55);
            EECON2.write(0xAA);
            eecon1::WR.set();
        });
        while eecon1::WR.is_set() {}
        eecon1::WREN.clear();
    }
    /// Writes only if the byte differs: an EEPROM cell wears out.
    pub fn update(address: u8, value: u8) {
        if Self::read(address) != value {
            Self::write(address, value)
        }
    }
}

/// Timer 0 as a free-running counter.
pub struct Timer0;

impl Timer0 {
    /// Counts instruction cycles divided by 2^(shift + 1), shift 0-7. With
    /// `interrupts`, the handler is entered each time it wraps (256
    /// counts).
    pub fn begin(shift: u8, interrupts: bool) {
        OPTION_REG.modify(|v| (v & 0xC0) | (shift & 7));
        TMR0.write(0);
        intcon::TMR0IF.clear();
        if interrupts {
            intcon::TMR0IE.set()
        } else {
            intcon::TMR0IE.clear()
        }
    }
    #[inline(always)]
    pub fn count() -> u8 {
        TMR0.read()
    }
    /// True once for each wrap, for a handler or a polling loop.
    #[inline(always)]
    pub fn wrapped() -> bool {
        if !intcon::TMR0IF.is_set() {
            return false;
        }
        intcon::TMR0IF.clear();
        true
    }
}
