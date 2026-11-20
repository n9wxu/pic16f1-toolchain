//! The Rust language on the PIC16F1, with `core` and nothing else: each
//! part checks itself, and the program reports through LATD as the C tests
//! do (0x5A if everything held, otherwise the number of the first
//! failure). For scripts/test-rust.sh.
#![no_std]
#![no_main]
#![feature(abi_avr_interrupt)]

use core::ops::Add;
use core::ptr::{addr_of, addr_of_mut, read_volatile, write_volatile};
use pic16f1::device::*;

static mut FAILED: u8 = 0;
fn check(ok: bool, number: u8) {
    unsafe {
        if !ok && read_volatile(addr_of!(FAILED)) == 0 {
            write_volatile(addr_of_mut!(FAILED), number);
        }
    }
}

/// A value the compiler cannot see through.
#[inline(never)]
fn seed() -> u8 {
    unsafe { read_volatile(addr_of!(SEED)) }
}
static mut SEED: u8 = 7;

// ---- 1x: structs, methods, operators -------------------------------------
#[derive(Clone, Copy, PartialEq)]
struct Q8(i16);
impl Q8 {
    const fn new(whole: i16, frac: u8) -> Self {
        Q8(whole * 256 + frac as i16)
    }
    fn mul(self, o: Q8) -> Q8 {
        Q8(((self.0 as i32 * o.0 as i32) >> 8) as i16)
    }
}
impl Add for Q8 {
    type Output = Q8;
    fn add(self, o: Q8) -> Q8 {
        Q8(self.0 + o.0)
    }
}

// ---- 2x: traits, generics, and calls through a table ---------------------
trait Shape {
    fn area(&self) -> u16;
    fn sides(&self) -> u8 {
        0
    }
}
struct Square(u8);
struct Circle(u8);
impl Shape for Square {
    fn area(&self) -> u16 {
        self.0 as u16 * self.0 as u16
    }
    fn sides(&self) -> u8 {
        4
    }
}
impl Shape for Circle {
    fn area(&self) -> u16 {
        self.0 as u16 * self.0 as u16 * 3
    }
}
#[inline(never)]
fn total(shapes: &[&dyn Shape]) -> u16 {
    shapes.iter().map(|s| s.area()).sum()
}
fn largest<T: PartialOrd + Copy>(items: &[T]) -> T {
    let mut best = items[0];
    for &x in items {
        if x > best {
            best = x;
        }
    }
    best
}

// ---- 3x: enums with data, match, Option and Result ----------------------
enum Command {
    Set(u8),
    Add { amount: u8, times: u8 },
    Clear,
}
fn run(c: &Command, acc: u8) -> u8 {
    match *c {
        Command::Set(v) => v,
        Command::Add { amount, times } => acc.wrapping_add(amount.wrapping_mul(times)),
        Command::Clear => 0,
    }
}
fn digit(c: u8) -> Option<u8> {
    if c.is_ascii_digit() {
        Some(c - b'0')
    } else {
        None
    }
}
#[derive(PartialEq)]
enum ParseError {
    Empty,
    NotANumber(u8),
    TooBig,
}
fn parse(text: &[u8]) -> Result<u16, ParseError> {
    if text.is_empty() {
        return Err(ParseError::Empty);
    }
    let mut value: u16 = 0;
    for &c in text {
        let d = digit(c).ok_or(ParseError::NotANumber(c))?;
        value = value.checked_mul(10).and_then(|v| v.checked_add(d as u16)).ok_or(ParseError::TooBig)?;
    }
    Ok(value)
}

// ---- 4x: a table made by the compiler, kept in flash ---------------------
const fn crc_table() -> [u8; 16] {
    let mut t = [0u8; 16];
    let mut i = 0;
    while i < 16 {
        let mut c = (i as u8) << 4;
        let mut k = 0;
        while k < 4 {
            c = if c & 0x80 != 0 { (c << 1) ^ 0x07 } else { c << 1 };
            k += 1;
        }
        t[i] = c;
        i += 1;
    }
    t
}
static CRC_TABLE: [u8; 16] = crc_table();
#[inline(never)]
fn crc8(data: &[u8]) -> u8 {
    data.iter().fold(0, |c, &b| {
        let c = c ^ b;
        let c = (c << 4) ^ CRC_TABLE[(c >> 4) as usize];
        (c << 4) ^ CRC_TABLE[(c >> 4) as usize]
    })
}

