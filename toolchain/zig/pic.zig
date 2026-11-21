//! The PIC16F1 LLVM toolchain's library for Zig: the device's registers,
//! what a program needs to start, set its configuration words and handle
//! interrupts, and a small hardware library for the PIC16F193x (pins,
//! delays, the serial port, the A/D converter, the data EEPROM, timer 0),
//! the same set as C++'s <pic/pic.hpp> and Rust's pic16f1::hal.
//!
//!     const pic = @import("pic");
//!     pub const panic = pic.panic;            // for the modes with safety checks
//!     const Led = pic.Pin('E', 0);
//!     const wait = pic.Delay(500_000);        // the oscillator, in Hz
//!
//!     comptime {
//!         pic.config("FOSC=INTOSC, WDTE=OFF, MCLRE=ON, LVP=ON");
//!     }
//!
//!     export fn main() noreturn {
//!         Led.digital();
//!         Led.output();
//!         while (true) {
//!             Led.toggle();                   // one XORWF
//!             wait.ms(250);
//!         }
//!     }
//!
//! An interrupt handler is `export fn isr() callconv(.avr_interrupt) void`
//! (Zig is compiled for a stand-in target, and its name shows there). See
//! docs/languages.md in the toolchain's repository.

/// What a failed safety check does in the Debug and ReleaseSafe modes:
/// stop the processor (a trap, which here is a call of `abort`). Zig's own
/// handler prints a message and a stack trace, which needs an operating
/// system and does not compile for a 16-bit machine, so a program built in
/// those modes must say in its root file
///     pub const panic = pic.panic;
/// (or give a handler of its own). ReleaseSmall and ReleaseFast have no
/// safety checks and need nothing.
pub const panic = @import("std").debug.no_panic;

/// The registers of the device `-mcpu=` names: `pic.device.LATE.* = 3;`,
/// `pic.device.INTCONbits.GIE = 1;`.
pub const device = @import("device");

extern fn __delay_loops(loops: u32) void;
extern fn __pic16f1_sleep() void;
extern fn __pic16f1_nop() void;
extern fn __pic16f1_clrwdt() void;

/// The SLEEP instruction.
pub inline fn sleep() void {
    __pic16f1_sleep();
}
/// The NOP instruction.
pub inline fn nop() void {
    __pic16f1_nop();
}
/// The CLRWDT instruction.
pub inline fn clearWatchdog() void {
    __pic16f1_clrwdt();
}

/// At least `cycles` instruction cycles, and up to a dozen more (never
/// fewer than 16). Interrupts lengthen it.
pub inline fn delayCycles(cycles: u32) void {
    // The toolchain's delay loop takes ten cycles a turn, and seven to
    // call and return.
    __delay_loops(if (cycles > 16) (cycles - 7) / 10 else 0);
}

/// Settings of the device's configuration words, by the names the data
/// sheet and XC8's `#pragma config` use. Call it in a `comptime` block.
/// The linker makes the words, and refuses a name or value the device does
/// not have; a setting not given keeps its erased value.
pub fn config(comptime settings: []const u8) void {
    comptime {
        var text: []const u8 = "zig: ";
        for (settings) |c| {
            if (c != ' ') text = text ++ &[_]u8{c};
        }
        // A name of its own for each use.
        var hash: u32 = 2166136261;
        for (text) |c| hash = (hash ^ c) *% 16777619;
        var name: []const u8 = "__pic16f1_config_";
        for (0..8) |i| name = name ++ &[_]u8{"0123456789abcdef"[(hash >> (28 - 4 * i)) & 15]};
        const final = text;
        const Record = struct {
            const bytes: [final.len:0]u8 = final[0..final.len].* ++ [_:0]u8{};
        };
        @export(&Record.bytes, .{ .name = name, .section = ".pic16f1.config" });
    }
}

/// Interrupts on (GIE).
pub inline fn enableInterrupts() void {
    device.INTCONbits.GIE = 1;
}
/// Interrupts off; returns whether they were on, for `restoreInterrupts`:
///     const were = pic.disableInterrupts();
///     defer pic.restoreInterrupts(were);
pub inline fn disableInterrupts() bool {
    const were = device.INTCONbits.GIE == 1;
    device.INTCONbits.GIE = 0;
    return were;
}
pub inline fn restoreInterrupts(were: bool) void {
    if (were) device.INTCONbits.GIE = 1;
}

