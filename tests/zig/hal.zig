//! The Zig library pic.zig on the simulator: pins, delays, the serial port
//! and a timer interrupt. (The A/D converter and the EEPROM are checked on
//! the part: tests/zig/hardware.zig.) Prints to the serial port and
//! reports through LATD. For scripts/test-zig.sh.
const pic = @import("pic");
const dev = pic.device;
pub const panic = pic.panic;

comptime {
    pic.config("FOSC=INTOSC, WDTE=OFF, MCLRE=ON, LVP=ON");
}

const Led0 = pic.Pin('E', 0);
const Led1 = pic.Pin('E', 1);
const Led3 = pic.Pin('D', 1);
const serial = pic.Uart(8_000_000);
const wait = pic.Delay(8_000_000);

var ticks: u8 = 0;
fn tickCount() u8 {
    return @as(*volatile u8, &ticks).*;
}

export fn isr() callconv(.avr_interrupt) void {
    if (pic.Timer0.wrapped()) @as(*volatile u8, &ticks).* = tickCount() +% 1;
}

export fn main() noreturn {
    var failed: u8 = 0;
    pic.setClock(.mhz8);
    Led0.digital();
    Led1.digital();
    Led0.output();
    Led1.output();
    Led3.output();
    Led0.set();
    Led1.clear();
    Led1.toggle();
    Led3.write(true);
    // (LATE3 has no latch; the device data says the bit reads 1.)
    if (dev.LATE.* & 7 != 0x03 or !Led0.driven() or !Led3.driven() or dev.TRISE.* & 3 != 0 or dev.TRISD.* & 2 != 0) failed = 1;
    Led0.write(false);
    if (dev.LATE.* & 7 != 0x02) failed = 2;

    serial.begin(9600);
    if (dev.SP1BRG.* != 207) failed = 3; // 8 MHz / (4 * 9600) - 1, rounded
    serial.writeAll("pins ");
    serial.print(12345);
    serial.write(' ');
    serial.printHex(0xA5);
    serial.write('\n');
    serial.flush();

    pic.Timer0.begin(0, true);
    pic.enableInterrupts();
    while (tickCount() < 2) {}
    {
        const were = pic.disableInterrupts();
        defer pic.restoreInterrupts(were);
        const before = tickCount();
        wait.ms(1); // 2,000 cycles: the timer wraps several times
        if (tickCount() != before) failed = 4;
    }
    if (dev.INTCONbits.GIE == 0) failed = 5;
    _ = pic.disableInterrupts();

    dev.TRISD.* = 0;
    dev.LATD.* = if (failed != 0) failed else 0x5A;
    while (true) pic.sleep();
}
