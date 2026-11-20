//! Zig on the part itself: a call through a table of functions, and the
//! library where the simulator cannot check it: the length of a delay
//! against a timer, the data EEPROM, and the A/D converter against the
//! fixed voltage reference. For a PIC16(L)F1937 on the F1 Evaluation
//! Platform; run by tools/hwtest.py, which reads the code this leaves in
//! LATD. Build with -g -mdebugger.
const pic = @import("pic");
const dev = pic.device;
pub const panic = pic.panic;

comptime {
    pic.config("FOSC=INTOSC, WDTE=OFF, PWRTE=ON, MCLRE=ON, BOREN=OFF, CLKOUTEN=OFF, IESO=OFF, FCMEN=OFF");
    pic.config("WRT=OFF, PLLEN=OFF, STVREN=ON, BORV=LO, LVP=ON");
}

const Led0 = pic.Pin('E', 0);
const Led1 = pic.Pin('E', 1);
const Led2 = pic.Pin('E', 2);
const wait = pic.Delay(500_000);

const Source = struct {
    state: u8,
    nextFn: *const fn (*Source) u8,
    fn next(self: *Source) u8 {
        return self.nextFn(self);
    }
};
fn counting(self: *Source) u8 {
    defer self.state += 1;
    return self.state;
}
fn fixed(_: *Source) u8 {
    return 0;
}
var use_counter: bool = true;

// What the checks measured, for the report.
export var timer_ticks: u8 = 0;
export var reference: u16 = 0;
export var stored: [4]u8 = .{ 0, 0, 0, 0 };

export fn hw_done() noreturn {
    while (true) {
        Led2.toggle();
        wait.ms(250);
    }
}

export fn main() noreturn {
    var failed: u8 = 0;
    dev.TRISD.* = 0;
    dev.LATD.* = 0xFF;
    Led0.digital();
    Led1.digital();
    Led2.digital();
    Led0.output();
    Led1.output();
    Led2.output();
    Led0.set();
    Led1.clear();
    if (!Led0.driven() or Led1.driven()) failed = 1;

    var source = Source{ .state = 0x30, .nextFn = if (@as(*volatile bool, &use_counter).*) counting else fixed };
    if (source.next() != 0x30 or source.next() != 0x31) failed = 2;

    // 100 ms against timer 0: 125,000 instruction cycles a second, divided
    // by 256, is a count every 2.048 ms, so about 49 counts.
    pic.Timer0.begin(7, false);
    wait.ms(100);
    const ticks = pic.Timer0.count();
    @as(*volatile u8, &timer_ticks).* = ticks;
    if (ticks < 47 or ticks > 52) failed = 3;

    // The EEPROM keeps what is written.
    for (0..4) |i| pic.Eeprom.update(@intCast(0x60 + i), source.next());
    for (0..4) |i| {
        const v = pic.Eeprom.read(@intCast(0x60 + i));
        @as(*volatile u8, &stored[i]).* = v;
        if (v != 0x32 + i) failed = 4;
    }

    // The 1.024 V reference measured against the supply: between 1.8 V and
    // 5.5 V that is a reading of 190 to 582.
    pic.Adc.begin();
    pic.Adc.fixedReferenceOn(1);
    const r = pic.Adc.read(pic.Adc.fixed_reference);
    @as(*align(1) volatile u16, &reference).* = r;
    if (r < 190 or r > 582) failed = 5;

    dev.LATD.* = if (failed != 0) failed else 0x5A;
    hw_done();
}
