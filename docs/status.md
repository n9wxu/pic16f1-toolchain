# Status against the plan's gates

The test suites run on the simulator. Hardware so far: programs built by
this toolchain have been programmed into a PIC16LF1937 and debugged there,
through a PICkit 2 running the probe firmware of a companion project. The
plan's own lab items (PICkit 3 golden path, Saleae captures, timing
against silicon) are still deferred. "Awaiting approval" means the work is done and needs the architect's
sign-off, which the plan requires before the gate counts as passed.

## G0: Foundations

| Exit criterion | State |
|---|---|
| v1 specifications approved | Drafted in [spec/](spec/README.md); **awaiting approval** |
| Device database covers first families | Five devices in four families: PIC16F1937, PIC16F1829, PIC16F1619, PIC16F18877, PIC16F15276. Which families are "first" is decision 3 in the plan and is yours to confirm |
| Simulator core passes instruction-semantics tests | 20 tests: results, the flags each instruction must and must not change, and cycle counts |
| Lab runs with the PICkit 3 golden path and the Saleae | Deferred (hardware) |
| Probe reads device IDs | The companion PICkit 2 probe does; nothing in this repository |

Decided: the license is Apache 2.0 with the LLVM exception. Still open from
section 9 of the plan: the ELF machine number (0x5046 used provisionally), first families, benchmark method, agent platform, human
roles, and the pinned MPLAB X version.

## G1: MC layer and programming

| Exit criterion | State |
|---|---|
| `llvm-mc` round-trips every instruction | Done: 61 instruction forms assemble, disassemble and reassemble to identical bytes; 57 match Microchip `pic-as` word for word, as do the three 64-bank `MOVLB` encodings |
| Probe programs and verifies | The companion probe's OpenOCD programs program memory, configuration words, user ID and EEPROM from this toolchain's ELF files; read back and compared on a PIC16LF1937 |
| A HEX file assembled by the toolchain runs on silicon | Compiled programs run on a PIC16LF1937 under the debugger, with the values the simulator gives |

## G2: C MVP

| Exit criterion | State |
|---|---|
| At least 95% of the GCC C torture tests | 98.6% of the tests that build at `-O0`, 98.7% at the other levels; none of the rest is wrong code |
| picolibc passes | picolibc compiles in full. Of the 141 of its test programs run, 104 pass on the simulated PIC16F18877 and none gives a wrong result. Its math, POSIX and locale tests were not run |
| Blink on three families | One program builds unchanged and passes on five devices in four families, on the simulator; silicon deferred |
| Simulator matches silicon on core validation | Deferred (hardware) |

## What G2 unlocks

Optimization I, C++, Rust, and LLDB.

**Optimization I** has met its aim: every one of the fifteen benchmark
programs is no larger and no slower than XC8's at each of XC8's five
levels (`-O0` to `-O3` and `-Os`). At `-Os` the set is 0.73 times XC8's
size and runs in 0.48 times the cycles, which meets the G3 size criterion
(within 15%) with room to spare. In place: a peephole pass over the final
code, register usage across calls, byte arguments and results in W chosen
function by function, startup code linked by need, a one-page code model,
one bank selection per variable, FSR0 as a register the allocator keeps
pointers in, shared instruction sequences (outlining), tail calls, tables
of jumps for switches, variables changed in place, assembly multiply and
divide routines, floating point written for the target, short multiplies
by constants in line, optimization levels retuned for a small machine,
whole-program builds with `-flto`, static frames (fixed addresses for the
variables of functions that cannot be re-entered, sharing memory where two
are never active together), and the linker's count of hardware stack
levels. Not started: the verifier, skip predication.

**LLDB** works on the simulator and on the part: breakpoints, stepping,
the call stack, variables in every frame, registers named as in the data
sheet, and VS Code projects for both.

**C++** works: the language without exceptions and run-time type
information, the header part of the standard library, and a device library,
tested on the simulator and on the part ([languages.md](languages.md)).
**Rust** is not started.

See [toolchain.md](toolchain.md) for test results in detail and known limits.
