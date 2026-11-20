//! The Zig language on the PIC16F1, without the standard library at run
//! time: each part checks itself, and the program reports through LATD as
//! the C tests do (0x5A if everything held, otherwise the number of the
//! first failure). For scripts/test-zig.sh.
const pic = @import("pic");
const dev = pic.device;
pub const panic = pic.panic;

var failed: u8 = 0;
fn check(ok: bool, number: u8) void {
    if (!ok and failed == 0) failed = number;
}

/// A value the compiler cannot see through.
var seed_cell: u8 = 7;
noinline fn seed() u8 {
    return @as(*volatile u8, &seed_cell).*;
}

// ---- 1x: structs, methods, defaults ---------------------------------------
const Q8 = struct {
    raw: i16,
    fn init(whole: i16, frac: u8) Q8 {
        return .{ .raw = whole * 256 + frac };
    }
    fn add(a: Q8, b: Q8) Q8 {
        return .{ .raw = a.raw + b.raw };
    }
    fn mul(a: Q8, b: Q8) Q8 {
        return .{ .raw = @intCast((@as(i32, a.raw) * b.raw) >> 8) };
    }
};

// ---- 2x: an interface by hand, and generics --------------------------------
const Shape = struct {
    ptr: *const anyopaque,
    areaFn: *const fn (*const anyopaque) u16,
    sides: u8,
    fn area(self: Shape) u16 {
        return self.areaFn(self.ptr);
    }
};
const Square = struct {
    side: u8,
    fn area(p: *const anyopaque) u16 {
        const self: *const Square = @ptrCast(p);
        return @as(u16, self.side) * self.side;
    }
    fn shape(self: *const Square) Shape {
        return .{ .ptr = self, .areaFn = area, .sides = 4 };
    }
};
const Circle = struct {
    radius: u8,
    fn area(p: *const anyopaque) u16 {
        const self: *const Circle = @ptrCast(p);
        return @as(u16, self.radius) * self.radius * 3;
    }
    fn shape(self: *const Circle) Shape {
        return .{ .ptr = self, .areaFn = area, .sides = 0 };
    }
};
noinline fn total(shapes: []const Shape) u16 {
    var t: u16 = 0;
    for (shapes) |s| t += s.area();
    return t;
}
fn largest(comptime T: type, items: []const T) T {
    var best = items[0];
    for (items) |x| {
        if (x > best) best = x;
    }
    return best;
}

// ---- 3x: tagged unions, switch, optionals and errors -----------------------
const Command = union(enum) {
    set: u8,
    add: struct { amount: u8, times: u8 },
    clear,
};
fn run(c: Command, acc: u8) u8 {
    return switch (c) {
        .set => |v| v,
        .add => |a| acc +% a.amount *% a.times,
        .clear => 0,
    };
}
fn digit(c: u8) ?u8 {
    return if (c >= '0' and c <= '9') c - '0' else null;
}
const ParseError = error{ Empty, NotANumber, TooBig };
fn parse(text: []const u8) ParseError!u16 {
    if (text.len == 0) return error.Empty;
    var value: u16 = 0;
    for (text) |c| {
        const d = digit(c) orelse return error.NotANumber;
        const tens = @mulWithOverflow(value, 10);
        if (tens[1] != 0) return error.TooBig;
        const sum = @addWithOverflow(tens[0], d);
        if (sum[1] != 0) return error.TooBig;
        value = sum[0];
    }
    return value;
}
var cleanups: u8 = 0;
fn guarded(fail: bool) ParseError!u8 {
    defer cleanups += 1;
    errdefer cleanups += 10;
    if (fail) return error.Empty;
    return 5;
}

// ---- 4x: a table made at compile time, kept in flash ------------------------
const crc_table: [16]u8 = blk: {
    var t: [16]u8 = undefined;
    for (&t, 0..) |*entry, i| {
        var c: u8 = @as(u8, @intCast(i)) << 4;
        for (0..4) |_| c = if (c & 0x80 != 0) (c << 1) ^ 0x07 else c << 1;
        entry.* = c;
    }
    break :blk t;
};
noinline fn crc8(data: []const u8) u8 {
    var c: u8 = 0;
    for (data) |b| {
        c ^= b;
        c = (c << 4) ^ crc_table[c >> 4];
        c = (c << 4) ^ crc_table[c >> 4];
    }
    return c;
}

