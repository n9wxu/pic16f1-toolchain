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
| Runtime | `toolchain/runtime` | Startup code; multiply, divide and shifts in assembly, built from the register-independent macros of `toolchain/include/picmath.inc` (64-bit multiply and divide in C); `memcpy` family; soft float from LLVM's compiler-rt |
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
- **All frames are on the software stack.** Static frames and the placement
  passes of stages C and D are not started. The optimizations that are in
  place are described under "Optimization" below.
- **Test programs exit through `LATD` and `SLEEP`** rather than a
  simulator-only register, so the same binary runs on silicon.

## Register model

The core has one working register. The code generator instead allocates
fourteen byte registers `R0`-`R13` in common RAM (0x70-0x7D), which every
bank can reach without a `MOVLB`. Adjacent pairs form the 16-bit registers
`RP0`-`RP6`. 0x7E and 0x7F are scratch bytes for instruction expansions.

FSR0 (`FSR0L`/`FSR0H`, 0x04/0x05) is a fifteenth pair. It also reads the
same in every bank, and it is the only pair a load or store through a
pointer can use. A pass before register allocation
(`PIC16F1PointerSetup.cpp`) puts a copy to FSR0 in front of every such
instruction; the allocator then gives the pointer FSR0 itself where nothing
else needs FSR0 meanwhile, and the copy disappears. A pointer that steps
through an array in a loop so stays in FSR0, and `ADDFSR` moves it. When
no pointer needs it, FSR0 holds ordinary values.

Instruction selection and register allocation work on pseudo instructions
over these registers (`ADD16rr`, `LD8ptr`, `BRCC8ri`, ...). After register
allocation each pseudo is expanded to machine instructions; W and BSR are
scratch inside an expansion. While expanding, the compiler tracks what W
and BSR hold within a basic block and leaves out reloads.

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
  half of a double-width product. A quotient and a remainder of the same
  unsigned operands are one call: the divide routine produces both
  (`sub_mul_udiv_to_urem` in `PIC16F1Combine.td` recognises the
  `x - (x / y) * y` that LLVM writes for the remainder). A signed division
  by a power of two, up to 16 bits, adds the divisor less one to a
  negative value and shifts.
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

## Optimization

What runs after instruction selection, in order:

| Step | What it does |
|---|---|
| Pointer setup (`PIC16F1PointerSetup.cpp`) | Copies each pointer to FSR0 in front of the load or store that uses it; see "Register model" |
| Register allocation with usage across calls (LLVM's IPRA) | A function compiled before its caller tells the caller which registers it changes. The caller keeps values in the others without saving them, and a function that only its own file calls saves nothing. On from `-O1`; `-mllvm -enable-ipra=0` turns it off |
| Pseudo expansion (`PIC16F1ExpandPseudo.cpp`) | Each pseudo instruction becomes machine instructions. Within a block it remembers what W and BSR hold, and which pair FSR0 was copied from, and skips reloads |
| Branch relaxation and branch expansion | A branch out of `BRA`'s reach becomes `GOTO` |
| Peephole (`PIC16F1Peephole.cpp`) | Works on the final instructions, over whole functions; see below |
| Outlining (LLVM's machine outliner) | In functions optimized for size, an instruction sequence that occurs several times in a source file becomes one subroutine; see below |

The peephole pass models W, the Z and C flags, the sixteen bytes of
common RAM that serve as registers, and the two bytes of FSR0. It

- removes a load of W, or a store, that changes nothing, and a load done
  only to set Z when Z already describes that register;
- reads the original instead of a copy, which leaves the copy unused;
- turns "copy a register, then operate on the copy" into the operation on
  the original with the result stored once (`movlw k; addwf a, W; movwf d`
  instead of copying `a` to `d` first);
- removes instructions whose results nothing reads, from liveness computed
  over the function with the actual argument, result and saved registers of
  each call and return;
- turns "load a byte, keep it in a register, test a bit of the register"
  into a bit test of the byte, and "load a byte, keep it in a register,
  use the register as an operand" into the operation on the byte;
- follows which bank is selected, over the whole function, and removes a
  `MOVLB` that selects the bank already selected on every path;
- turns a call followed by a return into a jump, so that the callee
  returns to the caller's caller (one word and one level of the hardware
  stack less);
- uses `DECFSZ`/`INCFSZ` where a decrement or increment is followed by a
  skip on Z;
- reads a byte of the stack frame again instead of the register it was
  copied to, when that leaves the register unused;
- loads a constant instead of a register known to hold it, which W often
  holds already.

Any file register outside common RAM is treated as memory that may be a
device register: an instruction that touches one is never removed, and the
bit-test change keeps it at exactly one read. An instruction after a skip
is never removed or split. `-mllvm -pic16f1-peephole=0` turns the pass off;
`-mllvm -pic16f1-peephole-steps=N` selects its steps by bit, for finding a
fault.

### Shared sequences (outlining)

XC8 calls this procedural abstraction. With `-Os` or `-Oz`, LLVM's machine
outliner looks for instruction sequences that occur more than once in a
source file and puts each, where that saves space, in a subroutine named
`OUTLINED_FUNCTION_n`. Only the hardware stack is involved in a call, so a
sequence that uses the software stack is as good as any other. The target
code is in `PIC16F1InstrInfo.cpp`:

- A sequence must not end in a skip, and must not follow one.
- A sequence that ends in a return is jumped to. One that ends in a call
  becomes a subroutine that ends in a jump to the callee.
- In the `medium` code model the call to the subroutine sets PCLATH, so a
  call inside the sequence must have its own `MOVLP` inside it.
- Callee-saved registers are saved by pushes at the start of a function
  and restored by pops before the return, always in the same order, so
  functions that save the same registers share those instructions.

Each use costs the four cycles of a call and return, and **one more level
of the 16-level hardware stack** while the sequence runs.
`-mno-outline` turns outlining off. The runtime library and picolibc are
built without it: a program links only a few of their functions, and a
sequence shared between functions would then cost more than it saves.

`pic16f1-cc -flto` compiles the whole program as one unit at link time, so
sequences are shared across source files as well, and functions the
program does not use are gone before code is generated.

### Inlining

LLVM's inliner assumes that a call costs several instructions and that a
function of about ten is small. Here a call is one or two words and each
instruction of the function several. At `-Os` and `-Oz`, `pic16f1-cc`
therefore passes `-mllvm -inline-threshold=10 -mllvm
-inline-call-penalty=5`. A function with one caller is still always
inlined. Less inlining means deeper calls: a program close to the 16
levels of the hardware stack may need a higher threshold.

### Fixed costs that were removed

- **Startup code.** `crt0` is twelve words: set the stack pointer, call
  `main`, call `exit`. The loops that copy initialised data and clear
  `.bss` are separate library members (`copydata.S`, `clearbss.S`). The
  compiler refers to `__do_copy_data` from every module that defines
  initialised data and to `__do_clear_bss` from every module that defines
  zeroed data, and that reference is what links the loop in. Assembly
  sources that define data must make the reference themselves
  (`.globl __do_copy_data`).
- **Register saves in `main`** and in functions that do not return. Nothing
  reads the saved registers back. A program that calls `main` itself from
  the file that defines it is detected, and `main` then saves them as usual;
  one that calls `main` from another file must compile the file defining
  `main` with `-mmain-saves-registers` (`pic16f1-cc`) or
  `-mllvm -pic16f1-main-saves-registers` (`clang`).
- **Page selection.** With `-mcmodel=small` the compiler never emits `MOVLP`:
  that is right when all code lies in the first 2K words, where PCLATH stays
  0 from reset. Such code refers to `__pic16f1_one_page`, and the linker
  script then fails the link if the code is larger. The default model,
  `medium`, selects the page before every call. `pic16f1-cc` tries `small`
  first when it compiles and links in one step and builds again for
  `medium` if the link fails; with `-c` it uses `medium` unless told
  otherwise. The runtime library exists for both (`libpicrt-small.a`,
  `crt0-small.o`).
- **Interrupt entry.** An interrupt handler that uses no stack does not
  switch to the interrupt stack, and saves the scratch byte TMP0 only if
  one of its comparisons really uses it. A handler that only counts
  (`ticks++; total += 3;`) is the bit test, the updates of the variables
  in place and `RETFIE`.
- **Variables changed in place.** `x += k`, `x -= y`, `x &= y` and the
  like on a directly addressed 8- or 16-bit variable are instructions on
  the variable's bytes, each read once and written once, with no register
  in between.
- **Multiply and divide routines** for 8, 16 and 32 bits are in assembly
  (`toolchain/runtime/muldiv.S`): 21 words for the 16-bit multiply and 24
  for the 16-bit divide. The 8- and 16-bit ones use only `R0`-`R5` and the
  scratch bytes, and the compiler knows that `R6`, `R7` and FSR0 survive a
  call to them.
- **The same routines on any registers.** The algorithms are assembler
  macros in `toolchain/include/picmath.inc` (`pic_mul8/16/32`,
  `pic_udivmod8/16/32`, `pic_divmod8/16/32`, `pic_neg…`, `pic_abs…`, and
  the 32- and 64-bit shifts). Each takes one file-register address per
  byte, so an assembly source can place one on its own registers:
  `#include <picmath.inc>`, then `pic_mul16 a0, a1, b0, b1, p0, p1`. The
  registers must be reachable with the bank selected at that point; the
  macros never change the bank. The runtime library is these macros placed
  on the calling convention's registers, which is the only place the
  register numbers are fixed. `tests/compiler/picmath` runs every macro on
  banked RAM and checks it against the compiler's arithmetic.
- **Bank selection.** The linker starts a data section of up to 16 bytes in
  the next bank if it would otherwise cross a bank boundary
  (`pic16f1KeepInBank` in lld), and the compiler always gives each variable
  its own section. So one `MOVLB` serves all the bytes of a variable up to
  that size. Assembly sources must keep to the same rule: one variable per
  section. The cost is up to 15 unused bytes of RAM at each bank boundary.

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
| Preserved | `R8`-`R13`. FSR0 is not |
| Variadic functions | All arguments on the stack |
| Structs | Passed as a pointer to a copy made by the caller |
| Data stack | FSR1, grows down, points at the lowest byte in use |
| Return addresses | Hardware stack (16 levels); depth is not yet checked. A call that is the last thing a function does is a jump and uses no level; an outlined sequence uses one while it runs |
| `PCLATH` | Not preserved. Every `CALL` and `GOTO` is preceded by `MOVLP` |
| `BSR` | Not preserved across calls |

`__attribute__((interrupt))` marks the interrupt handler. It is placed at the
vector (word 4), saves the byte registers it uses, and returns with
`RETFIE`. If it needs a stack (to save registers, for locals or for a
call) it runs on its own small one (`__isr_stack_size`, default 32 bytes).
The hardware saves and restores W, STATUS, BSR, the FSRs and PCLATH.

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
| Benchmark programs (`tests/programs`, 15 programs, `-O0` `-O1` `-O2` `-Os`) | 60 of 60 pass, with either instruction selector |
| Compiler tests (`tests/compiler`, 18 programs) | 68 of 68 builds pass, with either instruction selector |
| Regression tests (`tests/lit`) | 20 of 20 pass |
| One program on all five devices (`scripts/test-devices.sh`) | 10 of 10 builds pass |
| GCC C torture, `execute` (1,701 tests) at `-O0` | 1,389 of the 1,422 that build pass (97.7%) |
| GCC C torture, `execute` at `-O1` | 1,403 of 1,440 (97.4%) |
| GCC C torture, `execute` at `-O2` | 1,407 of 1,442 (97.6%) |
| GCC C torture, `execute` at `-Os` | 1,407 of 1,447 (97.2%); seven more programs fit than before the size work |
| picolibc's tests on the PIC16F18877 (141 programs, `-Os`) | 104 pass within 2 billion cycles, 3 exceed that, 33 do not build |
| picolibc's tests on the PIC16F1937 | 74 pass, 66 do not build (39 are too big for 8K words) |

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
On the PIC16F18877 no test gives a wrong result. Of the 33 that do not
build, most need file I/O (`open`, `unlink`), `setjmp`, or standard input,
none of which exist here, and 6 are too large even for 32K words.
`test-abort` ends by calling `abort`, as it is meant to. Three string tests
(`test-memchr`, `test-memmem`, `test-memmove`) were still running at
2 billion cycles and were stopped.

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
- At `-Os` the benchmark set is 5,094 words against 6,601 for XC8 `-Os`
  (0.77), and every one of the fifteen programs is smaller than XC8's
  (from 0.35 times for `arith16` to 0.99 for `crc8tab`, `matrix`, `sieve`
  and `timer_isr`). With `-flto` it is 4,957 words. The set runs in 0.57
  times the cycles (geometric mean); two programs are slower than XC8's:
  `structs` (1.17 times) and `arith32` (1.06). At `-O1` the set is 0.92
  times XC8 `-O1` in size (four programs larger) and no program is
  slower. `-O2` inlines and unrolls for speed: 1.22 times XC8 `-O2` in
  size, 0.24 times the cycles.
- Outlining and the lower inlining threshold at `-Os` trade hardware
  stack levels for space; see "Shared sequences" and "Inlining".
- Only one pointer at a time can be in FSR0 (FSR1 is the stack pointer),
  so a loop over two arrays copies each pointer to FSR0 in turn.
  Array indexing computes a 16-bit address for each access. All frames
  are on the software stack.
- Hardware stack depth is not analysed; the simulator stops on overflow.
- Not yet run on silicon.