/// One pin, known at compile time: `Pin('E', 0)` is RE0. Every operation
/// is one bit instruction on a register.
pub fn Pin(comptime port: u8, comptime bit: u3) type {
    return struct {
        const mask: u8 = @as(u8, 1) << bit;
        const port_reg: *volatile u8 = switch (port) {
            'A' => device.PORTA,
            'B' => device.PORTB,
            'C' => device.PORTC,
            'D' => device.PORTD,
            'E' => device.PORTE,
            else => @compileError("the PIC16F193x has ports A to E"),
        };
        const tris: *volatile u8 = switch (port) {
            'A' => device.TRISA,
            'B' => device.TRISB,
            'C' => device.TRISC,
            'D' => device.TRISD,
            'E' => device.TRISE,
            else => unreachable,
        };
        const lat: *volatile u8 = switch (port) {
            'A' => device.LATA,
            'B' => device.LATB,
            'C' => device.LATC,
            'D' => device.LATD,
            'E' => device.LATE,
            else => unreachable,
        };

        pub inline fn output() void {
            tris.* &= ~mask;
        }
        pub inline fn input() void {
            tris.* |= mask;
        }
        /// A pin with an analog function reads 0 until it is made digital.
        pub inline fn digital() void {
            ansel().* &= ~mask;
        }
        pub inline fn analog() void {
            ansel().* |= mask;
        }
        pub inline fn set() void {
            lat.* |= mask;
        }
        pub inline fn clear() void {
            lat.* &= ~mask;
        }
        pub inline fn toggle() void {
            lat.* ^= mask;
        }
        pub inline fn write(on: bool) void {
            if (on) set() else clear();
        }
        pub inline fn read() bool {
            return port_reg.* & mask != 0;
        }
        /// What the pin is driven to, as opposed to what it reads.
        pub inline fn driven() bool {
            return lat.* & mask != 0;
        }
        // Port C has no analog inputs, so no ANSELC: naming it for a pin of
        // port C is refused when the program is compiled.
        inline fn ansel() *volatile u8 {
            return comptime switch (port) {
                'A' => device.ANSELA,
                'B' => device.ANSELB,
                'D' => device.ANSELD,
                'E' => device.ANSELE,
                else => @compileError("port C has no analog inputs"),
            };
        }
    };
}

/// The internal oscillator's settings.
pub const Clock = enum(u8) {
    /// As out of reset.
    hz500k = 0x38,
    mhz1 = 0x58,
    mhz2 = 0x60,
    mhz4 = 0x68,
    mhz8 = 0x70,
    mhz16 = 0x78,
};
pub inline fn setClock(clock: Clock) void {
    device.OSCCON.* = @intFromEnum(clock);
}

/// Delays for an oscillator of `xtal` Hz: `const wait = pic.Delay(8_000_000);`
/// then `wait.ms(500)`. At least the time asked for, and up to a dozen
/// instruction cycles more; interrupts lengthen it.
pub fn Delay(comptime xtal: u32) type {
    return struct {
        pub inline fn ms(n: u16) void {
            delayCycles(@as(u32, n) * (xtal / 4000));
        }
        pub inline fn us(n: u32) void {
            delayCycles(n * (xtal / 4000) / 1000);
        }
    };
}

/// The serial port (EUSART, pins RC6 = TX and RC7 = RX) for an oscillator
/// of `xtal` Hz: 8 data bits, no parity, one stop bit, polled.
pub fn Uart(comptime xtal: u32) type {
    return struct {
        pub fn begin(baud: u32) void {
            const divisor: u16 = @intCast((xtal / 4 + baud / 2) / baud - 1);
            device.BAUDCONbits.BRG16 = 1;
            device.TXSTAbits.BRGH = 1;
            device.SP1BRGL.* = @truncate(divisor);
            device.SP1BRGH.* = @truncate(divisor >> 8);
            device.TRISCbits.TRISC7 = 1;
            device.RCSTAbits.SPEN = 1;
            device.TXSTAbits.TXEN = 1;
            device.RCSTAbits.CREN = 1;
        }
        pub inline fn canWrite() bool {
            return device.PIR1bits.TXIF == 1;
        }
        pub fn write(byte: u8) void {
            while (device.PIR1bits.TXIF == 0) {}
            device.TXREG.* = byte;
        }
        pub fn writeAll(text: []const u8) void {
            for (text) |c| write(c);
        }
        /// Until the last byte has left the pin.
        pub fn flush() void {
            while (device.TXSTAbits.TRMT == 0) {}
        }
        pub inline fn available() bool {
            return device.PIR1bits.RCIF == 1;
        }
        /// Waits for a byte. An overrun, which stops the receiver, is cleared.
        pub fn read() u8 {
            if (device.RCSTAbits.OERR == 1) {
                device.RCSTAbits.CREN = 0;
                device.RCSTAbits.CREN = 1;
            }
            while (device.PIR1bits.RCIF == 0) {}
            return device.RCREG.*;
        }
        /// A number in decimal, without std.fmt.
        pub fn print(value: u16) void {
            var digits: [5]u8 = undefined;
            var n: u8 = 0;
            var v = value;
            while (true) {
                digits[n] = '0' + @as(u8, @intCast(v % 10));
                n += 1;
                v /= 10;
                if (v == 0) break;
            }
            while (n > 0) {
                n -= 1;
                write(digits[n]);
            }
        }
        pub fn printHex(value: u8) void {
            const hex = "0123456789ABCDEF";
            write(hex[value >> 4]);
            write(hex[value & 15]);
        }
    };
}

