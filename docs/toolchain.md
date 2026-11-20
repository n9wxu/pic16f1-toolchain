# LLVM toolchain: design of the first working C compiler

This describes what is implemented, as opposed to the full plan in
[plan.md](plan.md). The precise rules are in the
[specifications](spec/README.md). It is the plan's "C MVP" (workstream 2, stage B): correct
first, with the simplest design that works.

## Components

| Component | Where | Notes |
|---|---|---|
| Languages other than C | [languages.md](languages.md) | C++, Rust and Zig |
| LLVM target `pic16f1` | `llvm/new-files/llvm/lib/Target/PIC16F1` | Assembler, object writer, code generator |
| Clang target | `llvm/new-files/clang`, `llvm/existing-files.patch` | Type sizes, ABI, `interrupt` attribute |
| LLD port | `llvm/new-files/lld/ELF/Arch/PIC16F1.cpp` | Relocations, banked-address conversion |
| Runtime | `toolchain/runtime` | Startup code, with the constructors of global objects; multiply, divide and shifts in assembly, built from the register-independent macros of `toolchain/include/picmath.inc` (64-bit multiply and divide in C); `memcpy` family; single-precision floating point (`float.c`), with only the 64-bit conversions from LLVM's compiler-rt; delays; `setjmp` |
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
- **Frames are static where they can be** (see "Static frames"): in a
  whole-program build nearly every function that cannot be re-entered has
  its variables at fixed addresses, shared with functions never active at
  the same time. The rest use the software stack. The placement passes of
  stages C and D are not written. The optimizations that are in place are
  described under "Optimization" below.
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

FSR1 is a second pointer register in a function with a static frame, and
the stack pointer in one with a stack frame; see "Static frames".

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

**Whole-program compilation is the default.** A build that compiles and
links in one step (`pic16f1-cc -o prog.hex a.c b.c`) is compiled as one
unit (`-flto`): the compiler sees every call. It inlines across files,
leaves out what the program does not use, and gives every function that is
not re-entered a static frame without needing the linker to choose (see
"Static frames"). It also turns calls through constant function pointers
into direct calls: a program put together by dependency injection, with
an interface that is a constant table of function pointers handed to the
code that uses it, comes out as if the calls had been written directly.
`tests/inject` is such a program: 69 words compiled as a whole, with no
indirect call left, against 157 words and two indirect calls file by
file. `-fno-lto` compiles each file on its own; with `-c` that is the
default, and `-flto` makes objects for a whole-program link.

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

Those the compiler can settle from one source file: `main` and the
`static` functions that hang off it. For every other function the answer
depends on the rest of the program, so the decision is left to the linker.

#### Compiled several ways, chosen at link time

A function the compiler cannot decide about is compiled three times:

| Version | Frame | Used when |
|---|---|---|
| `F` | on the stack | it may be re-entered; every ordinary call refers to this name |
| `F.sf` | static | it cannot be re-entered |
| `F.isr` | static, with variables of its own | it runs in an interrupt handler |

Each object file also carries a section `.pic16f1.frames` that lists its
functions and what each calls. The linker (`pic16f1SelectFrames` in lld)
reads them all, and so knows every call in the program. It points `F` at
`F.sf` unless

- `F` is on a cycle of calls;
- code that says nothing about its calls can reach `F`. An object file
  without the section is taken to call everything it refers to, from
  anywhere. The startup code and runtime library carry the section;
  hand-written assembly that calls C functions should too (see the end
  of `toolchain/runtime/crt0.S` for the form), or those functions keep
  stack frames;
- both the main program and an interrupt handler can reach `F` (but see
  below).

A call through a pointer is taken to reach every function whose address is
taken. The versions not chosen are unreferenced and `--gc-sections` drops
them, so they cost compile time and nothing else. `-Wl,--verbose` prints
each decision and the reason.

**The hardware stack.** Return addresses go on a stack of 16 levels that
a program cannot extend; a call too many loses a return address or resets
the part. With every call in view the linker counts the levels the deepest
chain of calls takes: one for a call, none for a jump to a function that
returns in the caller's place (the records distinguish them: `C` and `J`),
one more for an interrupt, wherever it arrives, and its handler's calls on
top. If that is more than 16 it warns and names the chain:

```
ld.lld: warning: hardware stack: 18 of 16 levels used at most; 17 by
_start > main > f15 > ... > f0 -> __udivqi3; 1 by an interrupt and 0 by isr
```

`-mstack-report` prints the count for any program, and `-Wl,--fatal-warnings`
makes the warning an error. A recursive program has no bound, nor has one
with code that does not describe its calls; the report says so and names
where. Over the benchmark and compiler test programs at every level the
count is never less than the depth the simulator measures, and equal to it
in 133 of 139 that have a bound (`scripts/test-stack.sh` checks a chain of
known depth).

Callers are compiled against `F` and may know exactly which registers it
changes; `F.sf` and `F.isr` are held to changing no others.

**Functions shared with an interrupt handler.** One copy with a stack
frame, or two copies with static frames: `F.sf` for the main program and
`F.isr` for the handlers. Code that runs in a handler (a handler itself,
or the `.isr` version of a function) calls `G$isr` where it means `G`, and
the linker makes that name the interrupt copy of `G` where it makes one
and `G` itself otherwise. Two copies cost flash and save time. On a test
with a function that has a local array and one that walks two arrays, both
called from the main program and from a handler:

| | Flash words | Cycles |
|---|---|---|
| One copy, stack frames (`-Os`) | 378 | 106,149 |
| Two copies, static frames (`-Os`) | 405 | 52,549 |
| One copy, stack frames (`-O2`) | 383 | 109,508 |
| Two copies, static frames (`-O2`) | 428 | 54,908 |

By default the linker makes the copies only where the two static versions
together are no larger than the stack version; `pic16f1-cc
-minterrupt-copies` (`-z pic16f1-interrupt-copies` to the linker) makes
them wherever it can. A function only handlers reach gets its `.isr`
version.

`scripts/test-frames.sh` checks the decisions on a program with a function
of each kind. `-mllvm -pic16f1-dual-frames=0` turns the extra versions off;
in a whole-program build, the default, few are needed: the compiler then
settles nearly every function itself, and the versions and the linker's
choice matter for programs built from separately compiled objects and
libraries.

#### What a static frame is

A function with a static frame never touches the software stack.

- **Its variables have fixed addresses.** What must be in memory (an
  array, something whose address is passed on, a value the register
  allocator could not keep in a register) is placed in blocks of up to 16
  bytes named `<function>.frame` and `<function>.slots`, in sections
  `.noinit.*`, and reached with direct addressing like a global. The
  linker keeps each block within a bank, and startup does not clear them.
- **So do the registers it saves**, in `<function>.saved`.
- **FSR1 is a second pointer register.** Nothing has to be moved out of
  it first. `PIC16F1PointerSetup.cpp` gives it to a pointer that a loop
  keeps from round to round while FSR0 is busy with another; see "Pointers
  through arrays". A string comparison has one pointer in each FSR and a
  nine-instruction loop. (`-mllvm -pic16f1-fsr1-pointer=0` turns this off.)

A function cannot have one if it takes or passes arguments on the stack
(more than eight bytes of arguments, a variadic function or a call to one,
64-bit arithmetic, whose library routines take stack arguments), or has a
variable of more than 64 bytes (`-mllvm -pic16f1-static-slot-limit=N`).
Such a function has a stack frame whatever the linker finds.

#### Where the stack pointer is

A program whose functions all have static frames has no stack: nothing
sets FSR1 up, and nothing is linked in for it. This is the usual case, and
it is how XC8 works.

A function with a stack frame still uses FSR1 as its stack pointer while
it runs, but cannot expect to find it there: the functions it calls and is
called by may have static frames and use FSR1 for other things. Between
functions the stack pointer lives in a two-byte cell, `__sp`
(`toolchain/runtime/stack.S`, linked only into a program that refers to
it, with eight words of startup code that set it to the end of RAM). A
function with a stack frame

- loads FSR1 from `__sp` on entry, and again after every call;
- stores FSR1 to `__sp` once its frame is set up, if it makes calls, so
  that what it calls starts below its frame, and again before it returns.

That is five instructions a time, which is the price of a stack frame
over a static one. An interrupt handler that makes calls runs them on its
own stack: it saves `__sp`, puts its stack pointer there, and puts the old
value back at the end. No call preserves FSR1.

`-mllvm -pic16f1-static-frames=0` turns static frames off altogether.

**Frames that share memory.** Two functions that are never active at the
same time do not need their variables at different addresses. With every
call in view the linker places each static frame where the frames of all
the functions that can call it have ended: along any chain of calls the
frames follow one another, and functions on different chains share. What
runs in an interrupt handler comes after everything the main program has,
since a handler can start between any two instructions; a call through a
pointer is taken to reach every function whose address is taken. This is
what XC8 calls its compiled stack. `-Wl,-z,pic16f1-no-frame-overlay` turns
it off. `scripts/test-overlay.sh` runs a program whose functions each keep
a pattern in their variables across the calls they make, with a handler
running meanwhile; its frames take 30 bytes where they took 44.

Not done yet: a function with a variable of more than 64 bytes keeps its
stack frame, which sharing has made less necessary; and a function that
passes arguments on the stack cannot have a static frame, though only the
call needs the stack.

### Variable-length arrays and `alloca`

Both take memory from the stack while the function runs, so a function
that has one always has a stack frame. Functions without one are not
affected in any way.

- **Allocating** lowers the cell `__sp` by the size. The functions called
  from here start their frames from the cell, so they land below the new
  bytes without knowing of them.
- **FSR1 does not move.** It stays at the fixed part of the frame, so the
  ordinary variables keep their constant offsets. The function keeps a copy
  of it in 0x7C/0x7D, which it saves and reserves, and takes FSR1 back from
  there after a call (four instructions).
- **Arguments passed on the stack** are stored from the cell's value, below
  the arrays, where the callee looks for them. Room for them is kept under
  every allocation.
- **Giving back.** The epilogue writes the stack pointer of entry to the
  cell, which returns everything at once: that is `alloca`'s lifetime. A
  variable-length array is returned at the end of its block, by saving the
  cell on the way in and restoring it on the way out.

