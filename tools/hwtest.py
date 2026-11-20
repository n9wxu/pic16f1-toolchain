#!/usr/bin/env python3
"""Run a test program on a real part and report what it found.

    tools/hwtest.py [--openocd DIR] [--vdd MILLIVOLTS] program.elf [variable ...]

The program must have been built with -g -mdebugger. It reports as the
simulator tests do, by writing a code to LATD (0x5A for success, otherwise
the number of the check that failed), and then calls a function named
hw_done, which never returns. This script has the PICkit 2 probe's OpenOCD
program the part, runs it under LLDB to a breakpoint on hw_done, reads LATD
and prints the named variables, and exits 0 if LATD was 0x5A. A variable
the program has no debug information for is named with its size in bytes,
SYMBOL@2, and printed as a number.

--openocd (or PIC16F1_OPENOCD) is the probe firmware's OpenOCD build
directory, with src/openocd and tcl/ in it. This drives the probe and the
board: nothing else may be using them.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.realpath(__file__))
LATD = 0x80010F  # the debugger's address of LATD on the PIC16F193x


def main():
    args = sys.argv[1:]
    openocd = os.environ.get("PIC16F1_OPENOCD")
    vdd = "3300"
    while args and args[0].startswith("--"):
        if args[0] == "--openocd":
            openocd = args[1]
        elif args[0] == "--vdd":
            vdd = args[1]
        else:
            sys.exit(__doc__)
        args = args[2:]
    if not args or not openocd:
        sys.exit(__doc__)
    elf = os.path.abspath(args[0])
    symbols = args[1:]
    lldb = os.path.join(HERE, "..", "toolchain", "bin", "pic16f1-lldb")
    with tempfile.TemporaryDirectory() as tmp:
        log = open(os.path.join(tmp, "openocd.log"), "w+")
        server = subprocess.Popen(
            ["src/openocd", "-s", "tcl", "-f", "interface/pk2icsp.cfg",
             "-c", f"pk2icsp vdd {vdd}", "-c", "pk2icsp hvp on",
             "-f", "target/pic16f193x.cfg",
             "-c", "pic16f193x.cpu configure -event gdb-detach { shutdown }",
             "-c", f'init; pic16f1 load_debug {elf}; echo "pic16f1: ready for the debugger"'],
            cwd=openocd, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.time() + 90
            while True:
                log.seek(0)
                text = log.read()
                if "ready for the debugger" in text:
                    break
                if server.poll() is not None or time.time() > deadline:
                    sys.exit("hwtest: OpenOCD did not get the part ready:\n" + text)
                time.sleep(0.3)
            cmds = ["gdb-remote 3333", "breakpoint set --name hw_done", "continue",
                    f"memory read --size 1 --count 1 --format x {LATD:#x}"]
            raw = {}
            for sym in symbols:
                if "@" not in sym:
                    cmds.append(f"target variable {sym}")
                    continue
                name, size = sym.split("@")
                nm = subprocess.run([os.path.join(HERE, "..", "build", "llvm-build", "bin", "llvm-nm"), elf],
                                    capture_output=True, text=True).stdout
                found = [l.split()[0] for l in nm.splitlines() if l.split()[-1:] == [name]]
                if not found:
                    sys.exit(f"hwtest: no symbol {name}")
                raw[int(found[0], 16)] = (name, int(size))
                cmds.append(f"memory read --size 1 --count {size} --format x {int(found[0], 16):#x}")
            cmds += ["process detach"]
            cmd = [lldb, "--batch"]
            for c in cmds:
                cmd += ["-o", c]
            out = subprocess.run(cmd + [elf], capture_output=True, text=True, timeout=180).stdout
        finally:
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
    if "stop reason = breakpoint" not in out:
        print(out)
        sys.exit("hwtest: the program did not reach hw_done")
    m = re.search(rf"^0x{LATD:08x}: 0x([0-9a-fA-F]{{2}})", out, re.M)
    if not m:
        print(out)
        sys.exit("hwtest: could not read LATD")
    code = int(m.group(1), 16)
    for line in out.splitlines():
        if re.match(r"^\(.*\) \S+ = ", line):
            print("  " + line)
        m2 = re.match(r"^0x([0-9a-f]{8}): ((?:0x[0-9a-f]{2} ?)+)", line)
        if m2 and int(m2.group(1), 16) in raw:
            name, size = raw[int(m2.group(1), 16)]
            value = int.from_bytes(bytes(int(b, 16) for b in m2.group(2).split()), "little")
            print(f"  {name} = {value}")
    print(f"LATD = {code:#04x}: " + ("pass" if code == 0x5A else f"check {code} failed"))
    return 0 if code == 0x5A else 1


if __name__ == "__main__":
    sys.exit(main())
