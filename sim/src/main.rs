use pic16f1_sim::{disasm, Cpu, Device, Halt, Image, UartMode};
use std::io::Write;
use std::process::ExitCode;

const USAGE: &str = "\
pic16f1-sim: PIC16F1 simulator

Usage:
  pic16f1-sim run [options] <image.hex|image.elf>
  pic16f1-sim gdb [--device NAME] [--port N] <image.elf>
  pic16f1-sim disasm [--device NAME] <image>
  pic16f1-sim size [--device NAME] <image>
  pic16f1-sim elf2hex <in.elf> <out.hex>

Run options:
  --device NAME       device to simulate (default pic16f1937; also pic16f1829,
                      pic16f1619, pic16f18877, pic16f15276, and the two
                      imaginary devices pic16f1032 and pic16f1064, the
                      largest 32- and 64-bank parts there could be)
  --max-cycles N      stop after N instruction cycles (default 100000000)
  --uart fast|timed   UART transmit timing (default fast)
  --json              print a JSON result record on stdout instead of UART text
  --trace             print every executed instruction on stderr
  --profile FILE      write per-address execution counts to FILE

gdb waits for a debugger (LLDB or gdb) to connect to the port, 3333 unless
--port says otherwise, and runs the program under its control. UART output
goes to the debugger's console.

A program ends by executing SLEEP. Its exit code is the value it left in LATD
(LATC on a device without PORTD).
The process exit status is that code, or 124 on timeout, 125 on a fault.";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match real_main(&args) {
        Ok(code) => ExitCode::from(code),
        Err(msg) => {
            eprintln!("pic16f1-sim: {msg}");
            ExitCode::from(2)
        }
    }
}

struct Opts {
    device: String,
    max_cycles: u64,
    uart: UartMode,
    json: bool,
    trace: bool,
    profile: Option<String>,
    port: u16,
    files: Vec<String>,
}

fn parse(args: &[String]) -> Result<Opts, String> {
    let mut o = Opts {
        device: "pic16f1937".into(),
        max_cycles: 100_000_000,
        uart: UartMode::Fast,
        json: false,
        trace: false,
        profile: None,
        port: 3333,
        files: Vec::new(),
    };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        let mut val = |name: &str| it.next().cloned().ok_or(format!("{name} needs a value"));
        match a.as_str() {
            "--device" => o.device = val("--device")?,
            "--max-cycles" => {
                o.max_cycles = val("--max-cycles")?.replace('_', "").parse().map_err(|_| "bad --max-cycles value")?
            }
            "--uart" => {
                o.uart = match val("--uart")?.as_str() {
                    "fast" => UartMode::Fast,
                    "timed" => UartMode::Timed,
                    v => return Err(format!("unknown --uart mode '{v}'")),
                }
            }
            "--port" => o.port = val("--port")?.parse().map_err(|_| "bad --port value")?,
            "--json" => o.json = true,
            "--trace" => o.trace = true,
            "--profile" => o.profile = Some(val("--profile")?),
            s if s.starts_with("--") => return Err(format!("unknown option '{s}'")),
            _ => o.files.push(a.clone()),
        }
    }
    Ok(o)
}