Not in an interrupt handler, which runs on a stack of its own; the compiler
says so. Nothing checks at run time that an allocation fits in the RAM
left. These functions are compiled by the SelectionDAG selector.
`scripts/test-vla.sh` runs `tests/vla/main.c` on two devices at six levels,
whole program and file by file.

### `setjmp` and `longjmp`

In the runtime library (`toolchain/runtime/setjmp.S`, declared in
`<setjmp.h>`), so they are there with or without the C library.
`__builtin_setjmp` and `__builtin_longjmp` are calls to the same two
routines.

Return addresses are on the core's 16-level hardware stack, not in memory,
but the enhanced mid-range core lets a program write that stack's pointer
and top entry. `longjmp` only ever goes back to a function that is still
running, so the entries below the one `setjmp` was called with are still
what they were: `longjmp` sets the pointer back, writes back the one entry
`setjmp` returned through (it has been used again since) and returns.

The buffer is 10 bytes: that return address, the stack pointer cell `__sp`
and the hardware stack pointer (packed into three bytes), and the six
registers a function preserves, since a function that is jumped over never
gets to restore the ones it saved. Restoring `__sp` also gives back every
variable-length array made since.

- Variables with static storage, which is where a function with a static
  frame keeps its own, are left as they are. C makes the variables changed
  between `setjmp` and `longjmp` indeterminate unless they are `volatile`.
- A function that calls `setjmp` is not outlined from: the return address
  kept must be in the function itself, not in a shared sequence.
- Not for leaving an interrupt handler, which has a stack of its own.

`scripts/test-setjmp.sh` runs `tests/setjmp/main.c` on two devices at six
levels, whole program and file by file.

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

In a whole-program build (the default) sequences are shared across source
files as well.

### Switches

A `switch` is comparisons, tests of a bit mask, or a table of jumps:
the index goes to W and `BRW` skips into a table of `GOTO`s that follows
it (one word an entry in the one-page code model, `MOVLP` and `GOTO`
otherwise, with W doubled). The table costs about seven words apart from
its entries, one of which every value in the range needs, and a dozen
cycles whatever the value; comparisons cost about three words for each run
of consecutive values that go to one place, and time that grows with the
number of runs. So a table is made

- at the levels that count size (`-O3`, `-Os`, `-Oz`), only if it is no
  larger than the comparisons;
- otherwise if there are at least eight runs and it is no more than twice
  the size;
- never for fewer than four destinations (bit masks do those), or a range
  over 255 values (127 with two-word entries).

The driver also stops LLVM from copying the block that ends in the table
jump into every block that leads to it, which it does for processors that
predict branches. On a test program of six switches the tables make the
code 11% smaller at `-O1` and 19% at `-Os`, and faster by 4% and 18%
(`scripts/test-switch.sh` runs it in both code models, through both
instruction selectors, and with tables off).

### Arguments and results in W

The calling convention passes every byte in a byte register, and a value
gets there through W: `movlw 62` / `movwf R0` / `call put`. Hand-written
code would often leave it in W. Whether that is better depends on the
function (does it use the argument at once, does it return constants?) and
on its callers (do they have the value in W, or already in `R0` from the
call before?), so the code generator decides function by function, by
trying. For each function that only direct calls in the program reach, it
tries

- the byte result in W,
- each byte argument in W, one at a time, and
- the first argument in W together with the results of the functions whose
  result is passed straight on as that argument (`put(hex(c))` costs
  nothing in `R0` and nothing in W, but a word if `hex` returns one way and
  `put` takes the other),

rewrites the function and every caller, cleans them up again, and keeps the
change only if those functions together are no larger, no slower (counting
instructions by the depth of the loops they are in), and better in one of
the two. A constant result then returns with `RETLW`, a result that is
tested at once is tested in W, and a constant argument is one instruction.

Everything that code outside the compilation can see keeps the documented
convention: functions with external linkage in a build without `-flto`,
functions whose address is taken, interrupt handlers, the runtime library.
With the default whole-program build that leaves nearly every function of
the program free to vary.

| | Words, off | Words, on |
|---|---|---|
| `tests/wcall/wcall.c`, `-O1` | 339 | 321 |
| `tests/wcall/wcall.c`, `-Os` | 304 | 290 |
| The 15 benchmarks, `-O1` | 5,052 | 5,022 |
| The 15 benchmarks, `-Os` | 4,837 | 4,807 |

The benchmarks gain little because most of their calls are inlined; a
program made of small functions that hand bytes to each other gains more.
No benchmark got larger or slower.

`-Wl,-mllvm,-pic16f1-w-report` (with `-fno-lto`: `-mllvm
-pic16f1-w-report`) lists the functions that were changed and the words
saved; `-pic16f1-w-conventions=0` in the same two places turns the choice
off. `scripts/test-wcall.sh` checks the results both ways, in both code
models and through both instruction selectors.

In a debugger, a parameter or variable whose value now lives only in W is
shown as unavailable until it is stored (it was never in its register).
This is at the optimized levels only: `-munoptimized` builds use the
documented convention throughout.

### Inlining

