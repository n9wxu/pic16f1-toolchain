# PIC16F1 open toolchain

An open compiler, simulator and programming toolchain for the Microchip
enhanced mid-range core (PIC16F1xxx). The full plan is in
[docs/plan.md](docs/plan.md). The first target is the PIC16F1937; the
PIC16F1829, PIC16F1619, PIC16F18877 and PIC16F15276 are also supported in
the simulator and compiler. Two imaginary devices, PIC16F1032 and
PIC16F1064, are the largest 32- and 64-bank parts there could be, for
testing without a real part's memory limits.

## Status

The test suites run on the simulator. Programs have also been programmed
into and debugged on a real PIC16LF1937, through a PICkit 2 running the
probe firmware of a companion project and its OpenOCD.
[docs/status.md](docs/status.md) tracks each exit criterion of the plan's
gates G0 to G2.

| Stage | Content | State |
|---|---|---|
| 1 | Repository, CI, releases | done |
| 2 | Test programs and XC8 reference binaries | done |
| 3 | Simulator and benchmark runner | done |
| 4 | LLVM backend, C working | done: 98.7% of the GCC C torture tests that build pass, picolibc builds and its tests pass |
| 5 | Optimization | every benchmark no larger and no slower than XC8 at each of its levels; in total 0.73 of XC8's size at `-Os` |
| 6 | Debugging | LLDB with a PIC16F1 plugin, on the simulator and on the part; VS Code projects (`pic16f1-new-project`) |
| 7 | Other languages | C++, Rust, Zig and Forth ([docs/languages.md](docs/languages.md)); a catalog of others ([docs/language-catalog.md](docs/language-catalog.md)) |
| 8 | Releases | For macOS, Linux and Windows: the toolchain as an archive, and as a VS Code extension that installs it and builds and debugs projects ([docs/install.md](docs/install.md)) |

## Layout

| Path | Content |
|---|---|
| `docs/` | Project plan and the design of what is built so far |
| `devices/` | Device database, generated from Microchip device family packs |
| `tools/devdb/` | Device database generator |
| `sim/` | Simulator (`pic16f1-sim`), written in Rust |
| `tests/programs/` | Test and benchmark programs, each with its expected output |
| `tests/compiler/` | Further tests for the new compiler: recursion, variadic calls, structs, 64-bit math, large frames, interrupt context |
| `tests/assembler/` | Instruction encodings checked against Microchip `pic-as`, and a round-trip test |
| `tests/lit/` | Regression tests for the assembler, linker and code generator (LLVM lit) |
| `tests/libc/` | A `printf` program using picolibc |
| `tests/cxx/` | C++: the language, the standard library, the device library (`scripts/test-cxx.sh`) |
| `tests/rust/` | Rust: the language with `core`, the `pic16f1` crate (`scripts/test-rust.sh`) |
| `tests/zig/` | Zig: the language, `pic.zig` (`scripts/test-zig.sh`) |
| `tests/forth/` | Forth: the words of `pic16f1-forth` (`scripts/test-forth.sh`) |
| `tests/hardware/` | Programs that check themselves on a real part, run by `tools/hwtest.py` |
| `tests/debug/`, `tests/float/`, `tests/overlay/`, `tests/stack/`, `tests/switch/`, `tests/wcall/`, `tests/xc8/`, `tests/vla/`, `tests/setjmp/`, `tests/frames/`, `tests/builtins/`, `tests/inject/` | One feature each, run by the `scripts/test-*.sh` of the same name |
| `tests/devices/` | A program that must build and run on every device in the database |
| `tests/torture/` | Harness for third-party conformance tests, run by `tools/torture.py` |
| `tests/common/` | Harness shared by the programs (UART output, exit code) |
| `tests/host/` | Stand-in headers for building the programs natively |
| `reference/xc8-v4.00/` | The programs built with MPLAB XC8 4.00 at `-O0` to `-Os` |
| `tools/bench.py` | Runs binaries on the simulator and reports size and cycles |
| `tools/memguard.py` | Runs a command and kills any process under it that passes a memory limit |
| `llvm/` | LLVM, Clang and LLD changes: a patch plus the new files |
| `toolchain/` | Compiler driver, runtime library, device headers, linker scripts, register descriptions (SVD), debugger wrappers and the template for new projects |
| `examples/debug/` | A program with a VS Code setup for debugging on the simulator |
| `pcb/` | KiCad project of a simple programmer and debugger |
| `vscode/` | The VS Code extension, and its tests |
| `scripts/` | Build helpers |

