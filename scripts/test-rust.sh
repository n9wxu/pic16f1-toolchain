#!/bin/sh
# Check Rust: the language with core (tests/rust/features) and the device
# library pic16f1::hal (tests/rust/hal), built by cargo with the toolchain's
# linker wrapper and run on the simulator. Needs cargo and the pinned
# nightly with the source of core (rustup toolchain install
# nightly-2024-11-19 --component rust-src); skipped without them.
# PIC16F1_RUST_TOOLCHAIN names another nightly to try.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
sim=$root/toolchain/bin/pic16f1-sim
nightly=${PIC16F1_RUST_TOOLCHAIN:-nightly-2024-11-19}
if ! command -v cargo > /dev/null 2>&1 || ! cargo "+$nightly" --version > /dev/null 2>&1; then
    echo "skip  Rust: no cargo, or no toolchain $nightly"
    exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

n=0
build() {  # build <crate> <link arguments...>: the ELF file is $elf
    crate=$1; shift
    flags="-C linker-plugin-lto"
    for a in "$@"; do flags="$flags -C link-arg=$a"; done
    (cd "$root/tests/rust/$crate" &&
        CARGO_TARGET_DIR="$tmp/target" \
        CARGO_TARGET_PIC16F1_LINKER="$root/toolchain/bin/pic16f1-rust-ld" \
        CARGO_TARGET_PIC16F1_RUSTFLAGS="$flags" \
        cargo "+$nightly" build --quiet --release -Zbuild-std=core \
            --target "$root/toolchain/rust/pic16f1.json") ||
        { echo "FAIL $crate $*: does not build"; exit 1; }
    elf=$tmp/target/pic16f1/release/$crate.elf
    n=$((n + 1))
}
run() {  # run <device> <label>: expect 0x5A
    code=0; "$sim" run --device "$1" "$elf" > "$tmp/out" || code=$?
    [ "$code" = 90 ] || { echo "FAIL $2: exit code $code"; exit 1; }
}

for level in -Os -O1 -O2; do
    build features -mcpu=pic16f1937 $level
    run pic16f1937 "features $level"
    build hal -mcpu=pic16f1937 $level
    run pic16f1937 "hal $level"
    grep -q "pins 12345 A5" "$tmp/out" || { echo "FAIL hal $level: serial output"; cat "$tmp/out"; exit 1; }
done
# The linker made the configuration words from config!: the internal
# oscillator and no watchdog in the first, nothing changed in the second.
words=$("$root/build/llvm-build/bin/llvm-objdump" -s -j .config "$elf" | tail -1 | awk '{print $2}')
[ "$words" = e43fff3f ] || { echo "FAIL hal: configuration words $words"; exit 1; }
# The HEX file and what the linker said are beside the program cargo names.
ls "$tmp"/target/pic16f1/release/deps/hal-*.hex "$tmp"/target/pic16f1/release/deps/hal-*.link.txt > /dev/null ||
    { echo "FAIL hal: no HEX file or link report"; exit 1; }
# For the in-circuit debugger, with debug information.
build hardware -mcpu=pic16f1937 -g -mdebugger
"$root/build/llvm-build/bin/llvm-readelf" -S "$elf" | grep -q debug_info || { echo "FAIL hardware: no debug information"; exit 1; }
echo "pass  Rust: the language, pic16f1::hal, and a build for the debugger"
echo "$n builds"