/// The A/D converter, 10 bits.
pub const Adc = struct {
    pub const temperature: u5 = 29;
    pub const dac: u5 = 30;
    pub const fixed_reference: u5 = 31;

    /// Right-justified results, the converter's own RC clock (which runs
    /// at any oscillator setting), VDD and VSS as references.
    pub fn begin() void {
        device.ADCON1.* = 0xF0;
        device.ADCON0bits.ADON = 1;
    }
    /// The fixed voltage reference as an input to measure against: 1 gives
    /// 1.024 V, 2 gives 2.048 V, 3 gives 4.096 V. Waits until it is stable.
    pub fn fixedReferenceOn(gain: u2) void {
        device.FVRCONbits.ADFVR = gain;
        device.FVRCONbits.FVREN = 1;
        while (device.FVRCONbits.FVRRDY == 0) {}
    }
    /// Channel 0-13 is pin AN0-AN13, which must be an analog input.
    pub fn read(channel: u5) u16 {
        device.ADCON0bits.CHS = channel;
        // The sampling capacitor needs about 5 us to follow the new input.
        for (0..8) |_| nop();
        // A conversion takes at most 200 instruction cycles. One that has
        // not finished in several times that never will: the PIC16F193x
        // can lose one (errata DS80000479: with the converter's own RC
        // clock when not asleep, or above 8 MHz). It is stopped and
        // started again.
        while (true) {
            device.ADCON0bits.GO_nDONE = 1;
            var patience: u8 = 255;
            while (patience != 0) : (patience -= 1) {
                if (device.ADCON0bits.GO_nDONE == 0) return device.ADRES.*;
            }
            device.ADCON0bits.GO_nDONE = 0;
        }
    }
};

/// The data EEPROM, 256 bytes.
pub const Eeprom = struct {
    pub fn read(address: u8) u8 {
        device.EEADRL.* = address;
        device.EECON1bits.EEPGD = 0;
        device.EECON1bits.CFGS = 0;
        device.EECON1bits.RD = 1;
        return device.EEDATL.*;
    }
    /// Takes about 4 ms and waits for it. Interrupts are off for the
    /// instructions that unlock the write.
    pub fn write(address: u8, value: u8) void {
        device.EEADRL.* = address;
        device.EEDATL.* = value;
        device.EECON1bits.EEPGD = 0;
        device.EECON1bits.CFGS = 0;
        device.EECON1bits.WREN = 1;
        {
            const were = disableInterrupts();
            defer restoreInterrupts(were);
            device.EECON2.* = 0x55;
            device.EECON2.* = 0xAA;
            device.EECON1bits.WR = 1;
        }
        while (device.EECON1bits.WR == 1) {}
        device.EECON1bits.WREN = 0;
    }
    /// Writes only if the byte differs: an EEPROM cell wears out.
    pub fn update(address: u8, value: u8) void {
        if (read(address) != value) write(address, value);
    }
};

/// Timer 0 as a free-running counter.
pub const Timer0 = struct {
    /// Counts instruction cycles divided by 2^(shift + 1). With
    /// `interrupts`, the handler is entered each time it wraps (256
    /// counts).
    pub fn begin(shift: u3, interrupts: bool) void {
        device.OPTION_REG.* = (device.OPTION_REG.* & 0xC0) | shift;
        device.TMR0.* = 0;
        device.INTCONbits.TMR0IF = 0;
        device.INTCONbits.TMR0IE = @intFromBool(interrupts);
    }
    pub inline fn count() u8 {
        return device.TMR0.*;
    }
    /// True once for each wrap, for a handler or a polling loop.
    pub inline fn wrapped() bool {
        if (device.INTCONbits.TMR0IF == 0) return false;
        device.INTCONbits.TMR0IF = 0;
        return true;
    }
};