## Installing a release

Install the `.vsix` file for your system in VS Code (Extensions view >
... > Install from VSIX): it carries the whole toolchain. Then
**PIC16F1: New Project...**, F7 to build, F5 to debug on the simulator.
Without VS Code, unpack the archive and put its `bin` on the PATH.
[docs/install.md](docs/install.md) has the details. The rest of this page
is about working on the toolchain itself.

## Quick start

```sh
cargo build --release --manifest-path sim/Cargo.toml
sim/target/release/pic16f1-sim run reference/xc8-v4.00/hello/Os/hello.hex
python3 tools/bench.py          # every reference binary, as a table
```

## Building the compiler

```sh
scripts/llvm-setup.sh     # fetch the pinned LLVM release and apply the changes
scripts/build-llvm.sh     # build clang, lld and the runtime into build/toolchain
scripts/build-picolibc.sh # optional: the C library (printf, string.h, malloc)
scripts/build-cxx.sh      # optional: the C++ standard library's headers
build/toolchain/bin/pic16f1-cc -mcpu=pic16f1937 -Os -I tests/common \
    -o hello.hex tests/programs/hello/main.c tests/common/bench.c
build/toolchain/bin/pic16f1-sim run hello.hex
```

To start a program of your own, with a Makefile and a VS Code setup for
building and for debugging on the simulator and on the part:

```sh
scripts/package-toolchain.sh          # everything into build/toolchain
build/toolchain/bin/pic16f1-new-project --device pic16f1937 ~/projects/blink
```

`scripts/build-llvm-tests.sh` builds every test program at each optimization
level, and `tools/bench.py reference/xc8-v4.00 build/out/llvm-pic16f1`
compares the result with XC8. The design is described in
[docs/toolchain.md](docs/toolchain.md).

## Conventions

**Program exit.** A test program writes its exit code to `LATD` and executes
`SLEEP`. Output goes out the EUSART. The same binary runs on the simulator
and on a PIC16F1937, with no simulator-only register.

**Expected output.** Each `expected.txt` comes from building the program with
the native compiler (`scripts/host-run.sh`), not from the simulator. A
reference binary passes when the simulator reproduces that output. The
programs also check published values where one exists, such as the CRC
check values.

**Device facts.** Register addresses, bit positions and memory sizes come
from `devices/*.json`, generated from the Microchip device pack by
`tools/devdb/gen_device.py`. Each file records the pack version and the hash
of its source. Only the twelve core registers that the instruction set
itself defines are fixed in simulator code.

**Simulator timing** is counted in instruction cycles. Interrupt entry
latency and peripheral timing have not yet been checked against silicon.

## Rebuilding the reference binaries

XC8 is not part of this repository. Install MPLAB XC8 4.00 or later and the
`PIC12-16F1xxx_DFP` device pack anywhere, then:

```sh
XC8_HOME=/Applications/microchip/xc8/v4.00 \
XC8_DFP=$HOME/tools/microchip-packs/PIC12-16F1xxx_DFP/1.10.270 \
scripts/build-xc8-refs.sh
```

## Releases

Pushing a tag `vX.Y.Z` builds, tests and packages the toolchain for Linux
and Windows and publishes them as a GitHub release;
`scripts/release-macos.sh --upload vX.Y.Z` on a Mac adds the macOS
packages. [docs/install.md](docs/install.md) describes what a release
holds and how it is made and tested.

## License

Apache License 2.0 with LLVM exceptions, the same as LLVM. The device
database is derived from Microchip device family packs, which Microchip
publishes under Apache 2.0. picolibc and the GCC torture tests are fetched
at build or test time under their own licenses and are not part of this
repository.
