//! Instruction-semantics tests. Expected values come from the instruction
//! descriptions in the PIC16(L)F193x data sheet (DS41364), not from the
//! simulator's own code.

use pic16f1_sim::cpu::{FLAG_C, FLAG_DC, FLAG_Z};
use pic16f1_sim::{Cpu, Device, Halt, Image};

const F: u16 = 0x80; // destination bit
const SLEEP: u16 = 0x0063;

fn movlw(k: u8) -> u16 { 0x3000 | k as u16 }
fn movwf(f: u8) -> u16 { 0x0080 | f as u16 }
fn movf(f: u8, d: u16) -> u16 { 0x0800 | d | f as u16 }
fn addwf(f: u8, d: u16) -> u16 { 0x0700 | d | f as u16 }
fn addwfc(f: u8, d: u16) -> u16 { 0x3D00 | d | f as u16 }
fn subwf(f: u8, d: u16) -> u16 { 0x0200 | d | f as u16 }
fn subwfb(f: u8, d: u16) -> u16 { 0x3B00 | d | f as u16 }
fn movlb(k: u8) -> u16 { 0x0020 | k as u16 }
fn movlp(k: u8) -> u16 { 0x3180 | k as u16 }
fn call(k: u16) -> u16 { 0x2000 | k }
fn goto(k: u16) -> u16 { 0x2800 | k }
fn bra(k: i16) -> u16 { 0x3200 | (k as u16 & 0x1FF) }
fn bsf(f: u8, b: u8) -> u16 { 0x1400 | ((b as u16) << 7) | f as u16 }
fn bcf(f: u8, b: u8) -> u16 { 0x1000 | ((b as u16) << 7) | f as u16 }
fn btfsc(f: u8, b: u8) -> u16 { 0x1800 | ((b as u16) << 7) | f as u16 }
fn btfss(f: u8, b: u8) -> u16 { 0x1C00 | ((b as u16) << 7) | f as u16 }
fn addfsr(n: u16, k: i8) -> u16 { 0x3100 | (n << 6) | (k as u16 & 0x3F) }
fn moviw_off(n: u16, k: i8) -> u16 { 0x3F00 | (n << 6) | (k as u16 & 0x3F) }
fn movwi_off(n: u16, k: i8) -> u16 { 0x3F80 | (n << 6) | (k as u16 & 0x3F) }

fn run(prog: &[u16]) -> Cpu {
    let (cpu, halt) = run_halt(prog);
    assert_eq!(halt, Halt::Sleep);
    cpu
}

fn run_halt(prog: &[u16]) -> (Cpu, Halt) {
    let mut img = Image::default();
    for (i, w) in prog.iter().enumerate() {
        img.words.insert(i as u32, *w);
    }
    let mut cpu = Cpu::new(Device::builtin("pic16f1937").unwrap());
    cpu.load(&img).unwrap();
    let halt = cpu.run(100_000);
    (cpu, halt)
}

fn flags(cpu: &Cpu) -> u8 {
    cpu.status() & (FLAG_C | FLAG_DC | FLAG_Z)
}

#[test]
fn add_sets_carry_digit_carry_and_zero() {
    let c = run(&[movlw(0xFF), movwf(0x20), movlw(0x01), addwf(0x20, 0), SLEEP]);
    assert_eq!(c.w(), 0x00);
    assert_eq!(flags(&c), FLAG_C | FLAG_DC | FLAG_Z);
    let c = run(&[movlw(0x0F), movwf(0x20), movlw(0x01), addwf(0x20, F), SLEEP]);
    assert_eq!(c.peek(0x20), 0x10);
    assert_eq!(flags(&c), FLAG_DC);
}

