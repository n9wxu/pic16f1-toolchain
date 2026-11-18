# Stack, frames, interrupts and startup

Status: draft v1, awaiting approval. This is the plan's "software stack"
model, used for every function. Static frames (plan section 3.3) will be
added as a second model and will need their own specification.

## Data stack

- `FSR1` is the stack pointer. The stack grows toward lower addresses and
  lives in linear RAM (`0x2000` up), so it can cross bank boundaries.
- `FSR1` points at the lowest byte in use. A push is `MOVWI --FSR1`.
- Return addresses are not on this stack. They are on the 16-level hardware
  stack, which the compiler does not yet check for depth.
- The stack starts at the end of RAM (`__stack_top`) and grows down toward
  `.bss`. Nothing checks for collision.

## Frames

On entry a function lowers `FSR1` by its whole frame size once, and raises it
by the same amount before returning. From low to high address a frame holds:

1. Outgoing stack arguments for the calls the function makes.
2. Spill slots and local variables.
3. Saved copies of the `R8`-`R13` registers the function changes.

Incoming stack arguments are just above the frame, in the caller's.

`MOVIW` and `MOVWI` reach offsets -32 to 31 from `FSR1`. For a slot further
away the code moves `FSR1` with `ADDFSR`, makes the access, and moves it
back. That is safe only because of the interrupt rule below.

There is no frame pointer. Variable-length arrays and `alloca` are not
supported.

## Interrupt handler

A function marked `__attribute__((interrupt))`:

- takes no arguments and returns nothing;
- is placed in section `.isr`, which the linker script puts at word 4, the
  interrupt vector. A program has at most one;
- loads `FSR1` with `__isr_stack_top` on entry and so runs on its own stack.
  `RETFIE` restores the interrupted `FSR1` from its shadow register. This is
  what allows ordinary code to move `FSR1` temporarily;
- saves and restores every one of `R0`-`R13`, `TMP0` and `TMP1` that it or
  anything it calls may change. If it calls a function, that is all of
  `R0`-`R7`, `TMP0` and `TMP1`;
- relies on the hardware to save and restore W, STATUS, BSR, FSR0, FSR1 and
  PCLATH;
- returns with `RETFIE`.

The handler's stack is `__isr_stack_size` bytes (default 32, set with
`-Wl,--defsym=__isr_stack_size=N`), allocated only if the program has a
handler. Interrupts do not nest.

A function called from both the main program and the handler is safe as it
stands, since both keep their data on their own stacks.

## Startup

The reset vector (word 0) jumps to `_start`, which:

1. copies `.data` from flash to RAM;
2. clears `.bss`;
3. sets `FSR1` to `__stack_top`;
4. calls `main`;
5. calls `exit` with `main`'s result. The default `exit` executes `SLEEP`
   forever; a program may define its own.

Startup contains no `MOVLB`, so one runtime library serves 32- and 64-bank
devices.

## Open

- Hardware-stack depth analysis and the response to overflow.
- Detecting collision between the stack and `.bss`.
- The default size of the interrupt stack.
