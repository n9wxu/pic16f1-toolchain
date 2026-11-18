//! Instruction decoding and disassembly for the 49-instruction enhanced
//! mid-range set.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Op {
    Nop,
    Reset,
    Return,
    Retfie,
    Callw,
    Brw,
    /// MOVIW with pre/post increment/decrement. mode: 0 ++FSR, 1 --FSR, 2 FSR++, 3 FSR--
    MoviwMode { n: u8, mode: u8 },
    MovwiMode { n: u8, mode: u8 },
    Movlb(u8),
    Option,
    Sleep,
    Clrwdt,
    Tris(u8),
    Movwf(u8),
    Clrw,
    Clrf(u8),
    Subwf(u8, bool),
    Decf(u8, bool),
    Iorwf(u8, bool),
    Andwf(u8, bool),
    Xorwf(u8, bool),
    Addwf(u8, bool),
    Movf(u8, bool),
    Comf(u8, bool),
    Incf(u8, bool),
    Decfsz(u8, bool),
    Rrf(u8, bool),
    Rlf(u8, bool),
    Swapf(u8, bool),
    Incfsz(u8, bool),
    Bcf(u8, u8),
    Bsf(u8, u8),
    Btfsc(u8, u8),
    Btfss(u8, u8),
    Call(u16),
    Goto(u16),
    Movlw(u8),
    Addfsr { n: u8, k: i8 },
    Movlp(u8),
    Bra(i16),
    Retlw(u8),
    Lslf(u8, bool),
    Lsrf(u8, bool),
    Asrf(u8, bool),
    Iorlw(u8),
    Andlw(u8),
    Xorlw(u8),
    Subwfb(u8, bool),
    Sublw(u8),
    Addwfc(u8, bool),
    Addlw(u8),
    MoviwOff { n: u8, k: i8 },
    MovwiOff { n: u8, k: i8 },
    Illegal(u16),
}

fn sext(v: u16, bits: u8) -> i16 {
    let shift = 16 - bits;
    ((v << shift) as i16) >> shift
}

pub fn decode(w: u16) -> Op {
    use Op::*;
    let w = w & 0x3FFF;
    let f = (w & 0x7F) as u8;
    let d = w & 0x80 != 0;
    let k8 = (w & 0xFF) as u8;
    match w >> 12 {
        0 => match (w >> 8) & 0xF {
            0 => match w {
                0x0000 => Nop,
                0x0001 => Reset,
                0x0008 => Return,
                0x0009 => Retfie,
                0x000A => Callw,
                0x000B => Brw,
                0x0010..=0x0017 => MoviwMode { n: ((w >> 2) & 1) as u8, mode: (w & 3) as u8 },
                0x0018..=0x001F => MovwiMode { n: ((w >> 2) & 1) as u8, mode: (w & 3) as u8 },
                0x0020..=0x003F => Movlb((w & 0x1F) as u8),
                0x0062 => Option,
                0x0063 => Sleep,
                0x0064 => Clrwdt,
                0x0065..=0x0067 => Tris((w & 7) as u8),
                0x0080..=0x00FF => Movwf(f),
                _ => Illegal(w),
            },
            1 => {
                if d {
                    Clrf(f)
                } else if w & 0xFC == 0 {
                    Clrw
                } else if w & 0xC0 == 0x40 {
                    // MOVLB on devices with 64 banks: 00 0001 01kk kkkk.
                    Movlb((w & 0x3F) as u8)
                } else {
                    Illegal(w)
                }
            }
            2 => Subwf(f, d),
            3 => Decf(f, d),
            4 => Iorwf(f, d),
            5 => Andwf(f, d),
            6 => Xorwf(f, d),
            7 => Addwf(f, d),
            8 => Movf(f, d),
            9 => Comf(f, d),
            10 => Incf(f, d),
            11 => Decfsz(f, d),
            12 => Rrf(f, d),
            13 => Rlf(f, d),
            14 => Swapf(f, d),
            _ => Incfsz(f, d),
        },
        1 => {
            let b = ((w >> 7) & 7) as u8;
            match (w >> 10) & 3 {
                0 => Bcf(f, b),
                1 => Bsf(f, b),
                2 => Btfsc(f, b),
                _ => Btfss(f, b),
            }
        }
        2 => {
            if w & 0x0800 == 0 {
                Call(w & 0x7FF)
            } else {
                Goto(w & 0x7FF)
            }
        }
        _ => match (w >> 8) & 0xF {
            0 => Movlw(k8),
            1 => {
                if w & 0x80 == 0 {
                    Addfsr { n: ((w >> 6) & 1) as u8, k: sext(w & 0x3F, 6) as i8 }
                } else {
                    Movlp((w & 0x7F) as u8)
                }
            }
            2 | 3 => Bra(sext(w & 0x1FF, 9)),
            4 => Retlw(k8),
            5 => Lslf(f, d),
            6 => Lsrf(f, d),
            7 => Asrf(f, d),
            8 => Iorlw(k8),
            9 => Andlw(k8),
            10 => Xorlw(k8),
            11 => Subwfb(f, d),
            12 => Sublw(k8),
            13 => Addwfc(f, d),
            14 => Addlw(k8),
            _ => {
                let n = ((w >> 6) & 1) as u8;
                let k = sext(w & 0x3F, 6) as i8;
                if w & 0x80 == 0 {
                    MoviwOff { n, k }
                } else {
                    MovwiOff { n, k }
                }
            }
        },
    }
}

