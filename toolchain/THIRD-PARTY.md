# What a packaged toolchain contains of others' work

| | License | Where |
|---|---|---|
| LLVM, Clang, LLD, LLDB (23.1.6, with this project's PIC16F1 changes) | Apache License 2.0 with LLVM exceptions | `bin/`, `lib/` |
| libc++ headers and the parts of compiler-rt in the runtime library | Apache License 2.0 with LLVM exceptions | `include/c++`, `lib/pic16f1` |
| picolibc | BSD-style licenses, per file (see its `COPYING.picolibc`) | `picolibc/` |
| CPython, from python-build-standalone | Python Software Foundation License; the build's own notices are in `libexec/python` | `libexec/python` |
| libxml2 (Linux and Windows, linked into the debugger) | MIT | `bin/`, `lib/` |
| Device data derived from Microchip device family packs | Apache License 2.0 (as Microchip publishes the packs) | `include/pic16f1`, `lib/pic16f1`, `share/pic16f1/svd` |

The toolchain itself is under the Apache License 2.0 with LLVM exceptions
(`LICENSE`).
