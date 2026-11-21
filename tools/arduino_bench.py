#!/usr/bin/env python3
"""Arduino sketches on the PIC16F1937 and on an Arduino Nano (ATmega328P):
how big each is, and how long the pieces of a benchmark take.

    tools/arduino_bench.py [--avrrun PROGRAM] [--markdown FILE]

The sketches are tools/arduino-bench/sketches. Each is built for the Nano
by arduino-cli with the Arduino AVR Boards (avr-gcc, -Os, whole program),
and for the PIC16F1937 by pic16f1-build (-Os, 16 MHz). Bench is then run
on simulators: pic16f1-sim at 16 and 32 MHz, and, if --avrrun names the
program tools/arduino-bench/build-avrrun.sh builds (or AVRRUN does), a
simulated ATmega328P at 16 MHz. Each prints the time of its own pieces by
micros(), and what they computed, which must agree.

arduino-cli is taken from the PATH, from ARDUINO_CLI, or from the Arduino
IDE's application on macOS.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCHES = os.path.join(ROOT, "tools", "arduino-bench", "sketches")
ORDER = ["BareMinimum", "Blink", "BlinkWithoutDelay", "DigitalReadSerial", "AnalogReadSerial", "ReadAnalogVoltage",
         "Fade", "ButtonInterrupt", "WireRead", "EepromCount", "SpiTransfer", "Bench"]
NOTES = {
    "write-constant-pin": "1,000 times `digitalWrite` high and low, the pin a constant",
    "write-variable-pin": "the same, the pin in a variable",
    "read-constant-pin": "1,000 `digitalRead`",
    "millis": "1,000 calls of `millis`",
    "crc8": "CRC-8 of 1,024 bytes, bit by bit",
    "int16": "1,000 16-bit multiplications, 200 divisions",
    "int32": "1,000 32-bit multiplications, 200 divisions",
    "float": "500 terms of a series for pi",
    "sieve": "the primes below 256, ten times",
    "sort": "64 16-bit numbers, insertion sort",
}


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.exit(f"{' '.join(cmd[:4])} ...: failed\n{r.stdout}{r.stderr}")
    return r.stdout + r.stderr


def times(text):
    """name -> (result, microseconds) from what Bench printed."""
    found = {}
    for line in text.splitlines():
        m = re.match(r"([a-z0-9-]+) (\d+) (\d+)\s*$", line)
        if m:
            found[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return found


def main():
    args = sys.argv[1:]
    avrrun = os.environ.get("AVRRUN")
    markdown = None
    while args:
        a = args.pop(0)
        if a == "--avrrun" and args:
            avrrun = args.pop(0)
        elif a == "--markdown" and args:
            markdown = args.pop(0)
        else:
            sys.exit(__doc__)
    cli = (os.environ.get("ARDUINO_CLI") or shutil.which("arduino-cli") or
           "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")
    if not os.path.exists(cli):
        sys.exit("arduino_bench: no arduino-cli")
    build = os.path.join(ROOT, "toolchain", "bin", "pic16f1-build")
    sim = os.path.join(ROOT, "toolchain", "bin", "pic16f1-sim")
    size_tool = os.path.join(ROOT, "build", "llvm-build", "bin", "llvm-size")
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        # (The real path: arduino-cli names its core library relative to
        # the build directory, which a symbolic link on the way breaks.)
        tmp = os.path.realpath(tmp)
        work = os.path.join(tmp, "sketches")
        shutil.copytree(SKETCHES, work)
        rows = []
        for name in ORDER:
            sketch = os.path.join(work, name)
            text = run([cli, "compile", "--fqbn", "arduino:avr:nano", "--build-path", os.path.join(tmp, "avr-" + name), sketch])
            avr_flash = int(re.search(r"Sketch uses (\d+) bytes", text).group(1))
            avr_ram = int(re.search(r"Global variables use (\d+) bytes", text).group(1))
            text = run([build, "build", "--f-cpu", "16000000", sketch])
            words = int(re.search(r"(\d+) words of flash used", text).group(1))
            ram = 0
            for line in run([size_tool, "-A", os.path.join(sketch, "build", name + ".elf")]).splitlines():
                parts = line.split()
                if parts and parts[0] in (".data", ".bss", ".noinit"):
                    ram += int(parts[1])
            rows.append((name, avr_flash, avr_ram, words, ram))
        out += ["| Sketch | Nano: bytes of flash | of 30,720 | PIC16F1937: words of flash | of 8,192 | in bytes (14 bits a word) | Nano: RAM | PIC: RAM |",
                "|---|---|---|---|---|---|---|---|"]
        for name, af, ar, w, r in rows:
            out.append(f"| {name} | {af:,} | {100 * af / 30720:.1f}% | {w:,} | {100 * w / 8192:.1f}% | {w * 14 // 8:,} | {ar} | {r} |")
        out.append("")

        pic = {}
        for hz, math in (("16000000", "small"), ("32000000", "small"), ("16000000", "fast")):
            text = run([build, "build", "--f-cpu", hz, "--math", math, os.path.join(work, "Bench")])
            if math == "fast":
                fast_words = int(re.search(r"(\d+) words of flash used", text).group(1))
            r = subprocess.run([sim, "run", "--max-cycles", "1000000000", os.path.join(work, "Bench", "build", "Bench.elf")],
                               capture_output=True, text=True)
            if r.returncode != 90:
                sys.exit(f"Bench on the PIC simulator at {hz}: exit code {r.returncode}\n{r.stdout}")
            pic[hz if math == "small" else "fast"] = times(r.stdout)
        avr = None
        if avrrun:
            r = subprocess.run([avrrun, os.path.join(tmp, "avr-Bench", "Bench.ino.hex")], capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit(f"Bench on the AVR simulator: exit code {r.returncode}\n{r.stdout}{r.stderr}")
            avr = times(r.stdout)
            for name in NOTES:
                if avr[name][0] != pic["16000000"][name][0]:
                    sys.exit(f"{name}: the Nano computed {avr[name][0]}, the PIC {pic['16000000'][name][0]}")

        def ms(us):
            return f"{us / 1000:.2f}"
        head = "| Piece of Bench | "
        if avr:
            out += [head + "Nano, 16 MHz: ms | PIC16F1937, 16 MHz: ms | times the Nano | with `--math fast`: ms | times the Nano | PIC16F1937, 32 MHz: ms | times the Nano |",
                    "|---|---|---|---|---|---|---|---|"]
        else:
            out += [head + "PIC16F1937, 16 MHz: ms | PIC16F1937, 32 MHz: ms |", "|---|---|---|"]
        for name, note in NOTES.items():
            p16, p32, pf = pic["16000000"][name][1], pic["32000000"][name][1], pic["fast"][name][1]
            if avr:
                a = avr[name][1]
                out.append(f"| {note} | {ms(a)} | {ms(p16)} | {p16 / a:.1f} | {ms(pf)} | {pf / a:.1f} | {ms(p32)} | {p32 / a:.1f} |")
            else:
                out.append(f"| {note} | {ms(p16)} | {ms(p32)} |")
    out += ["", f"Bench with `--math fast` is {fast_words:,} words."]
    text = "\n".join(out) + "\n"
    print(text)
    if markdown:
        with open(markdown, "w") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
