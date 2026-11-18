# Status against the plan's gates

Hardware items are deferred by decision: the work so far is on the simulator
only. "Awaiting approval" means the work is done and needs the architect's
sign-off, which the plan requires before the gate counts as passed.

## G0: Foundations

| Exit criterion | State |
|---|---|
| v1 specifications approved | Drafted in [spec/](spec/README.md); **awaiting approval** |
| Device database covers first families | Five devices in four families: PIC16F1937, PIC16F1829, PIC16F1619, PIC16F18877, PIC16F15276. Which families are "first" is decision 3 in the plan and is yours to confirm |
| Simulator core passes instruction-semantics tests | 20 tests: results, the flags each instruction must and must not change, and cycle counts |
| Lab runs with the PICkit 3 golden path and the Saleae | Deferred (hardware) |
| Probe reads device IDs | Deferred (hardware) |

Open decisions from section 9 of the plan: licensing (Apache 2.0 with LLVM
exception used provisionally), the ELF machine number (0x5046 used
provisionally), first families, benchmark method, agent platform, human
roles, and the pinned MPLAB X version.

## G1: MC layer and programming

| Exit criterion | State |
|---|---|
| `llvm-mc` round-trips every instruction | Done: 61 instruction forms assemble, disassemble and reassemble to identical bytes; 57 match Microchip `pic-as` word for word, as do the three 64-bank `MOVLB` encodings |
| Probe programs and verifies | Deferred (hardware) |
| A HEX file assembled by the toolchain runs on silicon | Runs on the simulator; silicon deferred |

## G2: C MVP

| Exit criterion | State |
|---|---|
| At least 95% of the GCC C torture tests | 97.4% of the tests that build, at `-O0` and at `-Os` |
| picolibc passes | picolibc compiles in full. Of the 141 of its test programs run, 103 pass on the simulated PIC16F18877 and none gives a wrong result. Its math, POSIX and locale tests were not run |
| Blink on three families | One program builds unchanged and passes on five devices in four families, on the simulator; silicon deferred |
| Simulator matches silicon on core validation | Deferred (hardware) |

## What G2 unlocks

Optimization I (static frames, bank and page tracking, the verifier), C++,
Rust, and LLDB. None of these is started. Code is currently about 1.4 times
the size of XC8 `-Os`; the G3 target is within 15%.

See [toolchain.md](toolchain.md) for test results in detail and known limits.