// ---- 5x: closures and functions as values ---------------------------------
#[inline(never)]
fn apply(f: fn(u8) -> u8, x: u8) -> u8 {
    f(x)
}
fn double(x: u8) -> u8 {
    x.wrapping_mul(2)
}
fn negate(x: u8) -> u8 {
    x.wrapping_neg()
}
#[inline(never)]
fn fold_with(data: &[u8], f: &mut dyn FnMut(u8)) {
    for &b in data {
        f(b)
    }
}

// ---- 6x: a ring buffer, generic over its element and its size ------------
struct Ring<T: Copy, const N: usize> {
    item: [T; N],
    head: u8,
    used: u8,
}
impl<T: Copy, const N: usize> Ring<T, N> {
    fn new(fill: T) -> Self {
        Ring { item: [fill; N], head: 0, used: 0 }
    }
    fn push(&mut self, v: T) -> bool {
        if self.used as usize == N {
            return false;
        }
        self.item[(self.head as usize + self.used as usize) % N] = v;
        self.used += 1;
        true
    }
    fn pop(&mut self) -> Option<T> {
        if self.used == 0 {
            return None;
        }
        let v = self.item[self.head as usize];
        self.head = ((self.head as usize + 1) % N) as u8;
        self.used -= 1;
        Some(v)
    }
}

// ---- 9x: an interrupt handler ------------------------------------------------
static mut TICKS: u8 = 0;
#[no_mangle]
pub unsafe extern "avr-interrupt" fn isr() {
    if intcon::TMR0IF.is_set() {
        intcon::TMR0IF.clear();
        write_volatile(addr_of_mut!(TICKS), read_volatile(addr_of!(TICKS)).wrapping_add(1));
    }
}

