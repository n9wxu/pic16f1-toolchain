//! A gdb-remote server for the simulator, for LLDB (or gdb).
//!
//! The address space is the ELF file's, which is also what the debug
//! information uses (docs/spec/debug-interface.md):
//!
//! | Addresses          | Content                                          |
//! |--------------------|--------------------------------------------------|
//! | 0x000000 up        | program memory, two bytes per word, low first    |
//! | 0x010000 up        | user IDs and configuration words, likewise       |
//! | 0x800000-0x801FFF  | data memory by banked address                    |
//! | 0x802000-0x807FFF  | data memory, linear view                         |
//! | 0x808000-0x80FFFF  | program memory as an FSR reads it, a byte a word |
//! | 0x810000 up        | hardware return stack, two bytes per level       |
//!
//! Registers, in this order: pc (32 bits: a byte address, twice the word
//! address), w, status, bsr, pclath, fsr0 (16), fsr1 (16), stkptr, then the
//! compiler's registers r0-r13, tmp0 and tmp1, which are the bytes at
//! 0x70-0x7F of data memory.

use crate::cpu::{Cpu, Halt, BSR, FSR0L, FSR1L, PCLATH, STATUS, WREG};
use std::collections::BTreeSet;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};

const DATA_BASE: u32 = 0x80_0000;
const STACK_BASE: u32 = 0x81_0000;
const CONFIG_BASE: u32 = 0x1_0000;

/// (name, bits)
const REGS: &[(&str, u8)] = &[
    ("pc", 32),
    ("w", 8),
    ("status", 8),
    ("bsr", 8),
    ("pclath", 8),
    ("fsr0", 16),
    ("fsr1", 16),
    ("stkptr", 8),
    ("r0", 8),
    ("r1", 8),
    ("r2", 8),
    ("r3", 8),
    ("r4", 8),
    ("r5", 8),
    ("r6", 8),
    ("r7", 8),
    ("r8", 8),
    ("r9", 8),
    ("r10", 8),
    ("r11", 8),
    ("r12", 8),
    ("r13", 8),
    ("tmp0", 8),
    ("tmp1", 8),
];

fn target_xml() -> String {
    let mut s = String::from(
        "<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n<target version=\"1.0\">\n\
         <architecture>pic16f1</architecture>\n<feature name=\"org.gnu.gdb.pic16f1.core\">\n",
    );
    for (n, (name, bits)) in REGS.iter().enumerate() {
        let (ty, extra) = match *name {
            "pc" => ("code_ptr", " generic=\"pc\""),
            "fsr0" | "fsr1" => ("data_ptr", ""),
            _ => ("int", ""),
        };
        let group = if n < 8 { "general" } else { "compiler" };
        s.push_str(&format!(
            "<reg name=\"{name}\" bitsize=\"{bits}\" regnum=\"{n}\" type=\"{ty}\" group=\"{group}\"{extra}/>\n"
        ));
    }
    s.push_str("</feature>\n</target>\n");
    s
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn unhex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok()).collect()
}

pub struct Server {
    cpu: Cpu,
    breakpoints: BTreeSet<u16>,
    /// Ranges of data memory (start as an FSR sees it, length) that stop the
    /// program when written.
    watch_write: Vec<(u16, u16)>,
    uart_sent: usize,
    no_ack: bool,
    exited: Option<u8>,
    /// Answer as a plain server without threads does (OpenOCD, for one), to
    /// check that the debugger copes: set by PIC16F1_SIM_PLAIN_SERVER.
    plain: bool,
    /// Behave as the silicon's debug hardware does: one breakpoint, which
    /// stops the program after the instruction at its address has run; no
    /// watchpoints; a pc that cannot be written. Set by
    /// PIC16F1_SIM_SILICON, which implies the plain server.
    silicon: bool,
}

enum Stop {
    Trap,
    Interrupted,
    Exited(u8),
    Fault(u8, String),
    Watch(u16),
}

