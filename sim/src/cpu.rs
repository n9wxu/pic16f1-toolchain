//! Core execution model.
//!
//! Timing is counted in instruction cycles (Tcy = 4/Fosc). Every instruction
//! takes one cycle, except those that change the program counter, which take
//! two (including a skip that is taken).

use crate::device::Device;
use crate::disasm::{decode, Op};
use crate::image::Image;

// Core registers. These twelve addresses are fixed by the instruction set
// (they are mirrored at the bottom of every bank), not by any one device.
pub const INDF0: u16 = 0x00;
pub const INDF1: u16 = 0x01;
pub const PCL: u16 = 0x02;
pub const STATUS: u16 = 0x03;
pub const FSR0L: u16 = 0x04;
pub const FSR0H: u16 = 0x05;
pub const FSR1L: u16 = 0x06;
pub const FSR1H: u16 = 0x07;
pub const BSR: u16 = 0x08;
pub const WREG: u16 = 0x09;
pub const PCLATH: u16 = 0x0A;
pub const INTCON: u16 = 0x0B;

pub const FLAG_C: u8 = 0x01;
pub const FLAG_DC: u8 = 0x02;
pub const FLAG_Z: u8 = 0x04;
const STATUS_NPD: u8 = 0x08;
const STATUS_NTO: u8 = 0x10;

const LINEAR_BASE: u16 = 0x2000;
const FLASH_VIEW: u16 = 0x8000;

/// Cycles between the end of the instruction that raised an interrupt and
/// the first instruction of the handler. To be confirmed against silicon.
const INTERRUPT_ENTRY_CYCLES: u64 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Halt {
    /// SLEEP executed with nothing able to wake the device.
    Sleep,
    MaxCycles,
    StackOverflow,
    StackUnderflow,
    /// RESET instruction.
    Reset,
    /// Fetched a word that the image never programmed.
    Unprogrammed(u16),
    Illegal { pc: u16, word: u16 },
}