LLVM's inliner assumes that a call costs several instructions and that a
function of about ten is small. Here a call is one or two words and each
instruction of the function several. `pic16f1-cc` therefore passes
`-mllvm -inline-threshold=10 -mllvm -inline-call-penalty=5` at every
level (give your own `-mllvm -inline-threshold=` to override it). A function with one caller is still always
inlined. Less inlining means deeper calls: a program close to the 16
levels of the hardware stack may need a higher threshold.

### Fixed costs that were removed

- **Startup code.** `crt0` is eight words: call `main`, call `exit`. (The
  stack pointer is set up only in a program that has a stack; see "Where
  the stack pointer is".) The loops that copy initialised data and clear
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
| Preserved | `R8`-`R13`. Neither FSR is |
| Variadic functions | All arguments on the stack |
| Structs | Passed as a pointer to a copy made by the caller |
| Functions only the program calls | May take one byte argument, and return a byte result, in W: see "Arguments and results in W" |
| Data stack | Only for functions with stack frames. Grows down; the pointer is in the cell `__sp` between functions and in FSR1 inside one, pointing at the lowest byte in use. At a call with stack arguments FSR1 points at them as well |
| Return addresses | Hardware stack (16 levels); the linker counts the levels the program needs (see "The hardware stack"). A call that is the last thing a function does is a jump and uses no level; an outlined sequence uses one while it runs |
| `PCLATH` | Not preserved. Every `CALL` and `GOTO` is preceded by `MOVLP` |
| `BSR` | Not preserved across calls |

`__attribute__((interrupt))` marks the interrupt handler. It is placed at the
vector (word 4), saves the byte registers it uses, and returns with
`RETFIE`. If it needs a stack (to save registers, for locals or for a
call) it runs on its own small one (`__isr_stack_size`, default 32 bytes).
The hardware saves and restores W, STATUS, BSR, the FSRs and PCLATH. Code
that runs in a handler calls `G$isr` where it means `G`; see "Static
frames".

## Devices

Five real devices: PIC16F1937, PIC16F1829, PIC16F1619 (32 banks) and
PIC16F18877, PIC16F15276 (64 banks). Each has a definition in `devices/`
made from Microchip's device pack (`tools/devdb/gen_device.py`), from which
the simulator, the C header and the linker script are all generated.

Two imaginary ones, for testing without a real part's memory limits:

| Device | Banks | RAM | Flash | Made from |
|---|---|---|---|---|
| PIC16F1032 | 32 (5-bit `MOVLB`) | 2,480 bytes: general RAM in every bank that can have it | 32K words | PIC16F1937, without its LCD data registers, which lie where that RAM is |
| PIC16F1064 | 64 (6-bit `MOVLB`) | 4,080 bytes | 32K words | PIC16F18877, which is already as large as a 64-bank part can be; only the name differs |

These are the largest device each bank-select encoding allows. Everything
else about them (registers, configuration words, the peripherals the
simulator models) is the real device's. `tools/devdb/gen_imaginary.py`
makes their definitions. Select one like any other: `-mcpu=pic16f1032`,
`pic16f1-sim run --device pic16f1032`.

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

## Library functions the compiler knows

What the compiler may assume about a function with a standard name, such as
replacing `strlen("abc")` by 3 or a call by a different library function:

| Build | Known functions |
|---|---|
| `--libc`, or `-fhosted` | All of the standard library, as Clang does on any target |
| Without the C library (the default; compiled `-ffreestanding`) | `memcpy`, `memmove`, `memset` and `memcmp` only: the runtime library always has them |
| `-fno-builtin` | None. The runtime library is built this way, since it is what implements the four |

A fill or copy of up to four bytes with a constant size is expanded into
the stores themselves, whether it was written as a call, a structure
assignment or an initializer; larger ones are calls.

The other direction is turned off: a loop that fills or copies memory is
never replaced by a call to `memset` or `memcpy`. The loop, with its pointer
in an FSR, is faster than the general routine, and with the replacement on
the `matrix` and `strings` benchmarks came out both larger (by up to 81
words) and slower, past XC8 at `-O3` and `-Os`.

`scripts/test-builtins.sh` checks all of this on `tests/builtins/main.c`.

## Debugging

`-g` adds debug information (DWARF 5) and changes no code; any
optimization level can be debugged, and `-munoptimized` keeps every
variable in memory and every line separate. The debugger is LLDB, built
from the same tree with a PIC16F1 plugin, and it talks to the simulator
or to a probe over the gdb-remote protocol.
[The debug interface specification](spec/debug-interface.md) has the
conventions.

```sh
pic16f1-cc -g -O1 -mcpu=pic16f1937 -o main.hex main.c   # leaves main.elf too
pic16f1-sim gdb --port 3333 main.elf &                  # waits for a debugger
pic16f1-lldb main.elf -o "gdb-remote 3333"
(lldb) breakpoint set --name main
(lldb) continue
```

What works: breakpoints by function, line or address (any number, on the
simulator); stepping by line and by instruction, into, over and out; the
call stack; parameters and local variables in every frame; globals;
pointers; expressions (`p table[3] + v`, assignments); registers, among
them the compiler's `r0`–`r13`; memory; watchpoints on writes; the
program's UART output in the debugger's console and its exit code.

For VS Code, `examples/debug` is a ready project: its
`.vscode/launch.json` attaches the LLDB DAP extension
(`llvm-vs-code-extensions.lldb-dap`), running this toolchain's own
`pic16f1-lldb-dap`, to a simulator that `tasks.json` builds for and
starts. Press F5.

How it is done:

- **Addresses** are the ELF file's: program memory in bytes from 0, data
  memory at `0x800000`. Debug information has 4-byte addresses; LLDB adds
  the offset when it follows a 16-bit C pointer.
- **The call stack** is read from the hardware return stack, which the
  debug server shows as memory at `0x810000`.
- **Variables with fixed addresses** (those of static-frame functions) are
  described as locals at an address, so they show in every frame with
  nothing to unwind.
- **Stack frames** are found from the stack pointer cell `__sp`, not from
  FSR1, which is stale just after a call; for callers' frames the compiler
  emits call frame information (`.debug_frame`) that says how each function
  with a stack frame moves the stack. The cell is fixed at the start of RAM
  for this.
- **Unused versions** of a function compiled several ways get the address
  `0xFFFFFFFF` in the debug information, where the linker's default, 0, is
  the reset vector.
- **Whole-program builds at `-munoptimized`** are linked without the
  link-time optimizer too, which would otherwise fold away variables.

Debug information records the column of each instruction as well as its
line, so a stop or a breakpoint has a position within the line (VS Code
marks it there). `-gno-column-info` leaves columns out; the code is the
same.

Limits: a variable the optimizer keeps in a register is shown only where
the debug information can say where it is ("not available" elsewhere);
a variable in one of the registers a call preserves can show a later
function's value in a caller's frame;
`memory read` takes addresses in the debugger's address space, so a C
pointer's value needs `0x800000` added there. `scripts/test-debug.sh` runs
a session on two devices at three levels and checks that `-g` leaves the
code as it is.

### What the debugger shows

- **Registers** are named as in the data sheet: `PC`, `WREG`, `STATUS`,
  `BSR`, `PCLATH`, `FSR0`, `FSR1` and their halves `FSR0L`..., `STKPTR`.
  (`PC` is the address in bytes, twice the data sheet's word address,
  because source lines and symbols go by it. Names are matched without
  regard to case, and `w` still means `WREG`.) What the compiler uses as
  its registers `r0`-`r13`, `tmp0` and `tmp1` is listed as what it is, the
  common RAM at 0x70-0x7F, and the 16-bit pairs `rp0`-`rp6` after it.
- **Peripheral registers** are described by an SVD file per device
  (`toolchain/svd`, written by `tools/devdb/gen_svd.py` from the device
  database, the registers grouped by name into ports, timers and so on).
  A launch configuration's `svdPath` gives it to VS Code's Peripheral
  Viewer extension (`mcu-debug.peripheral-viewer`), whose view is
  XPeripherals. The addresses in it are the debugger's: data memory
  address + `0x800000`. Reading a register through the debugger can have
  the side effects reading it has for the program (`RCREG`, for one).
- **Disassembly** and **memory** are VS Code's own views ("Open
  Disassembly View" on a frame; the Memory View extension follows the
  session when `memory-view.trackDebuggers` names `lldb-dap`).