impl Server {
    pub fn new(cpu: Cpu) -> Server {
        let silicon = std::env::var_os("PIC16F1_SIM_SILICON").is_some();
        Server { cpu, breakpoints: BTreeSet::new(), watch_write: Vec::new(), uart_sent: 0, no_ack: false, exited: None, plain: std::env::var_os("PIC16F1_SIM_PLAIN_SERVER").is_some() || silicon, silicon }
    }

    /// Listen on the loopback interface and serve one debugger.
    pub fn serve(mut self, port: u16) -> Result<u8, String> {
        let listener = TcpListener::bind(("127.0.0.1", port)).map_err(|e| format!("port {port}: {e}"))?;
        let port = listener.local_addr().map(|a| a.port()).unwrap_or(port);
        eprintln!("pic16f1-sim: {} waiting for a debugger on port {port}", self.cpu.dev.name);
        let (mut stream, _) = listener.accept().map_err(|e| e.to_string())?;
        stream.set_nodelay(true).ok();
        loop {
            let Some(packet) = self.read_packet(&mut stream)? else {
                return Ok(self.exited.unwrap_or(0));
            };
            match self.handle(&packet, &mut stream)? {
                Some(reply) => self.send(&mut stream, &reply)?,
                None => return Ok(self.exited.unwrap_or(0)),
            }
        }
    }

    fn send(&mut self, stream: &mut TcpStream, body: &str) -> Result<(), String> {
        let sum = body.bytes().fold(0u8, |a, b| a.wrapping_add(b));
        stream.write_all(format!("${body}#{sum:02x}").as_bytes()).map_err(|e| e.to_string())
    }

    /// The next packet's body; None when the debugger has gone.
    fn read_packet(&mut self, stream: &mut TcpStream) -> Result<Option<String>, String> {
        let mut byte = [0u8; 1];
        loop {
            // Skip acknowledgements and stray interrupt bytes up to '$'.
            loop {
                match stream.read(&mut byte) {
                    Ok(0) => return Ok(None),
                    Ok(_) if byte[0] == b'$' => break,
                    Ok(_) => {}
                    Err(e) => return Err(e.to_string()),
                }
            }
            let mut body = Vec::new();
            loop {
                match stream.read(&mut byte) {
                    Ok(0) => return Ok(None),
                    Ok(_) if byte[0] == b'#' => break,
                    Ok(_) => body.push(byte[0]),
                    Err(e) => return Err(e.to_string()),
                }
            }
            let mut sum = [0u8; 2];
            stream.read_exact(&mut sum).map_err(|e| e.to_string())?;
            let want = u8::from_str_radix(std::str::from_utf8(&sum).unwrap_or("zz"), 16).ok();
            let got = body.iter().fold(0u8, |a, &b| a.wrapping_add(b));
            if want == Some(got) {
                if !self.no_ack {
                    stream.write_all(b"+").map_err(|e| e.to_string())?;
                }
                return Ok(Some(String::from_utf8_lossy(&body).into_owned()));
            }
            if !self.no_ack {
                stream.write_all(b"-").map_err(|e| e.to_string())?;
            }
        }
    }

    // --- Memory -------------------------------------------------------------

    fn read_byte(&self, addr: u32) -> Option<u8> {
        let flash_bytes = self.cpu.flash_words() as u32 * 2;
        if addr < flash_bytes {
            let w = self.cpu.flash_word((addr / 2) as u16);
            Some(if addr % 2 == 0 { w as u8 } else { (w >> 8) as u8 })
        } else if (CONFIG_BASE..CONFIG_BASE + 0x40).contains(&addr) {
            let word = 0x8000 + (addr - CONFIG_BASE) / 2;
            let w = self.cpu.config.iter().find(|c| c.0 == word).map(|c| c.1).unwrap_or(0x3FFF);
            Some(if addr % 2 == 0 { w as u8 } else { (w >> 8) as u8 })
        } else if (DATA_BASE..DATA_BASE + 0x1_0000).contains(&addr) {
            Some(self.cpu.debug_read((addr - DATA_BASE) as u16))
        } else if addr >= STACK_BASE && addr < STACK_BASE + 2 * self.cpu.stack_levels() as u32 {
            let e = self.cpu.stack_entry(((addr - STACK_BASE) / 2) as usize);
            Some(if addr % 2 == 0 { e as u8 } else { (e >> 8) as u8 })
        } else {
            None
        }
    }

