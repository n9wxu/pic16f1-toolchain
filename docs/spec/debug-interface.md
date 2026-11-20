# Debug interface

Status: draft v1, awaiting approval. It describes what the simulator's
debug server (`pic16f1-sim gdb`) serves and what the compiler's debug
information and the LLDB plugin assume; `scripts/test-debug.sh` runs LLDB
against the simulator on these terms. A debug server for silicon (the
PICkit 2 probe's OpenOCD target) follows the same conventions so that one
debugger setup works with both.

## Address space

The debugger works in the address space of the ELF file
([object format](object-format.md)). Debug information uses 4-byte
addresses in it.

| Addresses | Content |
|---|---|
| `0x000000` up | Program memory: two bytes per 14-bit word, low byte first |
| `0x010000` up | User IDs and configuration words (word address `0x8000` up), likewise |
| `0x800000`–`0x801FFF` | Data memory by banked address: bank × `0x80` + offset |
| `0x802000`–`0x807FFF` | Data memory, linear view |
| `0x808000`–`0x80FFFF` | Program memory as an FSR reads it: one byte per word, its low 8 bits |
| `0x810000` up | Hardware return stack: two bytes per level, low byte first, each the word address the core holds. Level 0 is the first entry used |

A C data pointer `p` is the address `0x800000 + p` here, whichever of the
three data views it points into. A C function pointer is a word address:
twice its value is the address here. The debugger makes both conversions
when it follows a pointer variable or evaluates an expression; it treats
the target as having 2-byte pointers, and a server that answers `qHostInfo`
says `ptrsize:2`.

## Registers

In this order in a `g` packet, and by these names (lower case) in the
target description:

| Register | Bits | Content |
|---|---|---|
| `pc` | 32 | Address of the next instruction, in bytes: twice the word address |
| `w`, `status`, `bsr`, `pclath` | 8 each | The core registers |
| `fsr0`, `fsr1` | 16 each | The pointer registers, as the program holds them (not offset) |
| `stkptr` | 8 | Hardware stack pointer: the level of the top entry, `0x1F` when the stack is empty |
| `r0`–`r13`, `tmp0`, `tmp1` | 8 each | The compiler's registers: the bytes at `0x70`–`0x7F` of data memory |

The debugger composes the compiler's 16-bit pairs `rp0`–`rp6` from
`r0`–`r13` itself; a server does not serve them.

DWARF register numbers, fixed by the compiler: `r0`–`r13` are 0–13, `tmp0`
14, `tmp1` 15, `rp0`–`rp6` 16–22, `fsr1` 23, `fsr0` 24, `w` 25, `status`
26, `fsr0l` 27, `fsr0h` 28, `fsr1l` 29, `fsr1h` 30. The debugger assigns
32–35 to `pc`, `stkptr`, `bsr` and `pclath` for unwinding; the compiler
never uses those.

## Packets

A server handles `qSupported`, `qXfer:features:read` for `target.xml`, `?`,
`g`/`G`, `p`/`P`, `m`/`M`, `c`, `s`, `vCont?` and `vCont` with `c` and `s`,
`Z`/`z`, `H` (accepted and ignored), `qC`, `qfThreadInfo`/`qsThreadInfo`
with the single thread 1, and `k` or `D`. Stop replies are
`T05thread:1;` (a server without threads may leave the thread out and
answer `qfThreadInfo` with `l`). `QStartNoAckMode`, `qHostInfo` (triple
`pic16f1-unknown-unknown`, little-endian, pointer size 2),
`qMemoryRegionInfo` and `O` packets for program output are optional.

The simulator also reports the program's end (`SLEEP`) as `W` with its exit
code, sends EUSART output as `O` packets, takes any number of breakpoints
as `Z0` or `Z1`, and stops on a write to data memory watched with `Z2`,
reporting `watch:` with the address the watchpoint was set at.

## Breakpoints on silicon

The debug hardware of the parts tried so far (PIC16(L)F193x) has one
breakpoint, and it stops the program *after* the instruction at its address
has run. A server for such a part:

- takes its hardware breakpoint as `Z1`, and as `Z0` too or answers `Z0`
  with an empty reply; a request for more breakpoints than it has gets an
  `E` reply;
- reports the true `pc` at every stop: after a breakpoint, the address of
  the instruction that runs next (anywhere, if the one at the breakpoint
  was a jump or a call);
- reports the stop as `T05`, like any other.

The debugger does the rest:

- A program that was running and stops with `T05` at an address that has no
  breakpoint, while exactly one breakpoint is set in the target, stopped
  for that breakpoint. The debugger shows it so, an instruction past the
  place asked for, and from then on takes the target's breakpoints to be of
  this kind.
- When it needs a breakpoint of its own to step over or out of a call and
  the user's holds the only one, its own takes that one's place and the
  user's goes back when the step is done. A breakpoint inside the function
  stepped over is therefore not hit.
- To stop exactly where it means to, it puts its own breakpoints an
  instruction early: on the instruction before the end of a line, and, to
  step out of a function, on the function's return instruction when it has
  just one and no other way out. Otherwise it puts the breakpoint at the
  return address and the caller's first instruction runs too; if that is a
  call, the step goes on to its return.

The simulator behaves this way, for testing the debugger, when the
environment variable `PIC16F1_SIM_SILICON` is set: one breakpoint that stops
late, no watchpoints, a `pc` that cannot be written, and the replies of a
server without threads.

A program built for such a debugger keeps clear of what the debugger uses
on the part: the device data names the RAM of its executive (`0x320`–`0x32F`
on the PIC16F1937) and the two port pins of its clock and data lines, and
`pic16f1-cc -mdebugger` ends the program's RAM below the first and makes
naming the second an error.

## The call stack

Return addresses are only on the hardware stack. The frame running at
stack level *n* returns to the word address in entry *n*, which is at
`0x810000 + 2n`; its caller runs at level *n* − 1. With `stkptr` at `0x1F`
there is no caller. The debugger unwinds from these alone, so a call stack
needs nothing from the compiler. A debugger should read only the entries
up to `stkptr`: on silicon each costs time.

A frame is identified by its level. Debuggers order frames by a call frame
address that falls as the stack deepens, so the one used here, by the
compiler's `.debug_frame` and by the debugger for code without it, is
`0x810000 + 2 × ((30 − stkptr) mod 32)`: highest for the empty stack.
Nothing is stored there.

## Variables

- A variable of a function with a static frame has a fixed address. It is
  described as a local variable whose location is that address.
- A variable in a stack frame is at an offset from the frame base, which is
  what FSR1 holds while the function's own code runs, with `0x800000`
  added.

## Finding a stack frame

FSR1 itself is not dependable just after a call: a function takes its
stack pointer back from the cell `__sp` only when it next needs it. And a
caller's FSR1 is saved nowhere. So frames are found through the *stack
level* of a frame, the value the cell has while that frame runs:

| Frame | Its stack level |
|---|---|
| The innermost | The content of the cell |
| A caller's | What unwinding gives for `fsr1` |

The two are told apart by `bsr`, which unwinding sets to `0xFF` in every
caller's frame, a value the register cannot hold.

- A function with a stack frame that makes calls puts FSR1 in the cell
  before its first call, and whatever it calls leaves the cell as it found
  it. Its frame base is its stack level, and its caller's stack level is
  that plus the size of its frame.
- A function with a stack frame that makes no calls has its frame base in
  FSR1; its caller's stack level is the cell.
- A function with variable-length arrays or `alloca` has its frame base in
  `rp6`; its caller's stack level is that plus the size of its frame.
- A function with a static frame, and the runtime library, leave the level
  as it is.

The compiler says which of these a function is in `.debug_frame`: a common
entry with the rules for the return address, `stkptr`, `bsr` and the
static-frame rule for `fsr1`, and one instruction per function with a
stack frame that replaces the `fsr1` rule after its prologue. The debugger
uses the common rules for code that has no such information.

The cell is the first two bytes of RAM, `0x802000` (the linker scripts
place it there), so that these rules, which cannot hold a relocation, can
name it.

Inside a prologue and an epilogue the rules follow the frame as it is made
and undone: the common rule until the cell is written, a rule that goes by
FSR1 itself while it is (two bytes in four instructions; only the innermost
frame can be there), and in the epilogue FSR1 again once it is the caller's
level. Only a function's own variables are not to be had while its frame
is half made.

An interrupt handler that makes calls gives them a stack of its own and
keeps the interrupted code's level in the two bytes after the cell
(`0x802002`, also fixed by the linker scripts) meanwhile; its rule says so,
and the call stack reads through the handler into what it interrupted. A
handler that makes no calls leaves the cell alone. (What a function without
calls was doing with FSR1 when it was interrupted is not described: its
stack-frame variables are wrong in that one frame.)

**Open:** the registers a call preserves (`r8`-`r13`, and `rp6`, the frame
base of a function with variable-length arrays) are taken in a caller's
frame to be what they are now. That is wrong while a function further down
has saved one and is using it; where it saved it is not described.
