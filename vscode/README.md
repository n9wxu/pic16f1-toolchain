# PIC16F1 Toolchain

C, C++, Rust, Zig and Forth for Microchip's PIC16F1 microcontrollers (the
enhanced mid-range core: PIC16F1937, PIC16F18877 and their like), with a
simulator and a debugger. Installing this extension installs all of it:
the compiler (Clang and LLVM with a code generator written for this
processor), the linker, the C and C++ libraries, the simulator and LLDB.
Nothing else is needed for C and C++.

## Start

1. **PIC16F1: New Project...** in the Command Palette: choose the language
   and the device, and where the project goes.
2. **F7** builds. Mistakes are in the Problems view; the status bar shows
   the flash the program takes.
3. **F5** debugs on the simulator: the program is built, the simulator
   started, and the debugger stops at `main`.

The first time, the toolchain is unpacked into `.pic16f1/toolchain` in
your home directory (about 400 MB, once). The terminals of a window have
its programs on their PATH: `pic16f1-cc`, `pic16f1-build`, `pic16f1-sim`,
`pic16f1-lldb`.

## What it adds

| | |
|---|---|
| **PIC16F1: New Project...** | A project in C, C++, Rust or Zig for a device, with a Makefile for use outside the editor |
| **PIC16F1: Build** (F7), **Clean** | Also as tasks (Terminal > Run Build Task) |
| **PIC16F1: Debug on the Simulator** | The same as F5 with "Debug on the simulator" |
| **PIC16F1: Debug on the Part (Probe)** | Programs the part through OpenOCD and debugs it there |
| **PIC16F1: Select Device...** | Or click the device in the status bar |
| **PIC16F1: Select Optimization...** | `-O1`, `-O2`, `-O3`, `-Os`, or `-munoptimized` for stepping through every line |
| **PIC16F1: Show Disassembly** | Of the program as built; also a click on the size in the status bar |
| **PIC16F1: About the Toolchain** | Its version and where it is |

Debugging has breakpoints, stepping, the call stack (from the hardware
return stack), variables, watch expressions, registers and memory. With
the *Peripheral Viewer* extension (`mcu-debug.peripheral-viewer`) the
device's registers are shown by name and field: the toolchain has the
description of each device and the debug session names it.

A debug configuration in `launch.json` is two lines:

```json
{ "type": "pic16f1", "request": "launch", "name": "Debug on the simulator", "target": "simulator" }
```

`"target": "probe"` debugs on the part. That needs an OpenOCD that knows
your probe and the PIC16F1 (the setting `pic16f1.openocd` names its
directory; `pic16f1.openocdInterface` and `pic16f1.openocdTarget` its
configuration files). On the part there is one breakpoint, and two pins
belong to the debugger.

## Settings

| | |
|---|---|
| `pic16f1.device` | The device of the project (`pic16f1937`). Empty: what its Makefile says |
| `pic16f1.optimization` | How the program is optimized. Empty: what its Makefile says |
| `pic16f1.toolchain` | The `bin` directory of another toolchain to use (one you built) |
| `pic16f1.openocd`, `pic16f1.openocdInterface`, `pic16f1.openocdTarget`, `pic16f1.vdd` | For debugging on the part |

## The other languages

C and C++ need nothing more. **Rust** needs `rustup` (the project names
the nightly compiler it was tested with, which rustup fetches). **Zig**
needs Zig 0.15 on the PATH. **Forth** is compiled by `pic16f1-forth` from
a terminal; a folder with a `.fth` file builds and debugs as a project.

## Without the editor

Everything the extension does is done by the toolchain's programs, which
work from any terminal once `.pic16f1/toolchain/bin` is on the PATH:

```sh
pic16f1-new-project --language c --device pic16f1937 blink
pic16f1-build blink            # or: cd blink && make
pic16f1-build debug blink      # LLDB on the simulator
```