impl Halt {
    pub fn name(&self) -> &'static str {
        match self {
            Halt::Sleep => "sleep",
            Halt::MaxCycles => "max-cycles",
            Halt::StackOverflow => "stack-overflow",
            Halt::StackUnderflow => "stack-underflow",
            Halt::Reset => "reset",
            Halt::Unprogrammed(_) => "unprogrammed",
            Halt::Illegal { .. } => "illegal-opcode",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartMode {
    /// The transmitter is always ready; a byte takes no time.
    Fast,
    /// A byte occupies the transmitter for ten bit times at the configured baud rate.
    Timed,
}

#[derive(Debug, Default, Clone)]
struct Port {
    port: u16,
    lat: u16,
    tris: u16,
}

/// Addresses and bit masks of the peripherals the simulator models,
/// looked up by name in the device database.
#[derive(Debug, Default, Clone)]
struct Periph {
    gie: u8,
    peie: u8,
    /// (enable mask, flag mask) pairs inside INTCON.
    intcon_sources: Vec<(u8, u8)>,
    tmr0if: u8,
    /// (PIEn address, PIRn address).
    pie_pir: Vec<(u16, u16)>,
    tmr0: Option<u16>,
    option_reg: Option<u16>,
    opt_cs: u8,
    opt_psa: u8,
    opt_ps_shift: u8,
    tmr2: Option<u16>,
    pr2: u16,
    t2con: u16,
    t2_on: u8,
    t2_ckps_shift: u8,
    t2_outps_shift: u8,
    pir_tmr2: (u16, u8),
    txreg: Option<u16>,
    txsta: u16,
    rcsta: u16,
    spbrgl: u16,
    spbrgh: u16,
    baudcon: u16,
    txen: u8,
    trmt: u8,
    brgh: u8,
    spen: u8,
    brg16: u8,
    pir_tx: (u16, u8),
    ports: Vec<Port>,
    stkptr: Option<u16>,
    tosl: u16,
    tosh: u16,
    /// Shadow register addresses: W, STATUS, BSR, PCLATH, FSR0L, FSR0H, FSR1L, FSR1H.
    shadow: Option<[u16; 8]>,
    exit_port: Option<u16>,
}

pub struct Cpu {
    pub dev: Device,
    flash: Vec<u16>,
    programmed: Vec<bool>,
    pub config: Vec<(u32, u16)>,
    ram: Vec<u8>,
    impl_mask: Vec<u8>,
    write_mask: Vec<u8>,
    is_gpr: Vec<bool>,
    pub pc: u16,
    stack: Vec<u16>,
    stkptr: u8,
    pub cycles: u64,
    pub instructions: u64,
    pub max_stack_depth: u8,
    pub interrupts_taken: u64,
    pub uart_out: Vec<u8>,
    pub uart_mode: UartMode,
    pub uart_overruns: u64,
    tx_ready_at: u64,
    t0_prescale: u16,
    t0_inhibit: u8,
    t2_prescale: u16,
    t2_postscale: u8,
    pc_changed: bool,
    p: Periph,
    /// Per-word execution counts, if profiling is on.
    pub profile: Option<Vec<u64>>,
    pub trace: bool,
}

impl Cpu {
    pub fn new(dev: Device) -> Cpu {
        let mut impl_mask = vec![0u8; 0x1000];
        let mut write_mask = vec![0u8; 0x1000];
        let mut is_gpr = vec![false; 0x1000];
        for r in dev.gpr.iter().chain(std::iter::once(&dev.common_ram)) {
            for a in r.begin..r.end {
                impl_mask[a as usize] = 0xFF;
                write_mask[a as usize] = 0xFF;
                is_gpr[a as usize] = true;
            }
        }
        for s in &dev.sfrs {
            impl_mask[s.addr as usize] = s.impl_mask;
            write_mask[s.addr as usize] = s.write_mask() & s.impl_mask;
        }
        let p = Periph::lookup(&dev);
        let flash_words = dev.flash.words as usize;
        let depth = dev.hw_stack_depth as usize;
        let mut cpu = Cpu {
            dev,
            flash: vec![0x3FFF; flash_words],
            programmed: vec![false; flash_words],
            config: Vec::new(),
            ram: vec![0; 0x1000],
            impl_mask,
            write_mask,
            is_gpr,
            pc: 0,
            stack: vec![0; depth],
            stkptr: 0x1F,
            cycles: 0,
            instructions: 0,
            max_stack_depth: 0,
            interrupts_taken: 0,
            uart_out: Vec::new(),
            uart_mode: UartMode::Fast,
            uart_overruns: 0,
            tx_ready_at: 0,
            t0_prescale: 0,
            t0_inhibit: 0,
            t2_prescale: 0,
            t2_postscale: 0,
            pc_changed: false,
            p,
            profile: None,
            trace: false,
        };
        cpu.reset();
        cpu
    }

    /// Program the image into flash and configuration memory.
    pub fn load(&mut self, img: &Image) -> Result<(), String> {
        let n = self.flash.len() as u32;
        for (&addr, &w) in &img.words {
            if addr < n {
                self.flash[addr as usize] = w & 0x3FFF;
                self.programmed[addr as usize] = true;
            } else if self.dev.config.iter().any(|c| c.addr == addr) {
                self.config.push((addr, w & 0x3FFF));
            } else if (0x8000..0x8004).contains(&addr) || addr >= 0xF000 {
                // User ID and EEPROM: accepted, not modelled.
            } else {
                return Err(format!(
                    "image has a word at 0x{addr:04X}, outside the {} words of {} flash",
                    n, self.dev.name
                ));
            }
        }
        Ok(())
    }

    /// Power-on reset.
    pub fn reset(&mut self) {
        for b in self.ram.iter_mut() {
            *b = 0;
        }
        for s in &self.dev.sfrs {
            self.ram[s.addr as usize] = s.por & s.impl_mask;
        }
        self.pc = 0;
        self.stkptr = 0x1F;
        self.t0_prescale = 0;
        self.t0_inhibit = 0;
        self.t2_prescale = 0;
        self.t2_postscale = 0;
    }

    pub fn config_word(&self, name: &str) -> Option<u16> {
        let c = self.dev.config.iter().find(|c| c.name == name)?;
        Some(self.config.iter().rev().find(|(a, _)| *a == c.addr).map(|(_, v)| *v).unwrap_or(c.default))
    }

    pub fn flash_word(&self, addr: u16) -> u16 {
        self.flash.get(addr as usize).copied().unwrap_or(0x3FFF)
    }

    pub fn w(&self) -> u8 {
        self.ram[WREG as usize]
    }
    pub fn set_w(&mut self, v: u8) {
        self.ram[WREG as usize] = v;
    }
    pub fn status(&self) -> u8 {
        self.ram[STATUS as usize]
    }
    pub fn bsr(&self) -> u8 {
        self.ram[BSR as usize]
    }
    pub fn fsr(&self, n: u8) -> u16 {
        let base = if n == 0 { FSR0L } else { FSR1L } as usize;
        self.ram[base] as u16 | ((self.ram[base + 1] as u16) << 8)
    }
    fn set_fsr(&mut self, n: u8, v: u16) {
        let base = if n == 0 { FSR0L } else { FSR1L } as usize;
        self.ram[base] = v as u8;
        self.ram[base + 1] = (v >> 8) as u8;
    }
    pub fn stack_depth(&self) -> u8 {
        self.stkptr.wrapping_add(1) & 0x1F
    }

    /// Value the program left on the exit port (LATD), used as its exit code.
    pub fn exit_code(&self) -> Option<u8> {
        self.p.exit_port.map(|a| self.ram[a as usize])
    }

    /// Fold a banked address onto the canonical copy of mirrored registers.
    fn canon(&self, addr: u16) -> u16 {
        let off = addr & 0x7F;
        if off < self.dev.core_register_count || off >= self.dev.common_ram.begin {
            off
        } else {
            addr & 0x0FFF
        }
    }

    /// Read a data-memory byte by traditional address, without side effects.
    pub fn peek(&self, addr: u16) -> u8 {
        let a = self.canon(addr) as usize;
        self.ram[a] & self.impl_mask[a]
    }

    /// Write a data-memory byte by traditional address, bypassing peripherals.
    pub fn poke(&mut self, addr: u16, v: u8) {
        let a = self.canon(addr) as usize;
        self.ram[a] = v;
    }

    fn read_reg(&mut self, a: u16) -> u8 {
        let ai = a as usize;
        if self.is_gpr[ai] {
            return self.ram[ai];
        }
        match a {
            INDF0 => return self.read_indirect(self.fsr(0)),
            INDF1 => return self.read_indirect(self.fsr(1)),
            PCL => return self.pc as u8,
            _ => {}
        }
        if Some(a) == self.p.stkptr {
            return self.stkptr;
        }
        if self.p.stkptr.is_some() && (a == self.p.tosl || a == self.p.tosh) {
            let tos = self.stack[(self.stkptr as usize) % self.stack.len()];
            return if a == self.p.tosl { tos as u8 } else { (tos >> 8) as u8 };
        }
        if self.p.txreg.is_some() {
            if a == self.p.pir_tx.0 {
                let mut v = self.ram[ai] & !self.p.pir_tx.1;
                if self.tx_ready() {
                    v |= self.p.pir_tx.1;
                }
                return v;
            }
            if a == self.p.txsta {
                let mut v = self.ram[ai] & !self.p.trmt;
                if self.cycles >= self.tx_ready_at {
                    v |= self.p.trmt;
                }
                return v;
            }
        }
        for port in &self.p.ports {
            if a == port.port {
                // Output pins read back the latch; inputs are held low.
                return self.ram[port.lat as usize] & !self.ram[port.tris as usize];
            }
        }
        self.ram[ai] & self.impl_mask[ai]
    }

    fn write_reg(&mut self, a: u16, v: u8) {
        let ai = a as usize;
        if self.is_gpr[ai] {
            self.ram[ai] = v;
            return;
        }
        match a {
            INDF0 => return self.write_indirect(self.fsr(0), v),
            INDF1 => return self.write_indirect(self.fsr(1), v),
            PCL => {
                self.pc = (((self.ram[PCLATH as usize] & 0x7F) as u16) << 8) | v as u16;
                self.pc_changed = true;
                return;
            }
            _ => {}
        }
        if Some(a) == self.p.stkptr {
            self.stkptr = v & 0x1F;
            return;
        }
        if self.p.stkptr.is_some() && (a == self.p.tosl || a == self.p.tosh) {
            let i = (self.stkptr as usize) % self.stack.len();
            let tos = self.stack[i];
            self.stack[i] = if a == self.p.tosl {
                (tos & 0x7F00) | v as u16
            } else {
                (tos & 0x00FF) | (((v & 0x7F) as u16) << 8)
            };
            return;
        }
        for i in 0..self.p.ports.len() {
            if a == self.p.ports[i].port {
                let lat = self.p.ports[i].lat as usize;
                self.ram[lat] = v & self.impl_mask[lat];
                return;
            }
        }
        let wm = self.write_mask[ai];
        self.ram[ai] = (self.ram[ai] & !wm) | (v & wm);
        if Some(a) == self.p.tmr0 {
            self.t0_prescale = 0;
            self.t0_inhibit = 2;
        } else if Some(a) == self.p.txreg {
            self.uart_tx(v);
        } else if Some(a) == self.p.tmr2 {
            self.t2_prescale = 0;
            self.t2_postscale = 0;
        }
    }

    fn read_indirect(&mut self, fsr: u16) -> u8 {
        if fsr < 0x1000 {
            let a = self.canon(fsr);
            if a == INDF0 || a == INDF1 {
                return 0;
            }
            self.read_reg(a)
        } else if fsr >= FLASH_VIEW {
            self.flash_word(fsr & 0x7FFF) as u8
        } else if fsr >= LINEAR_BASE {
            match self.dev.linear_to_traditional(fsr - LINEAR_BASE) {
                Some(a) => self.ram[a as usize],
                None => 0,
            }
        } else {
            0
        }
    }

    fn write_indirect(&mut self, fsr: u16, v: u8) {
        if fsr < 0x1000 {
            let a = self.canon(fsr);
            if a != INDF0 && a != INDF1 {
                self.write_reg(a, v);
            }
        } else if (LINEAR_BASE..FLASH_VIEW).contains(&fsr) {
            if let Some(a) = self.dev.linear_to_traditional(fsr - LINEAR_BASE) {
                self.ram[a as usize] = v;
            }
        }
    }

    fn tx_ready(&self) -> bool {
        self.ram[self.p.txsta as usize] & self.p.txen != 0 && self.cycles >= self.tx_ready_at
    }

    fn uart_tx(&mut self, v: u8) {
        let enabled = self.ram[self.p.txsta as usize] & self.p.txen != 0
            && self.ram[self.p.rcsta as usize] & self.p.spen != 0;
        if !enabled {
            return;
        }
        if self.cycles < self.tx_ready_at {
            self.uart_overruns += 1;
            return;
        }
        self.uart_out.push(v);
        if self.uart_mode == UartMode::Timed {
            let n = self.ram[self.p.spbrgl as usize] as u64;
            let brgh = self.ram[self.p.txsta as usize] & self.p.brgh != 0;
            let brg16 = self.ram[self.p.baudcon as usize] & self.p.brg16 != 0;
            let n = if brg16 { n | ((self.ram[self.p.spbrgh as usize] as u64) << 8) } else { n };
            // Baud = Fosc / (64|16|4 * (n+1)); in Tcy that is 16, 4 or 1 per (n+1).
            let per_bit = match (brg16, brgh) {
                (false, false) => 16,
                (true, true) => 1,
                _ => 4,
            } * (n + 1);
            self.tx_ready_at = self.cycles + 10 * per_bit;
        }
    }

    fn push(&mut self, addr: u16) -> Result<(), Halt> {
        self.stkptr = self.stkptr.wrapping_add(1) & 0x1F;
        if self.stkptr as usize >= self.stack.len() {
            return Err(Halt::StackOverflow);
        }
        self.stack[self.stkptr as usize] = addr & 0x7FFF;
        let d = self.stack_depth();
        if d > self.max_stack_depth {
            self.max_stack_depth = d;
        }
        Ok(())
    }

    fn pop(&mut self) -> Result<u16, Halt> {
        if self.stkptr as usize >= self.stack.len() {
            return Err(Halt::StackUnderflow);
        }
        let v = self.stack[self.stkptr as usize];
        self.stkptr = self.stkptr.wrapping_sub(1) & 0x1F;
        Ok(v)
    }

    fn set_flags(&mut self, mask: u8, value: u8) {
        let s = &mut self.ram[STATUS as usize];
        *s = (*s & !mask) | (value & mask);
    }

    fn set_z(&mut self, v: u8) {
        self.set_flags(FLAG_Z, if v == 0 { FLAG_Z } else { 0 });
    }

    /// a + b + carry_in, setting C, DC and Z.
    fn add(&mut self, a: u8, b: u8, cin: u8) -> u8 {
        let r = a as u16 + b as u16 + cin as u16;
        let mut f = 0;
        if r > 0xFF {
            f |= FLAG_C;
        }
        if (a & 0xF) + (b & 0xF) + cin > 0xF {
            f |= FLAG_DC;
        }
        if r as u8 == 0 {
            f |= FLAG_Z;
        }
        self.set_flags(FLAG_C | FLAG_DC | FLAG_Z, f);
        r as u8
    }

    fn file_addr(&self, f: u8) -> u16 {
        self.canon(((self.bsr() as u16 & 0x1F) << 7) | f as u16)
    }

    /// Store an ALU result to W or back to the file register.
    fn store(&mut self, a: u16, to_file: bool, v: u8) {
        if !to_file {
            self.set_w(v);
        } else if a == STATUS {
            // The instruction's own flag updates take priority over the data write.
            let keep = FLAG_C | FLAG_DC | FLAG_Z | STATUS_NPD | STATUS_NTO;
            let flags = self.ram[STATUS as usize] & keep;
            self.ram[STATUS as usize] = (v & !keep) & self.impl_mask[STATUS as usize] | flags;
        } else {
            self.write_reg(a, v);
        }
    }

    fn tick(&mut self, n: u64) {
        for _ in 0..n {
            self.cycles += 1;
            self.tick_timer0();
            self.tick_timer2();
        }
    }

    fn tick_timer0(&mut self) {
        let (Some(tmr0), Some(opt)) = (self.p.tmr0, self.p.option_reg) else { return };
        let o = self.ram[opt as usize];
        if o & self.p.opt_cs != 0 {
            return; // external clock on T0CKI: no edges are supplied
        }
        if self.t0_inhibit > 0 {
            self.t0_inhibit -= 1;
            return;
        }
        let div: u16 = if o & self.p.opt_psa != 0 { 1 } else { 2 << ((o >> self.p.opt_ps_shift) & 7) };
        self.t0_prescale += 1;
        if self.t0_prescale >= div {
            self.t0_prescale = 0;
            let v = self.ram[tmr0 as usize].wrapping_add(1);
            self.ram[tmr0 as usize] = v;
            if v == 0 {
                self.ram[INTCON as usize] |= self.p.tmr0if;
            }
        }
    }

    fn tick_timer2(&mut self) {
        let Some(tmr2) = self.p.tmr2 else { return };
        let c = self.ram[self.p.t2con as usize];
        if c & self.p.t2_on == 0 {
            return;
        }
        let div: u16 = match (c >> self.p.t2_ckps_shift) & 3 {
            0 => 1,
            1 => 4,
            2 => 16,
            _ => 64,
        };
        self.t2_prescale += 1;
        if self.t2_prescale < div {
            return;
        }
        self.t2_prescale = 0;
        if self.ram[tmr2 as usize] == self.ram[self.p.pr2 as usize] {
            self.ram[tmr2 as usize] = 0;
            self.t2_postscale += 1;
            if self.t2_postscale > (c >> self.p.t2_outps_shift) & 0xF {
                self.t2_postscale = 0;
                self.ram[self.p.pir_tmr2.0 as usize] |= self.p.pir_tmr2.1;
            }
        } else {
            self.ram[tmr2 as usize] = self.ram[tmr2 as usize].wrapping_add(1);
        }
    }

    fn interrupt_pending(&mut self) -> bool {
        let ic = self.ram[INTCON as usize];
        for &(en, fl) in &self.p.intcon_sources {
            if ic & en != 0 && ic & fl != 0 {
                return true;
            }
        }
        if ic & self.p.peie != 0 {
            for i in 0..self.p.pie_pir.len() {
                let (pie, pir) = self.p.pie_pir[i];
                if self.ram[pie as usize] & self.read_reg(pir) != 0 {
                    return true;
                }
            }
        }
        false
    }

    fn enter_interrupt(&mut self) -> Result<(), Halt> {
        self.push(self.pc)?;
        if let Some(sh) = self.p.shadow {
            let src = [WREG, STATUS, BSR, PCLATH, FSR0L, FSR0H, FSR1L, FSR1H];
            for i in 0..8 {
                self.ram[sh[i] as usize] = self.ram[src[i] as usize];
            }
        }
        self.ram[INTCON as usize] &= !self.p.gie;
        self.pc = 0x0004;
        self.interrupts_taken += 1;
        self.tick(INTERRUPT_ENTRY_CYCLES);
        Ok(())
    }

    /// Run until something stops the program or `max_cycles` is reached.
    pub fn run(&mut self, max_cycles: u64) -> Halt {
        loop {
            if self.cycles >= max_cycles {
                return Halt::MaxCycles;
            }
            if let Err(h) = self.step() {
                return h;
            }
        }
    }

    /// Execute one instruction, then take a pending interrupt if enabled.
    pub fn step(&mut self) -> Result<(), Halt> {
        let pc = self.pc;
        if pc as usize >= self.flash.len() || !self.programmed[pc as usize] {
            return Err(Halt::Unprogrammed(pc));
        }
        let word = self.flash[pc as usize];
        if self.trace {
            eprintln!(
                "{:>10}  {:04X}  {:04X}  {:<22} W={:02X} ST={:02X} BSR={:02X} FSR0={:04X} FSR1={:04X}",
                self.cycles,
                pc,
                word,
                crate::disasm::disasm(pc, word),
                self.w(),
                self.status(),
                self.bsr(),
                self.fsr(0),
                self.fsr(1)
            );
        }
        if let Some(p) = self.profile.as_mut() {
            p[pc as usize] += 1;
        }
        self.pc = (pc + 1) & 0x7FFF;
        self.pc_changed = false;
        self.instructions += 1;
        let mut cyc: u64 = 1;
        let skip = |cpu: &mut Cpu, cyc: &mut u64| {
            cpu.pc = (cpu.pc + 1) & 0x7FFF;
            *cyc = 2;
        };
        let carry = self.status() & FLAG_C;

        match decode(word) {
            Op::Nop | Op::Clrwdt => {}
            Op::Reset => return Err(Halt::Reset),
            Op::Return => {
                self.pc = self.pop()?;
                cyc = 2;
            }
            Op::Retfie => {
                self.pc = self.pop()?;
                if let Some(sh) = self.p.shadow {
                    let dst = [WREG, STATUS, BSR, PCLATH, FSR0L, FSR0H, FSR1L, FSR1H];
                    for i in 0..8 {
                        self.ram[dst[i] as usize] = self.ram[sh[i] as usize];
                    }
                }
                self.ram[INTCON as usize] |= self.p.gie;
                cyc = 2;
            }
            Op::Callw => {
                self.push(self.pc)?;
                self.pc = (((self.ram[PCLATH as usize] & 0x7F) as u16) << 8) | self.w() as u16;
                cyc = 2;
            }
            Op::Brw => {
                self.pc = (self.pc + self.w() as u16) & 0x7FFF;
                cyc = 2;
            }
            Op::MoviwMode { n, mode } => {
                let mut f = self.fsr(n);
                match mode {
                    0 => f = f.wrapping_add(1),
                    1 => f = f.wrapping_sub(1),
                    _ => {}
                }
                let v = self.read_indirect(f);
                match mode {
                    2 => f = f.wrapping_add(1),
                    3 => f = f.wrapping_sub(1),
                    _ => {}
                }
                self.set_fsr(n, f);
                self.set_w(v);
                self.set_z(v);
            }
            Op::MovwiMode { n, mode } => {
                let mut f = self.fsr(n);
                match mode {
                    0 => f = f.wrapping_add(1),
                    1 => f = f.wrapping_sub(1),
                    _ => {}
                }
                let w = self.w();
                self.write_indirect(f, w);
                match mode {
                    2 => f = f.wrapping_add(1),
                    3 => f = f.wrapping_sub(1),
                    _ => {}
                }
                self.set_fsr(n, f);
            }
            Op::MoviwOff { n, k } => {
                let v = self.read_indirect(self.fsr(n).wrapping_add(k as u16));
                self.set_w(v);
                self.set_z(v);
            }
            Op::MovwiOff { n, k } => {
                let w = self.w();
                self.write_indirect(self.fsr(n).wrapping_add(k as u16), w);
            }
            Op::Movlb(k) => self.ram[BSR as usize] = k,
            Op::Option => {
                if let Some(o) = self.p.option_reg {
                    self.ram[o as usize] = self.w();
                }
            }
            Op::Tris(f) => {
                // TRIS 5/6/7 load TRISA/B/C.
                let idx = (f as usize).wrapping_sub(5);
                if let Some(port) = self.p.ports.get(idx) {
                    let t = port.tris;
                    let w = self.w();
                    self.write_reg(t, w);
                }
            }
            Op::Sleep => {
                self.set_flags(STATUS_NPD | STATUS_NTO, STATUS_NTO);
                self.tick(1);
                // No modelled peripheral runs in sleep, so nothing can wake the device.
                return Err(Halt::Sleep);
            }
            Op::Movwf(f) => {
                let a = self.file_addr(f);
                let w = self.w();
                self.write_reg(a, w);
            }
            Op::Clrw => {
                self.set_w(0);
                self.set_flags(FLAG_Z, FLAG_Z);
            }
            Op::Clrf(f) => {
                let a = self.file_addr(f);
                self.set_flags(FLAG_Z, FLAG_Z);
                self.store(a, true, 0);
            }
            Op::Subwf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = self.add(v, !self.w(), 1);
                self.store(a, d, r);
            }
            Op::Subwfb(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = self.add(v, !self.w(), carry);
                self.store(a, d, r);
            }
            Op::Addwf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = self.add(v, self.w(), 0);
                self.store(a, d, r);
            }
            Op::Addwfc(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = self.add(v, self.w(), carry);
                self.store(a, d, r);
            }
            Op::Decf(f, d) | Op::Incf(f, d) | Op::Comf(f, d) | Op::Movf(f, d)
            | Op::Iorwf(f, d) | Op::Andwf(f, d) | Op::Xorwf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = match decode(word) {
                    Op::Decf(..) => v.wrapping_sub(1),
                    Op::Incf(..) => v.wrapping_add(1),
                    Op::Comf(..) => !v,
                    Op::Movf(..) => v,
                    Op::Iorwf(..) => v | self.w(),
                    Op::Andwf(..) => v & self.w(),
                    _ => v ^ self.w(),
                };
                self.set_z(r);
                self.store(a, d, r);
            }
            Op::Decfsz(f, d) | Op::Incfsz(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = if matches!(decode(word), Op::Decfsz(..)) { v.wrapping_sub(1) } else { v.wrapping_add(1) };
                self.store(a, d, r);
                if r == 0 {
                    skip(self, &mut cyc);
                }
            }
            Op::Rrf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                self.set_flags(FLAG_C, v & 1);
                self.store(a, d, (v >> 1) | (carry << 7));
            }
            Op::Rlf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                self.set_flags(FLAG_C, v >> 7);
                self.store(a, d, (v << 1) | carry);
            }
            Op::Swapf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                self.store(a, d, (v << 4) | (v >> 4));
            }
            Op::Lslf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = v << 1;
                self.set_flags(FLAG_C | FLAG_Z, (v >> 7) | if r == 0 { FLAG_Z } else { 0 });
                self.store(a, d, r);
            }
            Op::Lsrf(f, d) | Op::Asrf(f, d) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                let r = if matches!(decode(word), Op::Asrf(..)) { ((v as i8) >> 1) as u8 } else { v >> 1 };
                self.set_flags(FLAG_C | FLAG_Z, (v & 1) | if r == 0 { FLAG_Z } else { 0 });
                self.store(a, d, r);
            }
            Op::Bcf(f, b) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                self.write_reg(a, v & !(1 << b));
            }
            Op::Bsf(f, b) => {
                let a = self.file_addr(f);
                let v = self.read_reg(a);
                self.write_reg(a, v | (1 << b));
            }
            Op::Btfsc(f, b) => {
                let a = self.file_addr(f);
                if self.read_reg(a) & (1 << b) == 0 {
                    skip(self, &mut cyc);
                }
            }
            Op::Btfss(f, b) => {
                let a = self.file_addr(f);
                if self.read_reg(a) & (1 << b) != 0 {
                    skip(self, &mut cyc);
                }
            }
            Op::Call(k) => {
                self.push(self.pc)?;
                self.pc = k | (((self.ram[PCLATH as usize] & 0x78) as u16) << 8);
                cyc = 2;
            }
            Op::Goto(k) => {
                self.pc = k | (((self.ram[PCLATH as usize] & 0x78) as u16) << 8);
                cyc = 2;
            }
            Op::Movlw(k) => self.set_w(k),
            Op::Addfsr { n, k } => {
                let f = self.fsr(n).wrapping_add(k as u16);
                self.set_fsr(n, f);
            }
            Op::Movlp(k) => self.ram[PCLATH as usize] = k,
            Op::Bra(k) => {
                self.pc = (self.pc as i32 + k as i32) as u16 & 0x7FFF;
                cyc = 2;
            }
            Op::Retlw(k) => {
                self.set_w(k);
                self.pc = self.pop()?;
                cyc = 2;
            }
            Op::Iorlw(k) => {
                let r = self.w() | k;
                self.set_w(r);
                self.set_z(r);
            }
            Op::Andlw(k) => {
                let r = self.w() & k;
                self.set_w(r);
                self.set_z(r);
            }
            Op::Xorlw(k) => {
                let r = self.w() ^ k;
                self.set_w(r);
                self.set_z(r);
            }
            Op::Sublw(k) => {
                let r = self.add(k, !self.w(), 1);
                self.set_w(r);
            }
            Op::Addlw(k) => {
                let r = self.add(k, self.w(), 0);
                self.set_w(r);
            }
            Op::Illegal(w) => return Err(Halt::Illegal { pc, word: w }),
        }
        if self.pc_changed {
            cyc = 2;
        }
        self.tick(cyc);
        if self.ram[INTCON as usize] & self.p.gie != 0 && self.interrupt_pending() {
            self.enter_interrupt()?;
        }
        Ok(())
    }
}

