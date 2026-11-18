//! Program images: Intel HEX and ELF loading, HEX writing.
//!
//! An image is a sparse map from word address to 14-bit word, using the
//! Microchip HEX convention: program memory at word 0, configuration words
//! at 0x8007.., EEPROM bytes at 0xF000.. (one byte per word).
//!
//! ELF conventions of this toolchain:
//!   * Load addresses below 0x800000 are program-memory byte addresses
//!     (two bytes per word, little-endian), the same as HEX addresses.
//!   * Load addresses 0x808000..0x80FFFF are flash seen through the FSR data
//!     view: one data byte per program word. Each byte is stored as a
//!     `RETLW byte` word (0x34xx) at word address (addr - 0x808000).
//!   * Everything at 0x800000..0x807FFF is RAM and is not loaded.

use std::collections::BTreeMap;

pub const DATA_SPACE: u32 = 0x80_0000;
pub const FLASH_DATA_VIEW: u32 = 0x80_8000;
const RETLW: u16 = 0x3400;

#[derive(Debug, Clone, Default)]
pub struct Image {
    pub words: BTreeMap<u32, u16>,
    /// Function and object symbols (ELF only): (name, value, is_code).
    pub symbols: Vec<(String, u32, bool)>,
}

impl Image {
    pub fn load(path: &str) -> Result<Image, String> {
        let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
        if data.starts_with(b"\x7fELF") {
            Image::from_elf(&data)
        } else {
            let text = String::from_utf8(data).map_err(|_| format!("{path}: not HEX or ELF"))?;
            Image::from_hex(&text)
        }
    }