// ---- 5x: functions as values, comptime parameters ---------------------------
noinline fn apply(f: *const fn (u8) u8, x: u8) u8 {
    return f(x);
}
fn double(x: u8) u8 {
    return x *% 2;
}
fn negate(x: u8) u8 {
    return 0 -% x;
}
fn fold(data: []const u8, context: anytype, comptime f: fn (@TypeOf(context), u8) void) void {
    for (data) |b| f(context, b);
}

// ---- 6x: a ring buffer, generic over its element and its size --------------
fn Ring(comptime T: type, comptime n: usize) type {
    return struct {
        item: [n]T = undefined,
        head: u8 = 0,
        used: u8 = 0,
        const Self = @This();
        fn push(self: *Self, v: T) bool {
            if (self.used == n) return false;
            self.item[(@as(usize, self.head) + self.used) % n] = v;
            self.used += 1;
            return true;
        }
        fn pop(self: *Self) ?T {
            if (self.used == 0) return null;
            const v = self.item[self.head];
            self.head = @intCast((@as(usize, self.head) + 1) % n);
            self.used -= 1;
            return v;
        }
    };
}

// ---- 9x: an interrupt handler --------------------------------------------------
var ticks: u8 = 0;
fn tickCount() u8 {
    return @as(*volatile u8, &ticks).*;
}
export fn isr() callconv(.avr_interrupt) void {
    if (dev.INTCONbits.TMR0IF == 1) {
        dev.INTCONbits.TMR0IF = 0;
        @as(*volatile u8, &ticks).* = tickCount() +% 1;
    }
}