impl Periph {
    fn lookup(dev: &Device) -> Periph {
        let mut p = Periph::default();
        let fm = |sfr: &str, names: &[&str]| -> u8 {
            names.iter().find_map(|n| dev.field_mask(sfr, n)).unwrap_or(0)
        };
        let shift = |sfr: &str, names: &[&str]| -> u8 {
            let s = dev.sfr(sfr);
            names.iter().find_map(|n| s.and_then(|s| s.field(n)).map(|f| f.bit)).unwrap_or(0)
        };
        p.gie = fm("INTCON", &["GIE"]);
        p.peie = fm("INTCON", &["PEIE"]);
        for (en, fl) in [("TMR0IE", "TMR0IF"), ("INTE", "INTF"), ("IOCIE", "IOCIF")] {
            let (e, f) = (fm("INTCON", &[en]), fm("INTCON", &[fl]));
            if e != 0 && f != 0 {
                p.intcon_sources.push((e, f));
            }
        }
        p.tmr0if = fm("INTCON", &["TMR0IF"]);
        for n in 1..=9 {
            if let (Some(pie), Some(pir)) = (dev.sfr_addr(&format!("PIE{n}")), dev.sfr_addr(&format!("PIR{n}"))) {
                p.pie_pir.push((pie, pir));
            }
        }
        p.tmr0 = dev.sfr_addr("TMR0");
        p.option_reg = dev.sfr_addr("OPTION_REG");
        p.opt_cs = fm("OPTION_REG", &["TMR0CS", "T0CS"]);
        p.opt_psa = fm("OPTION_REG", &["PSA"]);
        p.opt_ps_shift = shift("OPTION_REG", &["PS", "PS0"]);
        if let (Some(tmr2), Some(pr2), Some(t2con), Some(pir1)) =
            (dev.sfr_addr("TMR2"), dev.sfr_addr("PR2"), dev.sfr_addr("T2CON"), dev.sfr_addr("PIR1"))
        {
            p.tmr2 = Some(tmr2);
            p.pr2 = pr2;
            p.t2con = t2con;
            p.t2_on = fm("T2CON", &["TMR2ON"]);
            p.t2_ckps_shift = shift("T2CON", &["T2CKPS", "T2CKPS0"]);
            p.t2_outps_shift = shift("T2CON", &["T2OUTPS", "T2OUTPS0"]);
            p.pir_tmr2 = (pir1, fm("PIR1", &["TMR2IF"]));
        }
        let spbrgl = dev.sfr_addr("SPBRGL").or(dev.sfr_addr("SPBRG")).or(dev.sfr_addr("SP1BRGL"));
        if let (Some(txreg), Some(txsta), Some(rcsta), Some(pir1), Some(brgl)) = (
            dev.sfr_addr("TXREG"),
            dev.sfr_addr("TXSTA"),
            dev.sfr_addr("RCSTA"),
            dev.sfr_addr("PIR1"),
            spbrgl,
        ) {
            p.txreg = Some(txreg);
            p.txsta = txsta;
            p.rcsta = rcsta;
            p.spbrgl = brgl;
            p.spbrgh = dev.sfr_addr("SPBRGH").or(dev.sfr_addr("SP1BRGH")).unwrap_or(brgl);
            p.baudcon = dev.sfr_addr("BAUDCON").unwrap_or(txsta);
            p.txen = fm("TXSTA", &["TXEN"]);
            p.trmt = fm("TXSTA", &["TRMT"]);
            p.brgh = fm("TXSTA", &["BRGH"]);
            p.spen = fm("RCSTA", &["SPEN"]);
            p.brg16 = fm("BAUDCON", &["BRG16"]);
            p.pir_tx = (pir1, fm("PIR1", &["TXIF"]));
        }
        for letter in ["A", "B", "C", "D", "E", "F", "G"] {
            if let (Some(port), Some(lat), Some(tris)) = (
                dev.sfr_addr(&format!("PORT{letter}")),
                dev.sfr_addr(&format!("LAT{letter}")),
                dev.sfr_addr(&format!("TRIS{letter}")),
            ) {
                p.ports.push(Port { port, lat, tris });
            }
        }
        p.stkptr = dev.sfr_addr("STKPTR");
        p.tosl = dev.sfr_addr("TOSL").unwrap_or(0);
        p.tosh = dev.sfr_addr("TOSH").unwrap_or(0);
        let names = ["WREG_SHAD", "STATUS_SHAD", "BSR_SHAD", "PCLATH_SHAD", "FSR0L_SHAD", "FSR0H_SHAD", "FSR1L_SHAD", "FSR1H_SHAD"];
        let addrs: Vec<u16> = names.iter().filter_map(|n| dev.sfr_addr(n)).collect();
        if addrs.len() == 8 {
            p.shadow = Some([addrs[0], addrs[1], addrs[2], addrs[3], addrs[4], addrs[5], addrs[6], addrs[7]]);
        }
        p.exit_port = dev.sfr_addr("LATD");
        p
    }
}