    fn write_byte(&mut self, addr: u32, v: u8) -> bool {
        let flash_bytes = self.cpu.flash_words() as u32 * 2;
        if addr < flash_bytes {
            let a = (addr / 2) as u16;
            let w = self.cpu.flash_word(a);
            let w = if addr % 2 == 0 { (w & 0xFF00) | v as u16 } else { (w & 0x00FF) | ((v as u16) << 8) };
            self.cpu.set_flash_word(a, w);
            true
        } else if (DATA_BASE..DATA_BASE + 0x1_0000).contains(&addr) {
            self.cpu.debug_write((addr - DATA_BASE) as u16, v);
            true
        } else if addr >= STACK_BASE && addr < STACK_BASE + 2 * self.cpu.stack_levels() as u32 {
            let level = ((addr - STACK_BASE) / 2) as usize;
            let e = self.cpu.stack_entry(level);
            let e = if addr % 2 == 0 { (e & 0xFF00) | v as u16 } else { (e & 0x00FF) | ((v as u16) << 8) };
            self.cpu.set_stack_entry(level, e);
            true
        } else {
            false
        }
    }

    // --- Registers ----------------------------------------------------------

    fn read_reg(&self, n: usize) -> Option<Vec<u8>> {
        let c = &self.cpu;
        Some(match n {
            0 => (c.pc as u32 * 2).to_le_bytes().to_vec(),
            1 => vec![c.debug_read(WREG)],
            2 => vec![c.debug_read(STATUS)],
            3 => vec![c.debug_read(BSR)],
            4 => vec![c.debug_read(PCLATH)],
            5 => c.fsr(0).to_le_bytes().to_vec(),
            6 => c.fsr(1).to_le_bytes().to_vec(),
            7 => vec![c.stkptr()],
            8..=23 => vec![c.debug_read(0x70 + (n as u16 - 8))],
            _ => return None,
        })
    }

    fn write_reg(&mut self, n: usize, v: &[u8]) -> bool {
        let need = match REGS.get(n) {
            Some(r) => r.1 as usize / 8,
            None => return false,
        };
        if v.len() < need {
            return false;
        }
        if self.silicon && n == 0 {
            return false;
        }
        let c = &mut self.cpu;
        match n {
            0 => c.pc = (u32::from_le_bytes([v[0], v[1], v[2], v[3]]) / 2) as u16 & 0x7FFF,
            1 => c.debug_write(WREG, v[0]),
            2 => c.debug_write(STATUS, v[0]),
            3 => c.debug_write(BSR, v[0]),
            4 => c.debug_write(PCLATH, v[0]),
            5 | 6 => {
                let base = if n == 5 { FSR0L } else { FSR1L };
                c.debug_write(base, v[0]);
                c.debug_write(base + 1, v[1]);
            }
            7 => c.set_stkptr(v[0]),
            _ => c.debug_write(0x70 + (n as u16 - 8), v[0]),
        }
        true
    }

    // --- Running ------------------------------------------------------------

    fn stop_reply(&mut self, stop: Stop) -> String {
        let reply = self.stop_reply_with_thread(stop);
        if self.plain {
            reply.replace("thread:1;", "")
        } else {
            reply
        }
    }

    fn stop_reply_with_thread(&mut self, stop: Stop) -> String {
        match stop {
            Stop::Trap => "T05thread:1;".into(),
            Stop::Interrupted => "T02thread:1;".into(),
            Stop::Watch(a) => format!("T05thread:1;watch:{:x};", DATA_BASE + a as u32),
            Stop::Exited(code) => {
                self.exited = Some(code);
                format!("W{code:02x}")
            }
            Stop::Fault(sig, why) => {
                eprintln!("pic16f1-sim: {why}");
                format!("T{sig:02x}thread:1;")
            }
        }
    }