Views that other extensions add to the debug sidebar under similar names
(PlatformIO's Peripherals, Registers, Memory and Disassembly, for
instance) belong to those extensions' own debuggers and stay empty here.

### A project of your own

`scripts/package-toolchain.sh` assembles everything in one tree
(`build/toolchain`, or the directory it is given): the compiler, linker,
simulator, debugger, C library, device headers, linker scripts and
register descriptions. `pic16f1-new-project` in its `bin` starts a project
that uses it:

```sh
build/toolchain/bin/pic16f1-new-project --device pic16f1937 ~/projects/blink
cd ~/projects/blink && make        # or open the folder in VS Code and press F5
```

`--language c++`, `rust` or `zig` starts the same project in that language
(see [languages.md](languages.md) for what each needs installed): the
program, a Makefile that builds it to the same `build/NAME.hex` and
`.elf`, and the same VS Code setup, so that building and debugging on the
simulator and on the part work alike in all four.

The project has a small program for the F1 Evaluation Platform, a Makefile
(`make`, `make debug`, `make size`, `make list`), and a VS Code setup: a
build task, "Debug on the simulator" and "Debug on the part (PICkit 2
probe)" configurations, and include paths for the editor. The second
configuration builds with `-mdebugger`, has the probe's OpenOCD program
the part and waits for that to finish; OpenOCD ends with the debug
session, so each session starts from a freshly programmed and reset part.

### On the part itself

The same LLDB debugs a real part through any gdb-remote server that keeps
to the [debug interface](spec/debug-interface.md); the PICkit 2 probe
firmware's OpenOCD target does, and has run this debugger against a
PIC16LF1937 (its `pic16f1 load_debug` loads the ELF file: program,
configuration words, user ID and EEPROM data). Build with `-mdebugger` as
well as `-g`:

```sh
pic16f1-cc -g -O1 -mdebugger -mcpu=pic16f1937 -o main.hex main.c
```

It keeps the program out of the RAM the debugger's executive uses (the
last 16 bytes, `0x320`–`0x32F`, on the PIC16F1937; the linker says so if
the variables do not fit below it), defines `__DEBUG`, and makes it an
error to name the port, latch or direction bits of the debugger's clock
and data pins (`RB6` and `RB7` there). The driver refuses the option for a
device whose data does not say what a debugger uses.

