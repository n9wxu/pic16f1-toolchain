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
- **Most frames are on the software stack.** Static frames have been
  started (see "Static frames"); the placement passes of stages C and D
  have not. The optimizations that are in place are described under
  "Optimization" below.
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
through an array in a loop so stays in FSR0, and the access steps it
(`MOVIW FSR0++`). When no pointer needs it, FSR0 holds ordinary values.

FSR1 is the stack pointer, except in a function with a static frame (see
"Static frames"), where it is a second pointer register.

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

### What the levels mean

The aim at every level is code no larger and no slower than XC8's at the
same level, program by program; see "Test results" for where that stands.
LLVM's levels were tuned for machines with memory to spend on speed, so
`pic16f1-cc` adjusts them (clang called directly does none of this):

| Level | What `pic16f1-cc` does |
|---|---|
| `-O0`, or no `-O` option | Clang's `-O1` with `-fno-inline`: every function is kept and none is inlined, but variables live in registers and each function is tidied up. `-munoptimized` gives clang's own `-O0`, with every variable in memory, at about twice the size |
| `-O1`, `-O2` | Clang's level, with less inlining and no loop unrolling (`-funroll-loops` turns unrolling back on) |
| `-O3` | Clang's `-Os` without outlining: small, and nothing that costs time. Clang's own `-O3` spends words on speed that this processor does not have |
| `-Os`, `-Oz` | Clang's level, with less inlining, and shared sequences made into subroutines (outlining) |

At every level a bitwise CRC loop stays a loop: LLVM would replace it with
a 256-entry table (`-mllvm -disable-loop-idiom-hashrecognize`), which is
five times as fast and 450 words larger.

### After instruction selection

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

### Static frames

A function that can never be entered while it is already running needs no
stack frame. `PIC16F1StaticFrames.cpp` finds those functions in each
module and marks them. A function qualifies if

- nothing outside the module can reach it: neither it nor any of its
  callers has external linkage or has its address taken. Code in another
  file may run in an interrupt handler, where it could get to the function
  while the main program is inside it. `main` is the exception: only the
  startup code calls it (unless `-mmain-saves-registers` says otherwise);
- it cannot call itself, directly or through others;
- it cannot run in an interrupt handler of the module.

In an ordinary build that is `main` and the `static` functions that hang
off it. With `-flto` the whole program is one module and nearly every
function qualifies. This is sound without knowing the rest of the program,
which is why it is narrower than XC8's compiled stack: XC8 always sees
everything.

What a marked function gets, so far:

- **Small variables at fixed addresses.** A variable of up to 16 bytes that
  must be in memory (an array, something whose address is passed on) is
  placed in a block `<function>.frame`, in a section `.noinit`, and is
  reached with direct addressing like a global. The linker keeps each
  block within a bank, and startup does not clear it.
  (`-mllvm -pic16f1-static-local-limit=N` changes the size.)
- **FSR1 as a second pointer register.** FSR1 holds the stack pointer on
  entry and must hold it again at every call and at the return, but in
  between it is free. The stack pointer becomes an ordinary value: it
  stays in FSR1 if nothing else wants FSR1, and otherwise waits in a
  register pair. `PIC16F1PointerSetup.cpp` gives FSR1 to a pointer that a
  loop keeps from round to round while FSR0 is busy with another, in a
  loop without calls; the loads and stores through it are separate
  instructions (`LD8ptr1`...) that take their pointer in FSR1 only, which
  holds the register allocator to the choice. A string comparison so has
  one pointer in each FSR and a nine-instruction loop.
  (`-mllvm -pic16f1-fsr1-pointer=0` turns this off.) Not for a function
  that takes or passes arguments on the stack, or does 64-bit arithmetic.
- **Spill slots at fixed addresses**, in `<function>.slots`, but only where
  FSR1 really is in use for something else: a slot on the stack needs no
  bank selection, so the slots stay there otherwise.

`-mllvm -pic16f1-static-frames=0` turns all of it off.

Not done yet: the fixed variables of functions that are never active
together do not share memory, so a deep program can use more RAM than it
would on the stack (variables over 64 bytes therefore stay on the stack);
saved registers and outgoing arguments are still on the stack; and
arguments are still passed in registers and on the stack, not in fixed
locations.

### Pointers through arrays

`a[i]` in a loop is the address of `a` plus `i`: a 16-bit addition into an
FSR for every access. Three things turn it into a pointer that walks the
array and stays in an FSR:

- **`PIC16F1PointerIVs.cpp`** rewrites an address made of something fixed
  for the loop plus a constant multiple of a loop counter into a pointer
  that starts at the first element and is stepped each round. LLVM's own
  loop strength reduction does not, because the counter is usually a byte
  widened for the address and it does not look through the widening. The
  pass runs first, so the generic one then sees a counter that only counts
  and makes it count down to zero (`DECFSZ`). Elements a small constant
  apart share a pointer (`MOVIW -1[FSR0]`). A loop that needs more
  addresses than there are pointer registers (one, or two in a function
  with a static frame) is left alone, and so is a loop with calls.
  `-mllvm -pic16f1-pointer-ivs=0` turns it off.
- **The pointer-setup pass pins the pointer.** A pointer that a loop keeps
  from round to round gets FSR0 (or FSR1, see "Static frames") for the
  loop: its loads and stores become forms that take their pointer in that
  FSR only (`LD8ptr0`, `LD8ptr1`...), which holds the register allocator
  to it. A hint alone often left the pointer in a register pair, copied to
  FSR0 for each access. An increment in front of an access through the old
  value is moved to after it.
- **The peephole joins the step to the access** (`MOVIW FSR0++`), and moves
  a decrement down to its test for zero where it can (`DECFSZ`).

A loop that fills an array is then `MOVWI FSR0++; DECFSZ n; BRA`.

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
instruction of the function several. `pic16f1-cc` therefore passes
`-mllvm -inline-threshold=10 -mllvm -inline-call-penalty=5` at every
level (give your own `-mllvm -inline-threshold=` to override it). A function with one caller is still always
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
  call to them. The 32-bit divide lines the divisor up with the top of the
  word first and runs only as many rounds as that took, so dividing by a
  large number is quick; it leaves the remainder in place of the dividend,
  which makes it the remainder routine as well.
- **Multiplying by a constant** with two set bits, or by the difference of
  two powers of two (3, 5, 6, 7, 10, 257...), is two shifts and an add or
  subtract when the shifts are short: eight instructions for `x * 3`
  against some sixty in the multiply routine, in about the room of the
  call.