    fn halted(&self, h: Halt) -> Stop {
        let pc = self.cpu.pc;
        match h {
            Halt::Sleep => Stop::Exited(self.cpu.exit_code().unwrap_or(0)),
            Halt::MaxCycles => Stop::Interrupted,
            Halt::StackOverflow => Stop::Fault(11, format!("hardware stack overflow at 0x{pc:04X}")),
            Halt::StackUnderflow => Stop::Fault(11, format!("hardware stack underflow at 0x{pc:04X}")),
            Halt::Reset => Stop::Fault(6, format!("RESET instruction at 0x{:04X}", pc.wrapping_sub(1))),
            Halt::Unprogrammed(a) => Stop::Fault(4, format!("executed unprogrammed memory at 0x{a:04X}")),
            Halt::Illegal { pc, word } => Stop::Fault(4, format!("illegal opcode 0x{word:04X} at 0x{pc:04X}")),
        }
    }

    /// What the program has sent to the UART since the last call, as console
    /// output packets.
    fn flush_output(&mut self, stream: &mut TcpStream) -> Result<(), String> {
        if self.cpu.uart_out.len() > self.uart_sent {
            let text = hex(&self.cpu.uart_out[self.uart_sent..]);
            self.uart_sent = self.cpu.uart_out.len();
            self.send(stream, &format!("O{text}"))?;
        }
        Ok(())
    }

    /// Each watched byte with the start of its range and its value.
    fn watched(&self) -> Vec<(u16, u16, u8)> {
        let mut out = Vec::new();
        for &(start, len) in &self.watch_write {
            for a in start..start.saturating_add(len) {
                out.push((start, a, self.cpu.debug_read(a)));
            }
        }
        out
    }

    fn step_one(&mut self) -> Option<Stop> {
        let before = self.watched();
        if let Err(h) = self.cpu.step() {
            return Some(self.halted(h));
        }
        // The debugger knows a watchpoint by the address it was set at.
        for (start, a, v) in before {
            if self.cpu.debug_read(a) != v {
                return Some(Stop::Watch(start));
            }
        }
        None
    }

    fn resume(&mut self, single: bool, stream: &mut TcpStream) -> Result<String, String> {
        if self.exited.is_some() {
            let code = self.exited.unwrap();
            return Ok(format!("W{code:02x}"));
        }
        // On silicon a breakpoint at the instruction about to run lets it
        // run and stops after it.
        let late = |s: &Server| s.silicon && s.breakpoints.contains(&s.cpu.pc);
        let mut armed = late(self);
        let mut stop = self.step_one();
        if stop.is_none() && (single || armed) {
            stop = Some(Stop::Trap);
        }
        let mut count = 0u32;
        stream.set_nonblocking(true).ok();
        while stop.is_none() {
            if !self.silicon && self.breakpoints.contains(&self.cpu.pc) {
                stop = Some(Stop::Trap);
                break;
            }
            armed = late(self);
            stop = self.step_one();
            if stop.is_none() && armed {
                stop = Some(Stop::Trap);
            }
            count += 1;
            if count % 2048 == 0 {
                // An interrupt request is a single 0x03 byte.
                let mut byte = [0u8; 1];
                if let Ok(1) = stream.read(&mut byte) {
                    if byte[0] == 3 {
                        stop = Some(Stop::Interrupted);
                    }
                }
                if self.cpu.uart_out.len() > self.uart_sent {
                    stream.set_nonblocking(false).ok();
                    self.flush_output(stream)?;
                    stream.set_nonblocking(true).ok();
                }
            }
        }
        stream.set_nonblocking(false).ok();
        self.flush_output(stream)?;
        Ok(self.stop_reply(stop.unwrap()))
    }

    // --- Packets ------------------------------------------------------------