#[test]
fn subtract_carry_is_inverted_borrow() {
    // 5 - 3: no borrow, C = 1.
    let c = run(&[movlw(5), movwf(0x20), movlw(3), subwf(0x20, 0), SLEEP]);
    assert_eq!(c.w(), 2);
    assert_eq!(flags(&c) & FLAG_C, FLAG_C);
    // 3 - 5: borrow, C = 0.
    let c = run(&[movlw(3), movwf(0x20), movlw(5), subwf(0x20, 0), SLEEP]);
    assert_eq!(c.w(), 0xFE);
    assert_eq!(flags(&c) & FLAG_C, 0);
    // Equal operands: Z = 1, C = 1.
    let c = run(&[movlw(7), movwf(0x20), subwf(0x20, 0), SLEEP]);
    assert_eq!(flags(&c), FLAG_C | FLAG_DC | FLAG_Z);
    // SUBLW k: k - W.
    let c = run(&[movlw(1), 0x3C00 | 10, SLEEP]);
    assert_eq!(c.w(), 9);
}

#[test]
fn sixteen_bit_add_and_subtract_chain_through_carry() {
    // 0x01FF + 0x0001 = 0x0200 in 0x20/0x21.
    let c = run(&[
        movlw(0xFF), movwf(0x20), movlw(0x01), movwf(0x21),
        movlw(0x01), addwf(0x20, F), movlw(0x00), addwfc(0x21, F), SLEEP,
    ]);
    assert_eq!((c.peek(0x20), c.peek(0x21)), (0x00, 0x02));
    // 0x0200 - 0x0001 = 0x01FF.
    let c = run(&[
        movlw(0x00), movwf(0x20), movlw(0x02), movwf(0x21),
        movlw(0x01), subwf(0x20, F), movlw(0x00), subwfb(0x21, F), SLEEP,
    ]);
    assert_eq!((c.peek(0x20), c.peek(0x21)), (0xFF, 0x01));
    assert_eq!(flags(&c) & FLAG_C, FLAG_C);
}

#[test]
fn shifts_and_rotates() {
    // LSLF: C <- bit 7. LSRF: C <- bit 0. ASRF keeps the sign.
    let c = run(&[movlw(0x81), movwf(0x20), 0x3500 | F | 0x20, SLEEP]);
    assert_eq!(c.peek(0x20), 0x02);
    assert_eq!(flags(&c) & FLAG_C, FLAG_C);
    let c = run(&[movlw(0x81), movwf(0x20), 0x3600 | F | 0x20, SLEEP]);
    assert_eq!(c.peek(0x20), 0x40);
    assert_eq!(flags(&c) & FLAG_C, FLAG_C);
    let c = run(&[movlw(0x82), movwf(0x20), 0x3700 | F | 0x20, SLEEP]);
    assert_eq!(c.peek(0x20), 0xC1);
    assert_eq!(flags(&c) & FLAG_C, 0);
    // RLF/RRF rotate through carry. Carry starts clear after an add with no carry.
    let c = run(&[movlw(0x80), movwf(0x20), 0x0D00 | F | 0x20, 0x0D00 | F | 0x20, SLEEP]);
    assert_eq!(c.peek(0x20), 0x01);
    let c = run(&[movlw(0x01), movwf(0x20), 0x0C00 | F | 0x20, 0x0C00 | F | 0x20, SLEEP]);
    assert_eq!(c.peek(0x20), 0x80);
    // SWAPF exchanges nibbles.
    let c = run(&[movlw(0xA5), movwf(0x20), 0x0E00 | 0x20, SLEEP]);
    assert_eq!(c.w(), 0x5A);
}

#[test]
fn skips_take_two_cycles_when_taken() {
    // BTFSS on a clear bit: not taken, falls into MOVLW 1.
    let c = run(&[movlw(0), movwf(0x20), btfss(0x20, 3), movlw(1), SLEEP]);
    assert_eq!(c.w(), 1);
    assert_eq!(c.cycles, 5);
    // BTFSC on a clear bit: skip. The skipped instruction costs the second cycle.
    let c = run(&[movlw(0), movwf(0x20), btfsc(0x20, 3), movlw(1), SLEEP]);
    assert_eq!(c.w(), 0);
    assert_eq!(c.cycles, 5);
    // DECFSZ counts 3, 2, 1 and skips on reaching zero.
    let c = run(&[movlw(3), movwf(0x20), movlw(0), 0x3E01, 0x0B00 | F | 0x20, bra(-3), SLEEP]);
    assert_eq!(c.w(), 3);
}

