# Installing and releasing

A release is, for each system, two files that hold the same thing:

| | |
|---|---|
| `pic16f1-toolchain-VERSION-SYSTEM.vsix` | The VS Code extension, with the whole toolchain inside it. Installing it installs everything |
| `pic16f1-toolchain-VERSION-SYSTEM.tar.gz` (`.zip` for Windows) | The toolchain alone, for use from a terminal or another editor |

`SYSTEM` is `macos-arm64` (Apple silicon), `macos-x86_64` (Intel),
`linux-x86_64` or `windows-x86_64`.

What is in it: Clang and LLD with the PIC16F1 code generator, the LLVM
tools, the simulator, LLDB and its debug adapter, the device headers,
linker scripts and register descriptions, the runtime library, picolibc,
the C++ standard library's headers, the Rust and Zig device libraries, the
Forth compiler, the project templates, and a Python for the drivers
(`pic16f1-cc` and the others are Python scripts). It needs nothing else
installed for C, C++ and Forth; Rust needs `rustup` and Zig needs Zig 0.15
(see [languages.md](languages.md)).

## With VS Code

1. Extensions view > `...` > **Install from VSIX...**, and choose the
   `.vsix` for your system. (Or `code --install-extension FILE.vsix`.)
2. The first time, the extension unpacks the toolchain into
   `.pic16f1/toolchain` in your home directory: about 400 MB, a minute,
   once. A later version of the extension replaces it.
3. **PIC16F1: New Project...** in the Command Palette; then F7 builds and
   F5 debugs on the simulator.

The extension's own page (its README, in `vscode/`) lists its commands
and settings. The toolchain is put where it is, and not left inside the
extension, so that its path stays the same from version to version: a
project's Makefile and editor settings name it.

Nothing is signed. Installed this way that does not matter: VS Code
unpacks the extension and the extension unpacks the toolchain, so neither
macOS nor Windows sees a downloaded program.

## Without VS Code

Unpack the archive anywhere and put its `bin` on the PATH:

```sh
tar -xzf pic16f1-toolchain-0.2.0-linux-x86_64.tar.gz
export PATH=$PWD/pic16f1-toolchain/bin:$PATH
pic16f1-new-project --device pic16f1937 blink
pic16f1-build blink          # or: cd blink && make
pic16f1-build debug blink    # LLDB on the simulator
```

On Windows, unpack the `.zip` and add `pic16f1-toolchain\bin` to the PATH;
the commands are the same (`pic16f1-build` does what `make` does, which
Windows does not have). On macOS an archive that a browser downloaded is
quarantined, and its programs are refused until
`xattr -dr com.apple.quarantine pic16f1-toolchain` has been run on the
unpacked directory; the `.vsix` avoids that.

## What each system's package was tested with

| | |
|---|---|
| Every package | `tools/test_package.sh`: with nothing else on the PATH, start and build a C and a C++ project, run a benchmark on the simulator, a program with the C and C++ libraries, a Forth program, and a debug session |
| Every extension | `vscode/test/run.js`: in a real VS Code, the installed `.vsix` unpacks its toolchain, builds a project, shows a mistake in the Problems view, debugs on the simulator to `main`, reads the stack and a register, steps, and leaves no simulator behind |
| Linux | Also the whole compiler test suite, which runs there (the Toolchain workflow) |
| Windows | Also every benchmark, built on Windows and run on the simulator next to XC8's |
| macOS | Also the extension with a project in each of C, C++, Rust and Zig, and debugging on a PIC16LF1937 through the probe |

The files for the PIC16F1 in a package (libraries, headers, templates)
are the same files on every system: the Linux build makes and tests them,
and the others take them from there.

The programs need: macOS 11 or later; Linux with glibc 2.35 or later
(Ubuntu 22.04, Debian 12, Fedora 36 and their successors); Windows 10 or
later, 64-bit.

## Making a release

1. Set `VERSION`, commit, and push a tag `vVERSION`.
2. The **Release** workflow (`.github/workflows/release.yml`) runs the
   **Toolchain** workflow, which builds, tests and packages Linux and
   Windows, and publishes the GitHub release with their files.
3. On a Mac, at the tag: `scripts/release-macos.sh --upload vVERSION`
   builds both macOS packages around the release's own PIC16F1 files,
   tests them and adds them to the release. (Not in the workflow, because
   a hosted macOS runner costs ten times a Linux one and the LLVM build is
   long.)

| | |
|---|---|
| `tools/package_release.py` | Makes the tree and archive for one system from a build |
| `scripts/package-vsix.sh` | Wraps an archive and the extension into a `.vsix` |
| `scripts/fetch-python.sh` | The Python a package carries, pinned by checksum (python-build-standalone) |
| `scripts/build-libxml2.sh` | libxml2 for the debugger on Linux and Windows, linked in |
| `toolchain/windows/launcher.c` | The `NAME.exe` that runs each Python driver on Windows |
| `tools/test_package.sh`, `vscode/test/` | The tests above |

## MPLAB X

There is no MPLAB X plug-in. MPLAB X takes a compiler through a
"toolchain" plug-in written against its own interfaces, one per compiler;
none was written here, and the integration is VS Code's instead (which is
also where Microchip's own tools are going). What MPLAB X and MPLAB IPE
can use directly is the output: `build/NAME.hex` is an ordinary Intel HEX
file with the configuration words, user ID and EEPROM data in it, and
loads as a prebuilt image for any Microchip programmer.
