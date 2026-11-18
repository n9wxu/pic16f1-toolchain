# LLVM toolchain: design of the first working C compiler

This describes what is implemented, as opposed to the full plan in
[plan.md](plan.md). It is the plan's "C MVP" (workstream 2, stage B): correct
first, with the simplest design that works.

## Components

| Component | Where | Notes |
|---|---|---|
| LLVM target `pic16f1` | `llvm/new-files/llvm/lib/Target/PIC16F1` | Assembler, object writer, code generator |
| Clang target | `llvm/new-files/clang`, `llvm/existing-files.patch` | Type sizes, ABI, `interrupt` attribute |
| LLD port | `llvm/new-files/lld/ELF/Arch/PIC16F1.cpp` | Relocations, banked-address conversion |
| Runtime | `toolchain/runtime` | Startup code, multiply, divide, shifts, `memcpy` family; soft float from LLVM's compiler-rt |
| Device files | `toolchain/include`, `toolchain/lib` | Generated from the device database |
| Driver | `toolchain/bin/pic16f1-cc` | Compile, link, convert to HEX |

LLVM itself is not stored here. `llvm/LLVM_VERSION` pins a release;
`scripts/llvm-setup.sh` fetches it and applies the patch and new files.

## Differences from the plan

- **Instruction selection uses SelectionDAG**, not GlobalISel. Stock LLVM's
  GlobalISel legalizer has gaps for a target whose only legal type is i8.
  The assembler layer, linker, runtime and ABI do not depend on this choice.
- **16-bit values are legal**, held in pairs of byte registers, because
  SelectionDAG needs the pointer type to be legal.
- **All frames are on the software stack.** Static frames, bank and page
  placement, and the other optimizations of stages C and D are not started.
- **Test programs exit through `LATD` and `SLEEP`** rather than a
  simulator-only register, so the same binary runs on silicon.

## Register model

The core has one working register. The code generator instead allocates
fourteen byte registers `R0`-`R13` in common RAM (0x70-0x7D), which every
bank can reach without a `MOVLB`. Adjacent pairs form the 16-bit registers
`RP0`-`RP6`. 0x7E and 0x7F are scratch bytes for instruction expansions.

Instruction selection and register allocation work on pseudo instructions
over these registers (`ADD16rr`, `LD8ptr`, `BRCC8ri`, ...). After register
allocation each pseudo is expanded to machine instructions; W, FSR0 and BSR
are scratch inside an expansion. While expanding, the compiler tracks what
W, BSR and FSR0 hold within a basic block and leaves out reloads.

Only the carry flag is modelled as live between instructions, for multi-word
add and subtract. A conditional branch carries its own comparison, so Z is
never live across an instruction boundary and copies may clobber it.

## Calling convention

| Item | Rule |
|---|---|
| Arguments | Bytes in `R0`-`R7`, 16-bit values in `RP0`-`RP3`, then the stack |
| Wider values | Split into 16-bit parts, low part first |
| Results | `R0`-`R7` (up to 8 bytes); larger through a caller-supplied pointer |
| Preserved | `R8`-`R13` |
| Variadic functions | All arguments on the stack |
| Structs | Passed as a pointer to a copy made by the caller |
| Data stack | FSR1, grows down, points at the lowest byte in use |
| Return addresses | Hardware stack (16 levels); depth is not yet checked |
| `PCLATH` | Not preserved. Every `CALL` and `GOTO` is preceded by `MOVLP` |
| `BSR` | Not preserved across calls |

`__attribute__((interrupt))` marks the interrupt handler. It is placed at the
vector (word 4), runs on its own small stack (`__isr_stack_size`, default 32
bytes), saves the byte registers it uses, and returns with `RETFIE`. The
hardware saves and restores W, STATUS, BSR, the FSRs and PCLATH.

## Types

`char` 8 (unsigned), `short` and `int` 16, `long` 32, `long long` 64,
pointers 16, `float` and `double` 32. Everything is byte aligned.

## Address spaces and the object format

ELF32, little-endian, with a provisional machine number (0x5046).

| Range | Meaning |
|---|---|
| `0x000000` up | Program memory, byte addresses (two per word), as in HEX files |
| `0x800000` + FSR address | Data: banked registers `0x0000`, linear RAM `0x2000`, flash as data `0x8000` |

One 16-bit pointer reaches RAM and flash. Constants (`.rodata`) stay in flash,
one byte per word stored as `RETLW`, and are read through an FSR. Writable
globals live in linear RAM and are accessed directly with `MOVLB` plus a
7-bit offset; the linker converts each linear address to bank and offset.

Relocations (`R_PIC16F1_*`): `LO8`, `HI8` (data address bytes), `PM_LO8`,
`PM_HI8`, `PM16` (code word address), `CALL11`, `PAGE7`, `BRA9`, `BANK5`,
`F7`, and plain `8`, `16`, `32`.

The ELF-to-HEX step (`pic16f1-sim elf2hex`) expands the flash-data region to
one word per byte.

## Assembly syntax

Microchip mnemonics, not case sensitive: `addwf f, W|F`, `moviw ++FSR0`,
`moviw 3[FSR1]`, `addfsr FSR1, -4`. Address parts are written `lo8(x)`,
`hi8(x)`, `pm_lo8(x)`, `pm_hi8(x)`, `pm(x)`, `bank(x)`, `off(x)`, `page(x)`.

## Test results

All run on the simulator; nothing has run on silicon yet.

| Suite | Result |
|---|---|
| Benchmark programs (`tests/programs`, 15 programs, `-O0` `-O1` `-O2` `-Os`) | 60 of 60 pass |
| Compiler tests (`tests/compiler`, 16 programs) | 60 of 60 builds pass |
| GCC C torture, `execute` (1,701 tests) at `-O0` | 1,371 of the 1,407 that build pass (97.4%) |
| GCC C torture, `execute` at `-Os` | 1,398 of the 1,435 that build pass (97.4%) |

About 270 torture tests do not build for this device: they need `malloc`,
`printf`, a 32-bit `int`, variable-length arrays, nested functions, or more
flash than 8K words. Eleven crash Clang in builtins that assume a 32-bit
`int` (`__builtin_return_address`, `__builtin_prefetch`).

Of the roughly 45 that build and fail, the 20 examined so far all fail for a
reason outside the compiler: arrays larger than the 512 bytes of RAM,
recursion deeper than the 16-level return stack, loops too long for the
simulator's cycle limit, or reliance on GCC-only attributes. The rest have
not been examined one by one.

The torture tests are not in this repository. `tools/torture.py` explains
how to fetch and run them.

## Known limits

- No `printf`, no heap.
- Floating point works (single precision; `double` is the same type) and
  matches IEEE results bit for bit in the tests, but uses LLVM's generic
  compiler-rt routines. Each of add, multiply and divide is 1,500 to 2,100
  words, so a program using all three nearly fills an 8K-word part.
- No variable-length arrays or `alloca`; the compiler says so.
- No jump tables: a `switch` becomes a chain of comparisons.
- `byval` struct arguments from front ends other than Clang are rejected.
- No debug information.
- Code is about 1.4 times the size of XC8 `-Os` on the benchmark set. Every
  access to a global sets the bank, pointer accesses reload FSR0 unless the
  same pointer was just used, and values wider than a byte are shuffled
  between the fourteen byte registers.
- Hardware stack depth is not analysed; the simulator stops on overflow.
- Not yet run on silicon.