#[test]
fn banking_common_ram_and_core_mirrors() {
    // 0x20 in bank 1 is a different byte from 0x20 in bank 0; 0x70 is shared.
    let c = run(&[
        movlw(0x11), movwf(0x20), movwf(0x70),
        movlb(1), movlw(0x22), movwf(0x20), movf(0x70, 0), movlb(0), SLEEP,
    ]);
    assert_eq!(c.peek(0x020), 0x11);
    assert_eq!(c.peek(0x0A0), 0x22);
    assert_eq!(c.w(), 0x11);
    // WREG is a core register at 0x09 in every bank.
    let c = run(&[movlb(5), movlw(0x40), addwf(0x09, F), movlb(0), SLEEP]);
    assert_eq!(c.w(), 0x80);
}

#[test]
fn indirect_addressing_three_views() {
    // Linear 0x2050 is the first GPR byte of bank 1 (0xA0): 80 bytes per bank.
    let c = run(&[
        movlw(0x50), movwf(0x04), movlw(0x20), movwf(0x05),
        movlw(0x77), movwf(0x00), SLEEP,
    ]);
    assert_eq!(c.peek(0x0A0), 0x77);
    // Traditional view through FSR1 with an offset, and ADDFSR.
    let c = run(&[
        movlw(0x20), movwf(0x06), movlw(0x00), movwf(0x07),
        movlw(0x33), movwi_off(1, 5), addfsr(1, 5), moviw_off(1, 0), SLEEP,
    ]);
    assert_eq!(c.peek(0x025), 0x33);
    assert_eq!(c.w(), 0x33);
    assert_eq!(c.fsr(1), 0x0025);
    // Flash view: 0x8000 + word address reads the low byte of the word.
    let c = run(&[
        movlw(0x06), movwf(0x04), movlw(0x80), movwf(0x05), movf(0x00, 0), SLEEP, 0x34AB,
    ]);
    assert_eq!(c.w(), 0xAB);
}

#[test]
fn moviw_movwi_pre_and_post_modes() {
    // MOVWI FSR0++ twice then MOVIW --FSR0 reads the last byte back.
    let c = run(&[
        movlw(0x20), movwf(0x04), movlw(0x00), movwf(0x05),
        movlw(1), 0x001A, movlw(2), 0x001A, movlw(0), 0x0011, SLEEP,
    ]);
    assert_eq!((c.peek(0x20), c.peek(0x21)), (1, 2));
    assert_eq!(c.w(), 2);
    assert_eq!(c.fsr(0), 0x0021);
}

#[test]
fn call_return_and_paging() {
    // CALL takes the high bits from PCLATH<6:3>.
    let mut prog = vec![0x3FFFu16; 0x810];
    prog[0] = movlp(0x08);
    prog[1] = call(0x005);
    prog[2] = movlp(0x00);
    prog[3] = SLEEP;
    prog[0x805] = movlw(0x5A);
    prog[0x806] = 0x0008;
    let c = run(&prog);
    assert_eq!(c.w(), 0x5A);
    assert_eq!(c.max_stack_depth, 1);
    assert_eq!(c.stack_depth(), 0);
    // CALL 2 + MOVLP 1 + MOVLW 1 + RETURN 2 + MOVLP 1 + SLEEP 1
    assert_eq!(c.cycles, 8);
}

#[test]
fn callw_brw_and_computed_goto() {
    // CALLW: PCLATH:W. RETLW returns a constant.
    let c = run(&[movlp(0), movlw(4), 0x000A, SLEEP, 0x3442]);
    assert_eq!(c.w(), 0x42);
    // BRW skips W instructions.
    let c = run(&[movlw(1), 0x000B, movlw(0x11), movlw(0x22), SLEEP]);
    assert_eq!(c.w(), 0x22);
    // ADDWF PCL,F: PC = PCLATH : (PCL + W), where PCL already points past the add.
    let c = run(&[movlp(0), movlw(1), addwf(0x02, F), movlw(0x11), movlw(0x22), SLEEP]);
    assert_eq!(c.w(), 0x22);
    // GOTO and BRA.
    let c = run(&[goto(3), movlw(1), SLEEP, bra(-2), SLEEP]);
    assert_eq!(c.w(), 0);
}