- **The same routines on any registers.** The algorithms are assembler
  macros in `toolchain/include/picmath.inc` (`pic_mul8/16/32`,
  `pic_udivmod8/16/32`, `pic_udivmod32_norm`, `pic_divmod8/16/32`, `pic_neg…`, `pic_abs…`, and
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
| Benchmark programs (`tests/programs`, 15 programs, `-O0` `-O1` `-O2` `-O3` `-Os`) | 75 of 75 pass; the four default levels also with the SelectionDAG selector |
| Compiler tests (`tests/compiler`, 18 programs) | 72 of 72 builds pass, with either instruction selector; 14 of 14 with `-munoptimized` (four are too big for the device that way) |
| Regression tests (`tests/lit`) | 22 of 22 pass |
| One program on all five devices (`scripts/test-devices.sh`) | 10 of 10 builds pass |
| GCC C torture, `execute` (1,701 tests) at `-O0` | 1,400 of the 1,438 that build pass (97.4%) |
| GCC C torture, `execute` at `-O0 -munoptimized` | 1,389 of 1,422 (97.7%) |
| GCC C torture, `execute` at `-O1` | 1,402 of 1,440 (97.4%) |
| GCC C torture, `execute` at `-O2` and at `-O3` | 1,404 of 1,440 (97.5%) |
| GCC C torture, `execute` at `-Os` | 1,407 of 1,447 (97.2%) |
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

## Hand-written assembly as a yardstick

`tests/asm` has each of the fifteen benchmark programs written by hand in
assembly, taking the C program as the specification: the same output, exit
code and effect on the hardware. `scripts/build-asm-refs.sh` builds them
(stand-alone: no startup code, no library) and
`tools/bench.py build/out/asm-pic16f1` runs and checks them like any other
build. There are two versions of each:

- **`main.S`: the smallest and fastest program that does the job.**
  Whatever can be worked out beforehand is, as someone writing assembly
  would. None of the programs has input, so for thirteen of them that is
  all of the output, and the program is the initialisation, one loop
  that sends a string, and the exit. Two programs touch the hardware in
  ways that stay: `bitops` writes the port latch six times and reads a
  pin, which decides the last digit it prints and its exit code;
  `timer_isr` waits for ten real Timer0 interrupts.
- **`runtime.S`: everything computed when the program runs**, by the
  algorithm the C uses (a bit-at-a-time CRC stays bit-at-a-time, a bubble
  sort stays a bubble sort). It is free in where values live, how
  routines are called, and in widths and special cases that follow from
  the values the program uses; a product or shift by a loop counter is
  carried from one round to the next. This is the measure of the code a
  compiler generates.

At `-Os`:

| Program | Hand, words | Hand at run time, words | LLVM, words | XC8, words | Hand, cycles | Hand at run time, cycles | LLVM, cycles | XC8, cycles |
|---|---|---|---|---|---|---|---|---|
| arith16 | 99 | 334 | 265 | 763 | 686 | 50,489 | 3,356 | 147,863 |
| arith32 | 88 | 377 | 374 | 885 | 576 | 25,109 | 57,081 | 97,697 |
| bitops | 95 | 162 | 318 | 346 | 570 | 29,360 | 75,254 | 120,595 |
| crc16 | 53 | 131 | 230 | 279 | 226 | 6,684 | 11,261 | 16,918 |
| crc32 | 48 | 124 | 206 | 274 | 176 | 1,294 | 1,881 | 2,851 |
| crc8tab | 55 | 381 | 454 | 461 | 246 | 3,829 | 6,922 | 22,451 |
| fib | 59 | 140 | 155 | 311 | 286 | 1,012 | 676 | 2,952 |
| fsm | 57 | 310 | 556 | 596 | 266 | 1,997 | 3,058 | 4,585 |
| hello | 48 | 48 | 75 | 83 | 176 | 176 | 308 | 488 |
| matrix | 53 | 258 | 577 | 585 | 226 | 17,826 | 31,956 | 44,196 |
| sieve | 54 | 141 | 281 | 287 | 236 | 16,542 | 32,705 | 33,838 |
| sort | 75 | 266 | 464 | 529 | 446 | 12,493 | 24,142 | 28,704 |
| strings | 74 | 238 | 487 | 532 | 436 | 3,661 | 7,837 | 11,015 |
| structs | 53 | 170 | 341 | 405 | 226 | 914 | 2,968 | 3,292 |
| timer_isr | 85 | 145 | 252 | 265 | 5,468 | 5,645 | 5,933 | 6,181 |
| **Total** | 996 | 3,225 | 5,035 | 6,601 | | | | |

LLVM's code is 5.1 times the size of the smallest programs and
1.56 times the size of the ones that compute at run time; in
cycles, 18.7 and 1.40 times (geometric means).

LLVM does some of the working out itself: all of `fib`, every loop of
`arith16` but the `gcd` loops, and part of `arith32` are evaluated while
compiling, which is why it is smaller or faster than the run-time hand
version in those rows. It does not get as far as a person does for the
rest, because it will not run a long loop at compile time.

Where the run-time hand versions gain most, and so where the compiler
has most to find:

- No software stack and no calling convention: values stay where the next
  routine wants them, and nothing is saved.
- Both FSRs are used as pointers (the compiler has one; FSR1 is its stack
  pointer), with `MOVIW`/`MOVWI` stepping them.
- Arrays are placed so that an address is one byte.
- Counted loops run down to zero with `DECFSZ`, and flags left by one
  instruction are used by the next test.
- The output routines are a fraction of the size of `tests/common/bench.c`
  compiled: 48 words for the whole of `hello` against 75.

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
- Against XC8 at the same level, every one of the fifteen benchmark
  programs is no larger and no slower, at each of XC8's five levels:

  | Level | Words, LLVM / XC8 | Closest program in size | Cycles (geometric mean) | Closest program in speed |
  |---|---|---|---|---|
  | `-O0` | 5,557 / 8,542 (0.65) | `crc8tab` 0.89 | 0.37 | `timer_isr` 0.89 |
  | `-O1` | 5,330 / 7,818 (0.68) | `fsm` 0.95 | 0.41 | `timer_isr` 0.90 |
  | `-O2` | 5,243 / 7,697 (0.68) | `crc8tab` 0.94 | 0.40 | `timer_isr` 0.90 |
  | `-O3` | 5,129 / 6,790 (0.76) | `crc8tab` 0.98 (9 words) | 0.51 | `sieve` 0.98 |
  | `-Os` | 5,035 / 6,601 (0.76) | `matrix` 0.99 (8 words) | 0.51 | `sieve` 0.97 |

  That holds because of what `pic16f1-cc` makes of each level; see "What
  the levels mean". `-Oz` has no XC8 counterpart; against XC8 `-Os` it is
  smaller in total (4,999 words) but two programs are larger (`crc8tab`
  by 1 word, `matrix` by 19). `-munoptimized` is about 1.4 times XC8
  `-O0` in size. With `-flto` the `-Os` set is 4,912 words.
- Outlining at `-Os` and the lower inlining threshold at every level trade hardware
  stack levels for space; see "Shared sequences" and "Inlining".
- Outside functions with a static frame, only one pointer at a time can
  be in an FSR (FSR1 is the stack pointer), so a loop over two arrays
  copies each pointer to FSR0 in turn. Array indexing computes a 16-bit
  address for each access. Static frames cover only functions that
  nothing outside their module can reach, and are a first stage; see
  "Static frames".
- Hardware stack depth is not analysed; the simulator stops on overflow.
- Not yet run on silicon.