fn real_main(args: &[String]) -> Result<u8, String> {
    let Some(cmd) = args.first() else {
        println!("{USAGE}");
        return Ok(2);
    };
    let o = parse(&args[1..])?;
    let need = |n: usize| if o.files.len() == n { Ok(()) } else { Err(format!("'{cmd}' takes {n} file argument(s)\n\n{USAGE}")) };
    match cmd.as_str() {
        "run" => {
            need(1)?;
            run(&o)
        }
        "gdb" => {
            need(1)?;
            let dev = Device::builtin(&o.device)?;
            let img = Image::load(&o.files[0])?;
            let mut cpu = Cpu::new(dev);
            cpu.load(&img)?;
            cpu.uart_mode = o.uart;
            pic16f1_sim::gdb::Server::new(cpu).serve(o.port)
        }
        "disasm" => {
            need(1)?;
            let dev = Device::builtin(&o.device)?;
            let img = Image::load(&o.files[0])?;
            for (&addr, &w) in &img.words {
                if addr < dev.flash.words {
                    println!("{addr:04X}:  {w:04X}  {}", disasm::disasm(addr as u16, w));
                } else {
                    println!("{addr:04X}:  {w:04X}");
                }
            }
            Ok(0)
        }
        "size" => {
            need(1)?;
            let dev = Device::builtin(&o.device)?;
            let img = Image::load(&o.files[0])?;
            println!("{}", img.program_words(dev.flash.words));
            Ok(0)
        }
        "elf2hex" => {
            need(2)?;
            let img = Image::load(&o.files[0])?;
            std::fs::write(&o.files[1], img.to_hex()).map_err(|e| format!("{}: {e}", o.files[1]))?;
            Ok(0)
        }
        "-h" | "--help" | "help" => {
            println!("{USAGE}");
            Ok(0)
        }
        _ => Err(format!("unknown command '{cmd}'\n\n{USAGE}")),
    }
}

fn run(o: &Opts) -> Result<u8, String> {
    let dev = Device::builtin(&o.device)?;
    let img = Image::load(&o.files[0])?;
    let words = img.program_words(dev.flash.words);
    let mut cpu = Cpu::new(dev);
    cpu.load(&img)?;
    cpu.uart_mode = o.uart;
    cpu.trace = o.trace;
    if o.profile.is_some() {
        cpu.profile = Some(vec![0; cpu.dev.flash.words as usize]);
    }
    let halt = cpu.run(o.max_cycles);
    let exit_code = cpu.exit_code().unwrap_or(0);
    let detail = match halt {
        Halt::Unprogrammed(pc) => format!("executed unprogrammed memory at 0x{pc:04X}"),
        Halt::Illegal { pc, word } => format!("illegal opcode 0x{word:04X} at 0x{pc:04X}"),
        Halt::StackOverflow => format!("hardware stack overflow at 0x{:04X}", cpu.pc),
        Halt::StackUnderflow => format!("hardware stack underflow at 0x{:04X}", cpu.pc),
        Halt::MaxCycles => format!("no SLEEP within {} cycles", o.max_cycles),
        Halt::Reset => format!("RESET instruction at 0x{:04X}", cpu.pc.wrapping_sub(1)),
        Halt::Sleep => String::new(),
    };
    if let Some(path) = &o.profile {
        let mut out = String::from("{\"counts\":{");
        let prof = cpu.profile.as_ref().unwrap();
        let mut first = true;
        for (pc, &n) in prof.iter().enumerate() {
            if n > 0 {
                if !first {
                    out.push(',');
                }
                first = false;
                out.push_str(&format!("\"{pc}\":{n}"));
            }
        }
        out.push_str("}}\n");
        std::fs::write(path, out).map_err(|e| format!("{path}: {e}"))?;
    }
    if o.json {
        let rec = serde_json::json!({
            "device": cpu.dev.name,
            "halt": halt.name(),
            "detail": detail,
            "exit_code": exit_code,
            "cycles": cpu.cycles,
            "instructions": cpu.instructions,
            "program_words": words,
            "max_stack_depth": cpu.max_stack_depth,
            "interrupts": cpu.interrupts_taken,
            "uart_overruns": cpu.uart_overruns,
            "uart": String::from_utf8_lossy(&cpu.uart_out),
        });
        println!("{rec}");
    } else {
        std::io::stdout().write_all(&cpu.uart_out).ok();
        std::io::stdout().flush().ok();
        if halt != Halt::Sleep {
            eprintln!("pic16f1-sim: {detail}");
        }
    }
    Ok(match halt {
        Halt::Sleep => exit_code,
        Halt::MaxCycles => 124,
        _ => 125,
    })
}
