# LLVM toolchain: design of the first working C compiler

This describes what is implemented, as opposed to the full plan in
[plan.md](plan.md). The precise rules are in the
[specifications](spec/README.md). It is the plan's "C MVP" (workstream 2, stage B): correct
first, with the simplest design that works.

## Components

| Component | Where | Notes |
|---|---|---|
| LLVM target `pic16f1` | `llvm/new-files/llvm/lib/Target/PIC16F1` | Assembler, object writer, code generator |
| Clang target | `llvm/new-files/clang`, `llvm/existing-files.patch` | Type sizes, ABI, `interrupt` attribute |
| LLD port | `llvm/new-files/lld/ELF/Arch/PIC16F1.cpp` | Relocations, banked-address conversion |
| Runtime | `toolchain/runtime` | Startup code, multiply, divide, shifts, `memcpy` family; soft float from LLVM's compiler-rt |
| Device files | `toolchain/include`, `toolchain/lib` | Generated from the device database |
| C library | `toolchain/picolibc` | picolibc at a pinned commit, with a small patch; built for 32- and 64-bank devices |
| Driver | `toolchain/bin/pic16f1-cc` | Compile, link, convert to HEX |

LLVM itself is not stored here. `llvm/LLVM_VERSION` pins a release;
`scripts/llvm-setup.sh` fetches it and applies the patch and new files.

## Differences from the plan

- **Instruction selection uses GlobalISel**, as the plan says, but the
  first working compiler used SelectionDAG. That selector is still in the
  tree: a function GlobalISel cannot handle falls back to it, and
  `-mllvm -global-isel=0` selects it for everything. On the two benchmark
  sets GlobalISel's code is smaller at every optimization level (8% at
  `-Os`, 11% at `-O2`, 2% at `-O0` on `tests/programs`) and it passes every
  torture test SelectionDAG passes. See "Instruction selection" below.
- **16-bit values are legal**, held in pairs of byte registers, as well as
  bytes. The plan has i8 as the only legal type.
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

## Instruction selection

GlobalISel runs in the usual steps, with the code in
`llvm/lib/Target/PIC16F1/GISel`:

| Step | What this target does |
|---|---|
| Call lowering | Assigns arguments and results by the calling convention below |
| Combiner, before legalization | All of LLVM's generic combines |
| Legalizer | Brings every value to 8 or 16 bits |
| Combiner, after legalization | Folds the constants and undefined pieces that splitting leaves behind |
| Register bank selection | One bank: the byte registers and their pairs |
| Instruction selector | Hand-written; picks the pseudo instructions |

What the legalizer does with wider values:

- 32- and 64-bit add and subtract become a chain of 16-bit operations
  linked through the carry flag.
- An ordering comparison is the borrow out of a subtract chain. Equality
  compares piece by piece. A test of the sign looks only at the top piece.
- A shift by one or two bits is linked through the carry flag as well
  (`LSLF` then `RLF`, or `LSRF`/`ASRF` then `RRF`). A shift by a multiple
  of eight moves bytes. Other wide shifts call the runtime.
- Multiply and divide call the runtime at every width. Division by a
  constant stays a division, because the usual replacement needs the high
  half of a double-width product.
- Floating point is single precision in software: a `float` is carried as
  a 32-bit integer and each operation is a runtime call.

The selector keeps a carry in the flag only when the next link of the chain
follows with nothing in between that could change it; otherwise the function
falls back to SelectionDAG. So does a function with inline assembly that has
an operand wider than 16 bits. To see whether anything falls back, compile
with `-mllvm -global-isel-abort=2`, which prints a warning for each such
function.

Both combiners stop after a fixed number of rule applications per function
(1,000 plus 100 per instruction) and print a warning. LLVM's combiner runs
until no rule applies, and two rules that undo each other would otherwise
run for ever and allocate memory until the machine stops; one such pair in
LLVM's generic rules did exactly that here and is now excluded
(`hoist_logic_op_except_trunc` in `PIC16F1Combine.td`).

### Running the test suites safely

macOS enforces no per-process memory limit, so a compiler that allocates
without bound takes the whole machine down. `tools/memguard.py -- command`
runs a command and kills any process under it that passes a memory limit;
`tools/torture.py` and `scripts/build-picolibc.sh` have the same watch built
in. Use it around anything that runs a freshly changed compiler many times.

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

## Using the C library

```c
#include <stdio.h>
#include <xc.h>

static int uart_putc(char c, FILE *f)
{
    while (!PIR1bits.TXIF) { }
    TXREG = c;
    return c;
}
static FILE uart = FDEV_SETUP_STREAM(uart_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &uart;
```

