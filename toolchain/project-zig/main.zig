//! @NAME@: a starting point for the @DEVICE_UPPER@, in Zig.
//!
//! Counts on the four LEDs of the F1 Evaluation Platform (RE0, RE1, RE2 and
//! RD1) about twice a second. Change it into your program.
const pic = @import("pic");

// What a failed safety check does in the Debug and ReleaseSafe modes.
pub const panic = pic.panic;

// Configuration: internal oscillator, watchdog off, MCLR and low-voltage
// programming on. (Erased configuration words select an external clock,
// and the part does not run.)
comptime {
    pic.config("FOSC=INTOSC, WDTE=OFF, PWRTE=ON, MCLRE=ON, BOREN=OFF, CLKOUTEN=OFF, IESO=OFF, FCMEN=OFF");
    pic.config("WRT=OFF, PLLEN=OFF, STVREN=ON, BORV=LO, LVP=ON");
}

// The four LEDs, each a pin known at compile time: an operation on one is
// a single instruction.
const leds = .{ pic.Pin('E', 0), pic.Pin('E', 1), pic.Pin('E', 2), pic.Pin('D', 1) };

// The oscillator's frequency, for the delays: 500 kHz is what the internal
// oscillator runs at out of reset.
const wait = pic.Delay(500_000);

fn show(n: u8) void {
    inline for (leds, 0..) |Led, bit| Led.write(n >> bit & 1 != 0);
}

export fn main() noreturn {
    inline for (leds) |Led| {
        Led.digital();
        Led.output();
    }
    var count: u8 = 0;
    while (true) : (count +%= 1) {
        show(count);
        wait.ms(500);
    }
}