The part has one breakpoint, and it stops the program after the
instruction at its address. LLDB allows for both:

- a stop is shown as its breakpoint, one instruction past the line's
  start; a breakpoint on a line that begins with a call stops at the start
  of the called function;
- a second breakpoint is refused, with a warning that says the part has
  none left;
- stepping over or out of a call borrows the breakpoint from the user's
  and gives it back afterwards, so a breakpoint inside a function being
  stepped over is not hit;
- steps land exactly: LLDB puts its own breakpoint an instruction early,
  and for stepping out on the function's return instruction. (A function
  with several returns, or one that ends by jumping to another, is left by
  a breakpoint at the return address instead, which runs one instruction
  of the caller; if that is a call, the step carries on to its return.)
- there are no watchpoints, and the pc cannot be changed.

`PIC16F1_SIM_SILICON=1 pic16f1-sim gdb ...` makes the simulator behave the
same way, which is how the test script checks all this without a part.

## Source written for XC8

Much of it compiles as it is:

- **`#pragma config NAME = VALUE, ...`**, before or after `<xc.h>`, in any
  source file. The compiler records the text and the linker makes the
  configuration words from the device's table of settings
  (`toolchain/lib/<device>.cfg`, from the device database); a setting not
  given keeps its erased value. A value is one of the setting's names or
  a number. `IDLOC0`-`IDLOC3` set the user ID words. A setting or value the
  device does not have, and one given twice differently, are errors that
  name the source line. `PIC_CONFIG_WORDS(word1, word2)` gives the words
  whole instead; a program uses one or the other.
- **`__EEPROM_DATA(a, b, c, d, e, f, g, h)`** puts eight bytes in the
  initial content of the data EEPROM, each use after the one before.
- **`void __interrupt() isr(void)`**, `__bit` (a `_Bool`), `__persistent`,
  `__section("name")`, `__pack`, `ei()`, `di()`, `__nop()`, and the delays
  below.

The HEX file carries user IDs, configuration words and EEPROM data where
programmers expect them (word addresses 0x8000, 0x8007 and 0xF000).
`scripts/test-xc8.sh` builds a program in these forms and compares the
words.

Not there: `__at(address)` for a variable at a fixed address; the `eeprom`
qualifier and `eeprom_read`/`eeprom_write`; the single-bit names XC8's
headers give port and flag bits (`RA0`, `GIE`: write `PORTAbits.RA0`,
`INTCONbits.GIE`); `__DEBUG`-style options given as pragmas.

## Delays

`<xc.h>` has XC8's `_delay(cycles)`, `__delay_ms(ms)` and `__delay_us(us)`;
the last two need `_XTAL_FREQ`, the oscillator frequency in Hz, defined
first. A delay is a call to a counting loop in the runtime library and
takes at least the time asked for and at most a dozen instruction cycles
more, at any optimization level (`scripts/test-delay.sh` measures it). The
argument must be a constant.

## Assembly syntax

Microchip mnemonics, not case sensitive: `addwf f, W|F`, `moviw ++FSR0`,
`moviw 3[FSR1]`, `addfsr FSR1, -4`. Address parts are written `lo8(x)`,
`hi8(x)`, `pm_lo8(x)`, `pm_hi8(x)`, `pm(x)`, `bank(x)`, `off(x)`, `page(x)`.

## Test results

All run on the simulator, except the one row that says otherwise.