Build with `pic16f1-cc --libc`. `tests/libc/hello` is a complete example.

## Assembly syntax

Microchip mnemonics, not case sensitive: `addwf f, W|F`, `moviw ++FSR0`,
`moviw 3[FSR1]`, `addfsr FSR1, -4`. Address parts are written `lo8(x)`,
`hi8(x)`, `pm_lo8(x)`, `pm_hi8(x)`, `pm(x)`, `bank(x)`, `off(x)`, `page(x)`.

## Test results

All run on the simulator; nothing has run on silicon yet.

| Suite | Result |
|---|---|
| Assembler against Microchip `pic-as` (`scripts/test-assembler.sh`) | 57 of 57 instruction forms identical |
| Benchmark programs (`tests/programs`, 15 programs, `-O0` `-O1` `-O2` `-Os`) | 60 of 60 pass |
| Compiler tests (`tests/compiler`, 16 programs) | 60 of 60 builds pass |
| Regression tests (`tests/lit`) | 14 of 14 pass |
| One program on all five devices (`scripts/test-devices.sh`) | 10 of 10 builds pass |
| GCC C torture, `execute` (1,701 tests) at `-O0` | 1,388 of the 1,421 that build pass (97.7%) |
| GCC C torture, `execute` at `-O1` | 1,403 of 1,440 (97.4%) |
| GCC C torture, `execute` at `-O2` | 1,407 of 1,441 (97.6%) |
| GCC C torture, `execute` at `-Os` | 1,405 of 1,440 (97.6%) |
| picolibc's tests on the PIC16F18877 (141 programs, `-Os`) | 101 pass within 2 billion cycles, 4 exceed that, 35 do not build |
| picolibc's tests on the PIC16F1937 | 53 pass, 1 exceeds the cycle limit, 86 do not build (51 are too big for 8K words) |

About 270 torture tests do not build for this device: they need `malloc`,
`printf`, a 32-bit `int`, variable-length arrays, nested functions, or more
flash than 8K words. Eleven crash Clang in builtins that assume a 32-bit
`int` (`__builtin_return_address`, `__builtin_prefetch`).

Of the roughly 45 that build and fail, the 20 examined so far all fail for a
reason outside the compiler: arrays larger than the 512 bytes of RAM,
recursion deeper than the 16-level return stack, loops too long for the
simulator's cycle limit, or reliance on GCC-only attributes. The rest have
not been examined one by one.

### picolibc

All of picolibc compiles. Its own tests were run from the directories `test`,
`test-stdio`, `test-stdlib`, `test-string`, `test-ctype` and `test-stdbit`
(141 programs; the math, POSIX, locale and semihosting tests were not run).
On the PIC16F18877 no test gives a wrong result. Of the 35 that do not
build, most need file I/O (`open`, `unlink`), `setjmp`, or standard input,
none of which exist here, and 8 are too large even for 32K words.
`test-abort` ends by calling `abort`, as it is meant to. Four string tests
(`test-ffs`, `test-memchr`, `test-memmem`, `test-memmove`) were still
running at 2 billion cycles and were stopped; with the SelectionDAG
selector two of them finished within 20 billion.

The torture tests are not in this repository. `tools/torture.py` explains
how to fetch and run them.

## Known limits

- `printf` and the rest of the C library come from picolibc, built
  separately (`scripts/build-picolibc.sh`) and selected with
  `pic16f1-cc --libc`. `printf` handles integers only by default, and a
  program using it is about 3,600 words. `setjmp` is not implemented.
  `malloc` works; the heap sits between the data and the stack.
- Floating point works (single precision; `double` is the same type) and
  matches IEEE results bit for bit in the tests, but uses LLVM's generic
  compiler-rt routines. Each of add, multiply and divide is 1,500 to 2,100
  words, so a program using all three nearly fills an 8K-word part.
- No variable-length arrays or `alloca`; the compiler says so.
- No jump tables: a `switch` becomes a chain of comparisons.
- `byval` struct arguments from front ends other than Clang are rejected.
- No debug information.
- Code is about 1.2 times the size of XC8 `-Os` on the benchmark set (7,926
  words against 6,601) and runs in 0.81 times the cycles (geometric mean;
  six of the fifteen programs are slower). Every
  access to a global sets the bank, pointer accesses reload FSR0 unless the
  same pointer was just used, and values wider than a byte are shuffled
  between the fourteen byte registers.
- Hardware stack depth is not analysed; the simulator stops on overflow.
- Not yet run on silicon.