    pub fn from_hex(text: &str) -> Result<Image, String> {
        let mut bytes: BTreeMap<u32, u8> = BTreeMap::new();
        let mut upper: u32 = 0;
        for (ln, line) in text.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let err = |m: &str| format!("HEX line {}: {m}", ln + 1);
            let body = line.strip_prefix(':').ok_or_else(|| err("missing ':'"))?;
            if body.len() < 10 || body.len() % 2 != 0 {
                return Err(err("bad length"));
            }
            let mut rec = Vec::with_capacity(body.len() / 2);
            for i in (0..body.len()).step_by(2) {
                rec.push(u8::from_str_radix(&body[i..i + 2], 16).map_err(|_| err("bad hex digit"))?);
            }
            if rec.iter().fold(0u8, |a, b| a.wrapping_add(*b)) != 0 {
                return Err(err("checksum mismatch"));
            }
            let n = rec[0] as usize;
            if rec.len() != n + 5 {
                return Err(err("record length mismatch"));
            }
            let addr = ((rec[1] as u32) << 8) | rec[2] as u32;
            match rec[3] {
                0x00 => {
                    for i in 0..n {
                        bytes.insert(upper + addr + i as u32, rec[4 + i]);
                    }
                }
                0x01 => break,
                0x02 => upper = (((rec[4] as u32) << 8) | rec[5] as u32) << 4,
                0x04 => upper = (((rec[4] as u32) << 8) | rec[5] as u32) << 16,
                0x03 | 0x05 => {}
                t => return Err(err(&format!("unsupported record type {t:02X}"))),
            }
        }
        let mut img = Image::default();
        for (&a, &b) in &bytes {
            let w = img.words.entry(a / 2).or_insert(0);
            if a % 2 == 0 {
                *w = (*w & 0xFF00) | b as u16;
            } else {
                *w = (*w & 0x00FF) | ((b as u16) << 8);
            }
        }
        Ok(img)
    }

    pub fn from_elf(d: &[u8]) -> Result<Image, String> {
        let rd16 = |o: usize| -> Result<u32, String> {
            d.get(o..o + 2).map(|b| u16::from_le_bytes([b[0], b[1]]) as u32).ok_or_else(|| "truncated ELF".to_string())
        };
        let rd32 = |o: usize| -> Result<u32, String> {
            d.get(o..o + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]])).ok_or_else(|| "truncated ELF".to_string())
        };
        if d.len() < 52 || d[4] != 1 || d[5] != 1 {
            return Err("expected a 32-bit little-endian ELF".into());
        }
        let phoff = rd32(28)? as usize;
        let shoff = rd32(32)? as usize;
        let phentsize = rd16(42)? as usize;
        let phnum = rd16(44)? as usize;
        let shentsize = rd16(46)? as usize;
        let shnum = rd16(48)? as usize;
        let mut img = Image::default();
        for i in 0..phnum {
            let p = phoff + i * phentsize;
            if rd32(p)? != 1 {
                continue; // not PT_LOAD
            }
            let off = rd32(p + 4)? as usize;
            let paddr = rd32(p + 12)?;
            let filesz = rd32(p + 16)? as usize;
            let seg = d.get(off..off + filesz).ok_or("segment outside file")?;
            for (k, &b) in seg.iter().enumerate() {
                let a = paddr + k as u32;
                if a >= FLASH_DATA_VIEW && a < FLASH_DATA_VIEW + 0x8000 {
                    img.words.insert(a - FLASH_DATA_VIEW, RETLW | b as u16);
                } else if a < DATA_SPACE {
                    let w = img.words.entry(a / 2).or_insert(0);
                    if a % 2 == 0 {
                        *w = (*w & 0xFF00) | b as u16;
                    } else {
                        *w = (*w & 0x00FF) | ((b as u16) << 8);
                    }
                }
            }
        }
        // Symbols, for traces and profiles.
        for i in 0..shnum {
            let s = shoff + i * shentsize;
            if rd32(s + 4)? != 2 {
                continue; // not SHT_SYMTAB
            }
            let off = rd32(s + 16)? as usize;
            let size = rd32(s + 20)? as usize;
            let link = rd32(s + 24)? as usize;
            let stroff = rd32(shoff + link * shentsize + 16)? as usize;
            for e in (off..off + size).step_by(16) {
                let name_off = rd32(e)? as usize;
                let value = rd32(e + 4)?;
                let info = *d.get(e + 12).ok_or("truncated symtab")?;
                let typ = info & 0xF;
                if name_off == 0 || typ > 2 {
                    continue;
                }
                let start = stroff + name_off;
                let end = d[start..].iter().position(|&c| c == 0).map(|p| start + p).unwrap_or(start);
                let name = String::from_utf8_lossy(&d[start..end]).to_string();
                img.symbols.push((name, value, typ == 2 || value < DATA_SPACE));
            }
        }
        Ok(img)
    }

    /// Intel HEX text, 16 bytes per record, with type-04 records as needed.
    pub fn to_hex(&self) -> String {
        let mut out = String::new();
        let mut upper: u32 = 0;
        let mut rec: Vec<u8> = Vec::new();
        let mut rec_addr: u32 = 0;
        let flush = |out: &mut String, upper: &mut u32, rec: &mut Vec<u8>, rec_addr: u32| {
            if rec.is_empty() {
                return;
            }
            if rec_addr >> 16 != *upper {
                *upper = rec_addr >> 16;
                let b = [2u8, 0, 0, 4, (*upper >> 8) as u8, *upper as u8];
                emit(out, &b);
            }
            let mut b = vec![rec.len() as u8, (rec_addr >> 8) as u8, rec_addr as u8, 0];
            b.extend_from_slice(rec);
            emit(out, &b);
            rec.clear();
        };
        for (&wa, &w) in &self.words {
            let a = wa * 2;
            let contiguous = !rec.is_empty() && rec_addr + rec.len() as u32 == a;
            if !contiguous || rec.len() >= 16 || (a & 0xFFFF) == 0 {
                flush(&mut out, &mut upper, &mut rec, rec_addr);
                rec_addr = a;
            }
            rec.push(w as u8);
            rec.push((w >> 8) as u8);
        }
        flush(&mut out, &mut upper, &mut rec, rec_addr);
        out.push_str(":00000001FF\n");
        out
    }

    /// Number of program-memory words occupied (below `flash_words`).
    pub fn program_words(&self, flash_words: u32) -> u32 {
        self.words.range(..flash_words).count() as u32
    }
}

fn emit(out: &mut String, bytes: &[u8]) {
    let sum = bytes.iter().fold(0u8, |a, b| a.wrapping_add(*b));
    out.push(':');
    for b in bytes {
        out.push_str(&format!("{b:02X}"));
    }
    out.push_str(&format!("{:02X}\n", sum.wrapping_neg()));
}