#[test]
fn stack_overflow_and_underflow_stop_the_run() {
    let (_, h) = run_halt(&[call(0)]);
    assert_eq!(h, Halt::StackOverflow);
    let (_, h) = run_halt(&[0x0008]);
    assert_eq!(h, Halt::StackUnderflow);
    let (_, h) = run_halt(&[movlw(1)]);
    assert_eq!(h, Halt::Unprogrammed(1));
}

#[test]
fn timer0_interrupt_saves_and_restores_context() {
    // OPTION_REG (bank 1, 0x15): internal clock, prescaler bypassed.
    // The handler loads W and BSR with junk; RETFIE must restore both.
    let mut prog = vec![0x3FFFu16; 0x20];
    prog[0] = goto(0x10);
    prog[4] = bcf(0x0B, 2); // clear TMR0IF
    prog[5] = movlw(0xEE);
    prog[6] = movlb(7);
    prog[7] = movwf(0x70); // common RAM: mark that the handler ran
    prog[8] = 0x0009; // RETFIE
    prog[0x10] = movlb(1);
    prog[0x11] = movlw(0x08); // PSA = 1, T0CS = 0
    prog[0x12] = movwf(0x15);
    prog[0x13] = movlb(0);
    prog[0x14] = movlw(0x12);
    prog[0x15] = bsf(0x0B, 5); // TMR0IE
    prog[0x16] = bsf(0x0B, 7); // GIE
    prog[0x17] = btfss(0x70, 7); // wait for the handler
    prog[0x18] = bra(-2);
    prog[0x19] = SLEEP;
    let c = run(&prog);
    assert_eq!(c.interrupts_taken, 1);
    assert_eq!(c.w(), 0x12);
    assert_eq!(c.bsr(), 0);
    assert_eq!(c.peek(0x70), 0xEE);
}

#[test]
fn uart_transmit_and_exit_code() {
    let d = Device::builtin("pic16f1937").unwrap();
    let (txsta, rcsta, txreg, latd) = (
        d.sfr_addr("TXSTA").unwrap(), d.sfr_addr("RCSTA").unwrap(),
        d.sfr_addr("TXREG").unwrap(), d.sfr_addr("LATD").unwrap(),
    );
    let bank = |a: u16| movlb((a >> 7) as u8);
    let f = |a: u16| (a & 0x7F) as u8;
    let c = run(&[
        bank(txsta), bsf(f(txsta), 5), bank(rcsta), bsf(f(rcsta), 7),
        bank(txreg), movlw(b'O'), movwf(f(txreg)), movlw(b'K'), movwf(f(txreg)),
        bank(latd), movlw(7), movwf(f(latd)), SLEEP,
    ]);
    assert_eq!(c.uart_out, b"OK");
    assert_eq!(c.exit_code(), Some(7));
}

#[test]
fn hex_round_trip() {
    let mut img = Image::default();
    for i in 0..40u32 {
        img.words.insert(i, (0x3000 + i) as u16);
    }
    img.words.insert(0x8007, 0x3FE4);
    img.words.insert(0x8008, 0x3FFF);
    let text = img.to_hex();
    let back = Image::from_hex(&text).unwrap();
    assert_eq!(back.words, img.words);
    assert!(text.contains(":020000040001F9"));
}

#[test]
fn sixty_four_bank_devices_use_the_six_bit_movlb() {
    // MOVLB 40 on a PIC16F18877 is 0x0168 (from Microchip's assembler).
    // Bank 40 holds general purpose RAM at 0x20; linear offset 40 * 80.
    let mut img = Image::default();
    let prog = [0x0168u16, movlw(0x5A), movwf(0x20), 0x0140, SLEEP];
    for (i, w) in prog.iter().enumerate() {
        img.words.insert(i as u32, *w);
    }
    let mut cpu = Cpu::new(Device::builtin("pic16f18877").unwrap());
    cpu.load(&img).unwrap();
    assert_eq!(cpu.run(1000), Halt::Sleep);
    assert_eq!(cpu.peek(40 * 128 + 0x20), 0x5A);
    assert_eq!(cpu.bsr(), 0);
    let d = Device::builtin("pic16f18877").unwrap();
    assert_eq!(d.linear_to_traditional(40 * 80), Some(40 * 128 + 0x20));
}
