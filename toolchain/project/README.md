# @NAME@

A C project for the @DEVICE_UPPER@, built with the PIC16F1 LLVM toolchain
at `@TOOLCHAIN@`.

## In VS Code

Open this folder. The **LLDB DAP** extension
(`llvm-vs-code-extensions.lldb-dap`) is needed for debugging.

| | |
|---|---|
| Build | Terminal > Run Build Task (Cmd-Shift-B). Errors and warnings appear in the Problems panel |
| Debug on the simulator | Run > Start Debugging (F5), with "Debug on the simulator" chosen in the Run and Debug view. It builds, starts the simulator and stops at `main` |
| Debug on the part | Choose "Debug on the part (PICkit 2 probe)" and press F5. It builds for the in-circuit debugger, programs the part through the probe's OpenOCD and stops at `main` |

Breakpoints, stepping, the call stack, variables, watch expressions and
registers work in both. On the part there is one breakpoint, it stops one
instruction past the start of its line, there are no data breakpoints, and
pins RB6 and RB7 belong to the debugger. A part that has executed `SLEEP`
cannot be halted.

The device, the optimization level, the toolchain's location and the
probe's are in `.vscode/settings.json`.

## From a terminal

```sh
make            # build/@NAME@.hex and .elf
make debug      # LLDB on the simulator
make size
make list       # disassembly
```

`build/@NAME@.hex` is an ordinary Intel HEX file for any programmer.

## Writing for this toolchain

- `#include <xc.h>` declares the device's registers (`LATE`,
  `LATDbits.LATD1`, ...).
- Configuration words are set with `#pragma config`, as for XC8.
- `__delay_ms(n)` and `__delay_us(n)` need `_XTAL_FREQ` defined first.
- An interrupt handler is `void __attribute__((interrupt)) isr(void)`.
- `pic16f1-cc --libc` adds the C library (`printf`, `string.h`, `math.h`);
  add `--libc` to `CFLAGS` in the Makefile. `-mprintf=minimal` makes
  `printf` smaller; `-mprintf=float` gives it floating point, at a size
  that needs a part with more than 8K words.
- If the probe is not found when debugging on the part, its firmware may
  have gone back to the PICkit 2's bootloader (it does if nothing talks to
  it within 20 seconds of power-up): `host/pk2boot.py run` in the probe's
  repository restarts it.
- The debugger stops at a position within a line (VS Code marks it in the
  line) because the compiler records columns. `CFLAGS = -gno-column-info`
  in the Makefile makes it go by lines only.
- The toolchain's guide is `docs/toolchain.md` in its repository.