export fn main() noreturn {
    const s = seed();

    // 1x
    const half = Q8.init(0, 128);
    const q = Q8.init(3, 0).mul(half).add(Q8.init(1, 64));
    check(q.raw == Q8.init(2, 192).raw, 10);
    check(q.add(half).raw == 3 * 256 + 64, 11);

    // 2x
    const sq = Square{ .side = s };
    const ci = Circle{ .radius = 2 };
    const shapes = [_]Shape{ sq.shape(), ci.shape(), sq.shape() };
    check(total(&shapes) == 49 + 12 + 49, 20);
    check(shapes[0].sides == 4 and shapes[1].sides == 0, 21);
    check(largest(u8, &.{ 3, 9, s }) == 9 and largest(u32, &.{ 7, 70_000, 5 }) == 70_000, 22);
    check(@sizeOf(usize) == 2 and @sizeOf(*u8) == 2 and @sizeOf(c_int) == 2, 23);

    // 3x
    const program = [_]Command{ .{ .set = s }, .{ .add = .{ .amount = 3, .times = s } }, .clear, .{ .add = .{ .amount = 1, .times = 9 } } };
    var acc: u8 = 0;
    var trace: [4]u8 = undefined;
    for (program, &trace) |c, *slot| {
        acc = run(c, acc);
        slot.* = acc;
    }
    check(trace[0] == 7 and trace[1] == 28 and trace[2] == 0 and trace[3] == 9, 30);
    check((parse("1937") catch 0) == 1937, 31);
    check(parse("") == error.Empty, 32);
    check(parse("12x4") == error.NotANumber, 33);
    check(parse("70000") == error.TooBig, 34);
    check((digit('7') orelse 99) == 7 and digit('x') == null, 35);
    check((guarded(false) catch 0) == 5 and cleanups == 1, 36);
    check((guarded(true) catch 9) == 9 and cleanups == 12, 37);

    // 4x
    check(crc8("123456789") == 0xF4, 40); // the check value of CRC-8
    check(@intFromPtr(&crc_table) >= 0x8000, 41); // in flash, not copied to RAM

    // 5x
    check(apply(double, s) == 14 and apply(negate, s) == 249, 50);
    const table = [_]*const fn (u8) u8{ double, negate };
    check(table[s & 1](10) == 246, 51);
    const Tally = struct { count: u8 = 0, sum: u16 = 0 };
    var tally = Tally{};
    fold("123456789", &tally, struct {
        fn f(t: *Tally, b: u8) void {
            t.count += 1;
            t.sum += b - '0';
        }
    }.f);
    check(tally.count == 9 and tally.sum == 45, 52);

    // 6x
    var ring = Ring(u16, 4){};
    var v: u16 = 1000;
    while (ring.push(v)) v += 1000;
    check(ring.used == 4 and ring.pop().? == 1000 and ring.pop().? == 2000, 60);
    _ = ring.push(5000);
    check(ring.pop().? == 3000 and ring.pop().? == 4000 and ring.pop().? == 5000 and ring.pop() == null, 61);

    // 7x: slices, loops, labelled blocks, inline loops
    const data = [_]u8{ 5, 3, 9, 1, 7, s };
    var above: u8 = 0;
    for (data) |x| {
        if (x > 4) above += 1;
    }
    check(above == 4, 70);
    const where: ?usize = blk: {
        for (data, 0..) |x, i| {
            if (x == 9) break :blk i;
        }
        break :blk null;
    };
    check(where.? == 2, 71);
    var hundreds: u16 = 0;
    for (data[1..4]) |x| hundreds += @as(u16, x) * 100;
    check(hundreds == 1300, 72);
    var sorted = data;
    // An insertion sort: std.sort would bring in far more.
    for (1..sorted.len) |i| {
        var j = i;
        while (j > 0 and sorted[j - 1] > sorted[j]) : (j -= 1) {
            const t = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
    }
    check(sorted[0] == 1 and sorted[2] == 5 and sorted[3] == 7 and sorted[4] == 7 and sorted[5] == 9, 73);
    var widths: u8 = 0;
    inline for (.{ u8, u16, u32, u64 }) |T| widths += @sizeOf(T);
    check(widths == 15, 74);
    const Mode = enum(u8) { off, slow = 10, fast = 200 };
    const mode: Mode = if (s == 7) .fast else .off;
    check(@intFromEnum(mode) == 200 and mode != .slow, 75);
    const Flags = packed struct(u8) { ready: bool, mode: u3, count: u4 };
    var flags: Flags = @bitCast(@as(u8, 0));
    flags.mode = 5;
    flags.count = @truncate(s);
    flags.ready = true;
    check(@as(u8, @bitCast(flags)) == 0x7B, 76);

    // 8x: arithmetic of every width, also the odd ones
    const a = @as(u32, s) * 100_000; // 700,000
    check(a / 7 == 100_000 and a % 1000 == 0 and a >> 4 == 43_750, 80);
    const b = @as(u64, s) << 40;
    check(b / 3 == 2_565_527_131_477 and @as(u8, @truncate(b >> 36)) == 112, 81);
    const c = -@as(i16, s) * 300; // -2100
    check(@divTrunc(c, 7) == -300 and @rem(c, 1000) == -100 and @abs(c) == 2100, 82);
    check(@popCount(s) == 3 and @clz(@as(u16, s)) == 13 and (s << 6 | s >> 2) == 0xC1, 83);
    check(200 +| s * 10 == 255 and 200 +% s * 10 == 14 and @addWithOverflow(@as(u8, 200), s * 10)[1] == 1, 84);
    const t24: u24 = @as(u24, s) * 1_000_000; // 7,000,000 fits 24 bits
    check(t24 / 1000 == 7000 and @as(u8, @truncate(t24 >> 16)) == 0x6A, 85);
    const f = @as(f32, @floatFromInt(s)) * 1.5 + 0.25; // 10.75
    check(f > 10.7 and f < 10.8 and @as(u8, @intFromFloat(f)) == 10 and @as(u16, @intFromFloat(f * 4.0)) == 43, 86);

    // 9x
    dev.OPTION_REG.* = 0x08; // timer 0 from the instruction clock, no prescaler
    dev.INTCONbits.TMR0IE = 1;
    pic.enableInterrupts();
    while (tickCount() < 3) {}
    _ = pic.disableInterrupts();
    check(tickCount() >= 3, 90);

    dev.TRISD.* = 0;
    dev.LATD.* = if (failed != 0) failed else 0x5A;
    while (true) pic.sleep();
}