/// Text form of one instruction at word address `pc`.
pub fn disasm(pc: u16, w: u16) -> String {
    use Op::*;
    let fd = |name: &str, f: u8, d: bool| format!("{name} 0x{f:02X}, {}", if d { "F" } else { "W" });
    let ind = |n: u8, mode: u8| match mode {
        0 => format!("++FSR{n}"),
        1 => format!("--FSR{n}"),
        2 => format!("FSR{n}++"),
        _ => format!("FSR{n}--"),
    };
    match decode(w) {
        Nop => "NOP".into(),
        Reset => "RESET".into(),
        Return => "RETURN".into(),
        Retfie => "RETFIE".into(),
        Callw => "CALLW".into(),
        Brw => "BRW".into(),
        MoviwMode { n, mode } => format!("MOVIW {}", ind(n, mode)),
        MovwiMode { n, mode } => format!("MOVWI {}", ind(n, mode)),
        Movlb(k) => format!("MOVLB {k}"),
        Option => "OPTION".into(),
        Sleep => "SLEEP".into(),
        Clrwdt => "CLRWDT".into(),
        Tris(f) => format!("TRIS {f}"),
        Movwf(f) => format!("MOVWF 0x{f:02X}"),
        Clrw => "CLRW".into(),
        Clrf(f) => format!("CLRF 0x{f:02X}"),
        Subwf(f, d) => fd("SUBWF", f, d),
        Decf(f, d) => fd("DECF", f, d),
        Iorwf(f, d) => fd("IORWF", f, d),
        Andwf(f, d) => fd("ANDWF", f, d),
        Xorwf(f, d) => fd("XORWF", f, d),
        Addwf(f, d) => fd("ADDWF", f, d),
        Movf(f, d) => fd("MOVF", f, d),
        Comf(f, d) => fd("COMF", f, d),
        Incf(f, d) => fd("INCF", f, d),
        Decfsz(f, d) => fd("DECFSZ", f, d),
        Rrf(f, d) => fd("RRF", f, d),
        Rlf(f, d) => fd("RLF", f, d),
        Swapf(f, d) => fd("SWAPF", f, d),
        Incfsz(f, d) => fd("INCFSZ", f, d),
        Bcf(f, b) => format!("BCF 0x{f:02X}, {b}"),
        Bsf(f, b) => format!("BSF 0x{f:02X}, {b}"),
        Btfsc(f, b) => format!("BTFSC 0x{f:02X}, {b}"),
        Btfss(f, b) => format!("BTFSS 0x{f:02X}, {b}"),
        Call(k) => format!("CALL 0x{k:03X}"),
        Goto(k) => format!("GOTO 0x{k:03X}"),
        Movlw(k) => format!("MOVLW 0x{k:02X}"),
        Addfsr { n, k } => format!("ADDFSR FSR{n}, {k}"),
        Movlp(k) => format!("MOVLP 0x{k:02X}"),
        Bra(k) => format!("BRA 0x{:04X}", (pc as i32 + 1 + k as i32) & 0x7FFF),
        Retlw(k) => format!("RETLW 0x{k:02X}"),
        Lslf(f, d) => fd("LSLF", f, d),
        Lsrf(f, d) => fd("LSRF", f, d),
        Asrf(f, d) => fd("ASRF", f, d),
        Iorlw(k) => format!("IORLW 0x{k:02X}"),
        Andlw(k) => format!("ANDLW 0x{k:02X}"),
        Xorlw(k) => format!("XORLW 0x{k:02X}"),
        Subwfb(f, d) => fd("SUBWFB", f, d),
        Sublw(k) => format!("SUBLW 0x{k:02X}"),
        Addwfc(f, d) => fd("ADDWFC", f, d),
        Addlw(k) => format!("ADDLW 0x{k:02X}"),
        MoviwOff { n, k } => format!("MOVIW {k}[FSR{n}]"),
        MovwiOff { n, k } => format!("MOVWI {k}[FSR{n}]"),
        Illegal(w) => format!(".word 0x{w:04X}"),
    }
}