| Suite | Result |
|---|---|
| Assembler against Microchip `pic-as` (`scripts/test-assembler.sh`) | 57 of 57 instruction forms identical |
| Benchmark programs (`tests/programs`, 15 programs, `-O0` `-O1` `-O2` `-O3` `-Os`) | 75 of 75 pass; the four default levels also with the SelectionDAG selector |
| Compiler tests (`tests/compiler`, 18 programs) | 72 of 72 builds pass, with either instruction selector; 14 of 14 with `-munoptimized` (four are too big for the device that way) |
| Regression tests (`tests/lit`) | 24 of 24 pass |
| Zig (`scripts/test-zig.sh`; see [languages.md](languages.md)) | 10 of 10 builds pass: the language in Zig's four modes, `pic.zig`, Zig with C |
| Rust (`scripts/test-rust.sh`; see [languages.md](languages.md)) | 7 of 7 builds by cargo pass: the language with `core`, the `pic16f1` crate |
| C++ (`scripts/test-cxx.sh`; see [languages.md](languages.md)) | 47 of 47 builds pass: the language, the standard library's headers, dynamic memory, the device library |
| One program on all seven devices (`scripts/test-devices.sh`) | 14 of 14 builds pass |
| GCC C torture, `execute` (1,701 tests) on the imaginary PIC16F1064, at `-O0` | 1,488 of the 1,511 that build pass (98.5%) |
| GCC C torture, `execute` at `-O1`, `-O2` and `-O3` | 1,491 or 1,492 of 1,514 (98.5%) |
| GCC C torture, `execute` at `-Os` | 1,493 of 1,515 (98.5%) |
| The same on the imaginary PIC16F1032 (32 banks) | the same results, test for test (before the last three were reclassified; not rerun since) |
| The same on the PIC16F1937 (496 bytes of RAM, 8K words) | 1,458 to 1,465 of the 1,489 to 1,497 that build (98%); the 34 tests that differ from the large device need its RAM or flash |
| The same on the PIC16F1937 (8K words, 496 bytes of RAM), before the fixes found on the large device | 1,414 to 1,418 of about 1,452; file by file (`-fno-lto`) 1,404 at `-O2`, 1,408 at `-Os` |
| Variable-length arrays and `alloca` (`scripts/test-vla.sh`) | pass on 2 devices at 6 levels, whole program and file by file |
| `setjmp` and `longjmp` (`scripts/test-setjmp.sh`) | pass on 2 devices at 6 levels, whole program and file by file |
| Debugging with LLDB on the simulator (`scripts/test-debug.sh`) | pass on 2 devices at `-munoptimized`, `-O1`, `-Os` |
| Callers' frames after every instruction of a recursive function, and the call stack through an interrupt handler | pass on 2 devices; 248 steps, none wrong |
| The same against the simulator behaving as the part's debug hardware (one breakpoint, stopping late) | pass at `-munoptimized` and `-O1` |
| Delays (`scripts/test-delay.sh`) | pass on 2 devices at 3 levels: never short, at most 12 cycles long |
| Hardware stack count (`scripts/test-stack.sh`) | pass at 3 levels: equal to the measured depth, warns only past 16 |
| XC8 source forms (`scripts/test-xc8.sh`) | pass: configuration, user ID and EEPROM words at 4 settings; wrong settings refused |
| Switches (`scripts/test-switch.sh`) | pass on 2 devices: 5 levels, both code models, both selectors, tables on and off |
| Arguments and results in W (`scripts/test-wcall.sh`) | pass on 2 devices: 5 levels, both code models, both selectors, the choice on and off |
| On a PIC16LF1937, through the PICkit 2 probe's OpenOCD, by a scripted LLDB session | program, configuration words, user ID and EEPROM load and read back; breakpoints by line and by function, five steps over (two of them over calls), a step out, the call stack and variables all as on the simulator; OpenOCD ends with the session |
| Floating point (`scripts/test-float.sh`) | pass: bit for bit against a host's arithmetic (47 million checks by default, 282 million run), and on 2 devices at 3 levels |
| Static frames that share memory (`scripts/test-overlay.sh`) | pass on 2 devices at 5 settings: no frame overwritten, 30 bytes where there were 44 |
| Memory builtins without the C library (`scripts/test-builtins.sh`) | pass at `-O1`, `-O2`, `-Os` |
| Frame selection at link time, and calls through constant function pointers (`scripts/test-frames.sh`) | right frames and right output at `-O1`, `-O2`, `-Os`; no indirect call left in `tests/inject` |
| picolibc's tests on the PIC16F18877 (141 programs, `-Os`) | 103 pass within 2 billion cycles, 3 exceed that, 34 do not build |
| picolibc's tests on the PIC16F1937 | 74 pass, 66 do not build (39 are too big for 8K words) |

About 195 torture tests do not build even on the large device: they need
`malloc`, `printf`, a 32-bit `int` or nested functions.

The 25 that build and do not pass at one level or another have each been examined; none is wrong
code from the compiler:

| Reason | Tests |
|---|---|
| The test assumes a 32-bit `int` | `divmod-1`, `20071211-1`, `pr53645` |
| Undefined with a 16-bit `int` (a shift by its width, an index that does not fit, an uninitialized argument), so that the compiler makes `main` empty. These counted as passing while the empty `main` happened to be laid out just ahead of `exit` | `20180131-1`, `pr79286`, `vector-div-neg-pow2` (at `-O1`) |
| GCC behaviour Clang has on no target: the storage-order and `optimize` attributes, arithmetic at the width of a bit-field, inlining a function that takes a label's address | `20230630-2`, `20230630-4`, `alias-1`, `pr79043`, `bitfld-3`, `bitfld-5`, `pr32244-1`, `pr34971`, `990208-1` |
| `-O0` here optimizes without inlining, and the test expects `__builtin_constant_p` to fold after inlining | `bcp-1` (at `-O0` only) |
| Type-based aliasing across a call in a whole-program build; the test says itself that it may not be conforming, and passes with `-fno-lto` | `20000603-1` (at `-O0` only) |
| Needs the C library, or more than 200 million cycles; they pass with `--libc` and a 2-billion-cycle limit | `printf-1`, `920501-6`, `arith-rand-ll` |
| Recursion deeper than the 16-level return stack | `20000412-2`, `pr58209` |
| A variable changed between `setjmp` and `longjmp` without being `volatile`; a comparison with a new `alloca` that Clang folds | `pr60003`, `pr64242` |
| `__builtin_frame_address` gives 0: there is no chain of frames | `frame-address` |