pic16f1::entry!(main);
fn main() -> ! {
    let s = seed();

    // 1x
    let half = Q8::new(0, 128);
    let q = Q8::new(3, 0).mul(half) + Q8::new(1, 64);
    check(q == Q8::new(2, 192), 10);
    check((q + half).0 == 3 * 256 + 64, 11);

    // 2x
    let sq = Square(s);
    let ci = Circle(2);
    let shapes: [&dyn Shape; 3] = [&sq, &ci, &sq];
    check(total(&shapes) == 49 + 12 + 49, 20);
    check(shapes[0].sides() == 4 && shapes[1].sides() == 0, 21);
    check(largest(&[3u8, 9, s]) == 9 && largest(&[7u32, 70_000, 5]) == 70_000, 22);
    check(core::mem::size_of::<usize>() == 2 && core::mem::size_of::<&u8>() == 2, 23);

    // 3x
    let program = [Command::Set(s), Command::Add { amount: 3, times: s }, Command::Clear, Command::Add { amount: 1, times: 9 }];
    let mut acc = 0;
    let mut trace = [0u8; 4];
    for (slot, c) in trace.iter_mut().zip(program.iter()) {
        acc = run(c, acc);
        *slot = acc;
    }
    check(trace == [7, 28, 0, 9], 30);
    check(parse(b"1937") == Ok(1937), 31);
    check(parse(b"") == Err(ParseError::Empty), 32);
    check(parse(b"12x4") == Err(ParseError::NotANumber(b'x')), 33);
    check(parse(b"70000") == Err(ParseError::TooBig), 34);
    check(digit(b'7').unwrap_or(99) == 7 && digit(b'x').is_none(), 35);

    // 4x
    check(crc8(b"123456789") == 0xF4, 40); // the check value of CRC-8
    check(addr_of!(CRC_TABLE) as usize >= 0x8000, 41); // in flash, not copied to RAM

    // 5x
    check(apply(double, s) == 14 && apply(negate, s) == 249, 50);
    let table: [fn(u8) -> u8; 2] = [double, negate];
    check(table[(s & 1) as usize](10) == 246, 51);
    let mut count = 0u8;
    let mut sum = 0u16;
    fold_with(b"123456789", &mut |b| {
        count += 1;
        sum += (b - b'0') as u16;
    });
    check(count == 9 && sum == 45, 52);
    let bias = s;
    let add_bias = |x: u8| x + bias;
    check(add_bias(3) == 10, 53);

    // 6x
    let mut ring: Ring<u16, 4> = Ring::new(0);
    let mut v = 1000;
    while ring.push(v) {
        v += 1000;
    }
    check(ring.used == 4 && ring.pop() == Some(1000) && ring.pop() == Some(2000), 60);
    ring.push(5000);
    check(ring.pop() == Some(3000) && ring.pop() == Some(4000) && ring.pop() == Some(5000) && ring.pop().is_none(), 61);

    // 7x: iterators and slices
    let data = [5u8, 3, 9, 1, 7, s];
    check(data.iter().filter(|&&x| x > 4).count() == 4, 70);
    check(data.iter().rev().position(|&x| x == 9) == Some(3), 71);
    check(data.iter().copied().max() == Some(9) && data.iter().copied().min() == Some(1), 72);
    check(data[1..4].iter().map(|&x| x as u16 * 100).sum::<u16>() == 1300, 73);
    check(data.windows(2).filter(|w| w[0] > w[1]).count() == 2, 74);
    let mut sorted = data;
    // An insertion sort: the library's sorts are thousands of words.
    for i in 1..sorted.len() {
        let mut j = i;
        while j > 0 && sorted[j - 1] > sorted[j] {
            sorted.swap(j - 1, j);
            j -= 1;
        }
    }
    check(sorted == [1, 3, 5, 7, 7, 9], 75);
    check(sorted.binary_search(&5) == Ok(2) && sorted.contains(&9) && !sorted.contains(&4), 76);
    check("PIC16F1937".as_bytes().iter().filter(|b| b.is_ascii_digit()).count() == 6, 77);
    check("abc" < "abd" && "pic".eq_ignore_ascii_case("PIC"), 78);

    // 8x: arithmetic of every width
    let a = s as u32 * 100_000; // 700,000
    check(a / 7 == 100_000 && a % 1000 == 0 && a >> 4 == 43_750, 80);
    let b = (s as u64) << 40;
    check(b / 3 == 2_565_527_131_477 && (b >> 36) as u8 == 112, 81);
    let c = -(s as i16) * 300; // -2100
    check(c / 7 == -300 && c % 1000 == -100 && c.abs() == 2100, 82);
    check(s.count_ones() == 3 && (s as u16).leading_zeros() == 13 && s.rotate_left(6) == 0xC1, 83);
    check(200u8.checked_add(s * 10).is_none() && 200u8.saturating_add(s * 10) == 255 && 200u8.wrapping_add(s * 10) == 14, 84);
    check(u16::from_le_bytes([0x34, 0x12]) == 0x1234 && 0x1234u16.swap_bytes() == 0x3412, 85);
    let f = s as f32 * 1.5 + 0.25; // 10.75
    check(f > 10.7 && f < 10.8 && f as u8 == 10 && (f * 4.0) as u16 == 43, 86);

    // 9x
    OPTION_REG.write(0x08); // timer 0 from the instruction clock, no prescaler
    intcon::TMR0IE.set();
    pic16f1::enable_interrupts();
    while unsafe { read_volatile(addr_of!(TICKS)) } < 3 {}
    pic16f1::disable_interrupts();
    check(unsafe { read_volatile(addr_of!(TICKS)) } >= 3, 90);

    TRISD.write(0);
    let failed = unsafe { read_volatile(addr_of!(FAILED)) };
    LATD.write(if failed != 0 { failed } else { 0x5A });
    loop {
        pic16f1::sleep();
    }
}