    /// The reply to a packet; None to end the session.
    fn handle(&mut self, p: &str, stream: &mut TcpStream) -> Result<Option<String>, String> {
        let ok = || Ok(Some("OK".to_string()));
        let err = || Ok(Some("E01".to_string()));
        let unsupported = || Ok(Some(String::new()));
        let num = |s: &str| u32::from_str_radix(s, 16).ok();

        if p == "?" {
            return Ok(Some(match self.exited {
                Some(code) => format!("W{code:02x}"),
                None if self.plain => "S05".into(),
                None => "T05thread:1;".into(),
            }));
        }
        if p.starts_with("qSupported") {
            return Ok(Some("PacketSize=4000;qXfer:features:read+;QStartNoAckMode+;vContSupported+".into()));
        }
        if p == "QStartNoAckMode" {
            self.no_ack = true;
            // This reply is still acknowledged by the old rules.
            return ok();
        }
        if let Some(rest) = p.strip_prefix("qXfer:features:read:target.xml:") {
            let xml = target_xml();
            let mut it = rest.split(',');
            let (Some(off), Some(len)) = (it.next().and_then(num), it.next().and_then(num)) else { return err() };
            let (off, len) = (off as usize, len as usize);
            if off >= xml.len() {
                return Ok(Some("l".into()));
            }
            let end = (off + len).min(xml.len());
            let more = if end < xml.len() { 'm' } else { 'l' };
            return Ok(Some(format!("{more}{}", &xml[off..end])));
        }
        if self.plain && (p == "qHostInfo" || p == "qProcessInfo") {
            return unsupported();
        }
        if self.plain && p == "qC" {
            return Ok(Some("QC0".into()));
        }
        if self.plain && p == "qfThreadInfo" {
            return Ok(Some("l".into()));
        }
        if p == "qHostInfo" || p == "qProcessInfo" {
            let triple = hex(b"pic16f1-unknown-unknown");
            let pid = if p == "qProcessInfo" { "pid:1;" } else { "" };
            return Ok(Some(format!("{pid}triple:{triple};endian:little;ptrsize:2;")));
        }
        if p == "qC" {
            return Ok(Some("QC1".into()));
        }
        if p == "qfThreadInfo" {
            return Ok(Some("m1".into()));
        }
        if p == "qsThreadInfo" {
            return Ok(Some("l".into()));
        }
        if p == "qAttached" {
            return Ok(Some("1".into()));
        }
        if let Some(rest) = p.strip_prefix("qMemoryRegionInfo:") {
            let Some(addr) = num(rest) else { return err() };
            let flash_end = self.cpu.flash_words() as u32 * 2;
            let stack_end = STACK_BASE + 2 * self.cpu.stack_levels() as u32;
            let region = |start: u32, end: u32, perm: &str| {
                Ok(Some(format!("start:{start:x};size:{:x};permissions:{perm};", end - start)))
            };
            return if addr < flash_end {
                region(0, flash_end, "rx")
            } else if addr < DATA_BASE {
                region(flash_end, DATA_BASE, "")
            } else if addr < DATA_BASE + 0x8000 {
                region(DATA_BASE, DATA_BASE + 0x8000, "rw")
            } else if addr < STACK_BASE {
                region(DATA_BASE + 0x8000, STACK_BASE, "r")
            } else if addr < stack_end {
                region(STACK_BASE, stack_end, "rw")
            } else {
                Ok(Some(format!("start:{stack_end:x};size:{:x};", u32::MAX - stack_end)))
            };
        }
        if p.starts_with('H') {
            return ok();
        }
        if p.starts_with('T') {
            return ok();
        }
        if p == "vCont?" {
            return Ok(Some("vCont;c;C;s;S".into()));
        }
        if let Some(rest) = p.strip_prefix("vCont;") {
            let action = rest.chars().next().unwrap_or('c');
            return Ok(Some(self.resume(action == 's' || action == 'S', stream)?));
        }
        if p.starts_with('c') || p.starts_with('C') {
            return Ok(Some(self.resume(false, stream)?));
        }
        if p.starts_with('s') || p.starts_with('S') {
            return Ok(Some(self.resume(true, stream)?));
        }
        if p == "g" {
            let mut all = Vec::new();
            for n in 0..REGS.len() {
                all.extend(self.read_reg(n).unwrap());
            }
            return Ok(Some(hex(&all)));
        }
        if let Some(rest) = p.strip_prefix('G') {
            let Some(bytes) = unhex(rest) else { return err() };
            let mut at = 0;
            for (n, r) in REGS.iter().enumerate() {
                let len = r.1 as usize / 8;
                if at + len > bytes.len() {
                    break;
                }
                self.write_reg(n, &bytes[at..at + len]);
                at += len;
            }
            return ok();
        }
        if let Some(rest) = p.strip_prefix('p') {
            return match num(rest).and_then(|n| self.read_reg(n as usize)) {
                Some(v) => Ok(Some(hex(&v))),
                None => err(),
            };
        }
        if let Some(rest) = p.strip_prefix('P') {
            let mut it = rest.split('=');
            let (Some(n), Some(v)) = (it.next().and_then(num), it.next().and_then(unhex)) else { return err() };
            return if self.write_reg(n as usize, &v) { ok() } else { err() };
        }
        if let Some(rest) = p.strip_prefix('m') {
            let mut it = rest.split(',');
            let (Some(addr), Some(len)) = (it.next().and_then(num), it.next().and_then(num)) else { return err() };
            let mut out = Vec::new();
            for i in 0..len.min(0x2000) {
                match self.read_byte(addr.wrapping_add(i)) {
                    Some(b) => out.push(b),
                    // Such a server reads zeros past the return stack.
                    None if self.plain && addr >= STACK_BASE => out.push(0),
                    None => break,
                }
            }
            return if out.is_empty() && len != 0 { err() } else { Ok(Some(hex(&out))) };
        }
        if let Some(rest) = p.strip_prefix('M') {
            let Some((head, data)) = rest.split_once(':') else { return err() };
            let mut it = head.split(',');
            let (Some(addr), Some(_len), Some(bytes)) = (it.next().and_then(num), it.next().and_then(num), unhex(data))
            else {
                return err();
            };
            for (i, b) in bytes.iter().enumerate() {
                if !self.write_byte(addr.wrapping_add(i as u32), *b) {
                    return err();
                }
            }
            return ok();
        }
        if p.starts_with('Z') || p.starts_with('z') {
            let set = p.starts_with('Z');
            let mut it = p[1..].split(',');
            let (Some(kind), Some(addr)) = (it.next().and_then(num), it.next().and_then(num)) else { return err() };
            match kind {
                // Software and hardware breakpoints are the same thing here,
                // and there is no limit on their number.
                0 | 1 => {
                    let word = (addr / 2) as u16;
                    if addr as usize >= self.cpu.flash_words() * 2 {
                        return err();
                    }
                    if set && self.silicon && !self.breakpoints.is_empty() && !self.breakpoints.contains(&word) {
                        return err();
                    }
                    if set {
                        self.breakpoints.insert(word);
                    } else {
                        self.breakpoints.remove(&word);
                    }
                    return ok();
                }
                // Write watchpoints on data memory.
                2 if !self.silicon => {
                    let len = it.next().and_then(num).unwrap_or(1).max(1);
                    if !(DATA_BASE..DATA_BASE + 0x8000).contains(&addr) {
                        return err();
                    }
                    let range = ((addr - DATA_BASE) as u16, len as u16);
                    self.watch_write.retain(|r| *r != range);
                    if set {
                        self.watch_write.push(range);
                    }
                    return ok();
                }
                _ => return unsupported(),
            }
        }
        if p == "k" || p.starts_with("vKill") {
            return Ok(None);
        }
        if p.starts_with('D') {
            self.send(stream, "OK")?;
            return Ok(None);
        }
        if p == "qSymbol::" {
            return ok();
        }
        unsupported()
    }
}