`tools/torture.py` gives tests that size themselves to the stack
(`STACK_SIZE`) a quarter of the device's RAM.

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
| arith16 | 99 | 334 | 252 | 763 | 686 | 50,489 | 3,276 | 147,863 |
| arith32 | 88 | 377 | 352 | 885 | 576 | 25,109 | 56,092 | 97,697 |
| bitops | 95 | 162 | 299 | 346 | 570 | 29,360 | 75,194 | 120,595 |
| crc16 | 53 | 131 | 216 | 279 | 226 | 6,684 | 11,245 | 16,918 |
| crc32 | 48 | 124 | 187 | 274 | 176 | 1,294 | 1,838 | 2,851 |
| crc8tab | 55 | 381 | 431 | 461 | 246 | 3,829 | 4,512 | 22,451 |
| fib | 59 | 140 | 124 | 311 | 286 | 1,012 | 642 | 2,952 |
| fsm | 57 | 310 | 547 | 596 | 266 | 1,997 | 3,026 | 4,585 |
| hello | 48 | 48 | 64 | 83 | 176 | 176 | 219 | 488 |
| matrix | 53 | 258 | 573 | 585 | 226 | 17,826 | 31,921 | 44,196 |
| sieve | 54 | 141 | 270 | 287 | 236 | 16,542 | 32,668 | 33,838 |
| sort | 75 | 266 | 449 | 529 | 446 | 12,493 | 24,213 | 28,704 |
| strings | 74 | 238 | 506 | 532 | 436 | 3,661 | 8,781 | 11,015 |
| structs | 53 | 170 | 321 | 405 | 226 | 914 | 3,008 | 3,292 |
| timer_isr | 85 | 145 | 246 | 265 | 5,468 | 5,645 | 5,910 | 6,181 |
| **Total** | 996 | 3,225 | 4,837 | 6,601 | | | | |

LLVM's code is 4.9 times the size of the smallest programs and
1.50 times the size of the ones that compute at run time; in
cycles, 17.8 and 1.33 times (geometric means).

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
  `pic16f1-cc --libc`. `-mprintf=` chooses how much `printf` can do, and
  what a program that uses it costs: `minimal` (no field widths, `long` or
  floating point) about 2,600 words, `integer` (the default) 3,600, `float`
  9,800, which is more than the 8K-word parts hold; `-mscanf=` likewise.
  The library's float conversion is the smaller, inexact one (correct to
  the digits a `float` has). `malloc` works; the heap sits between the data
  and the stack.
- Floating point is single precision (`double` is the same type), IEEE
  with subnormals, round to nearest. The arithmetic, comparisons and
  conversions to and from 32-bit integers are the runtime library's own
  (`toolchain/runtime/float.c`), written for an 8-bit machine: about 550
  words for addition and subtraction, 430 for multiplication, 390 for
  division, and 280 for the rounding they share; a program that uses all
  of them and the conversions is about 2,800 words. (LLVM's generic
  routines, used before, were 1,500 to 2,000 words each, and division took
  thirteen times as long.) Results are the same bit for bit:
  `scripts/test-float.sh` compares them with a host's arithmetic over
  hundreds of millions of operands. Conversions to and from 64-bit
  integers are still the generic ones.
- No variable-length arrays or `alloca` in an interrupt handler; the compiler
  says so.
- `byval` struct arguments from front ends other than Clang are rejected.
- Against XC8 at the same level, every one of the fifteen benchmark
  programs is no larger and no slower, at each of XC8's five levels:

  | Level | Words, LLVM / XC8 | Closest program in size | Cycles (geometric mean) | Closest program in speed |
  |---|---|---|---|---|
  | `-O0` | 5,270 / 8,542 (0.62) | `crc8tab` 0.84 | 0.36 | `timer_isr` 0.89 |
  | `-O1` | 5,022 / 7,818 (0.64) | `strings` 0.86 | 0.39 | `timer_isr` 0.89 |
  | `-O2` | 5,009 / 7,697 (0.65) | `crc8tab` 0.86 | 0.40 | `timer_isr` 0.90 |
  | `-O3` | 4,916 / 6,790 (0.72) | `strings` 0.99 (5 words) | 0.48 | `sieve` 0.97 |
  | `-Os` | 4,807 / 6,601 (0.73) | `matrix` 0.98 (14 words) | 0.48 | `sieve` 0.96 |

  That holds because of what `pic16f1-cc` makes of each level; see "What
  the levels mean". `-Oz` has no XC8 counterpart; against XC8 `-Os` it is
  smaller in total (4,715 words) but one program is larger (`matrix`,
  by 9 words). `-munoptimized` is about 1.6 times XC8 `-O0` in size.
  These are whole-program builds, the default; compiled file by file
  (`-fno-lto`) the `-Os` set is 4,952 words.
- Outlining at `-Os` and the lower inlining threshold at every level trade hardware
  stack levels for space; see "Shared sequences" and "Inlining".
- In a function with a stack frame, only one pointer at a time can be in
  an FSR (FSR1 is the stack pointer), so a loop over two arrays copies
  each pointer to FSR0 in turn, and each call costs five instructions to
  get the stack pointer back. An array index that is not a loop
  counter is added to the array's address, 16 bits, for each access.
  See what is not done yet under "Static frames".
- The count of hardware stack levels cannot bound a recursive program, and
  takes a call through a pointer to reach every function whose address is
  taken. The simulator stops on an overflow.
- On silicon, programs have been loaded and debugged on a PIC16LF1937
  (see "Test results"); the test suites themselves run on the simulator,
  and timing has not been compared with the part.
