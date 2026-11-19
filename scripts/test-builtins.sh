#!/bin/sh
# Check the memory builtins of a build without the C library
# (tests/builtins/main.c): small constant-size calls are expanded in place,
# loops stay loops, and -fno-builtin leaves the calls alone.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
src=$root/tests/builtins/main.c
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

calls() { grep -cE "(call|goto)[[:space:]]+mem(cpy|set)" "$1" || true; }

for level in -O1 -O2 -Os; do
    "$cc" $level -fno-lto -S -o "$tmp/on.s" "$src"
    "$cc" $level -fno-lto -fno-builtin -S -o "$tmp/off.s" "$src"
    on=$(calls "$tmp/on.s"); off=$(calls "$tmp/off.s")
    [ "$on" = 0 ] || { echo "FAIL $level: $on calls to memcpy or memset left"; exit 1; }
    [ "$off" != 0 ] || { echo "FAIL $level: -fno-builtin left no call"; exit 1; }
    "$cc" $level -o "$tmp/a.hex" "$src"
    "$root/sim/target/release/pic16f1-sim" run --json "$tmp/a.hex" > "$tmp/run.json"
    grep -q '"halt":"sleep"' "$tmp/run.json" || { echo "FAIL $level: did not finish"; exit 1; }
    echo "pass  $level  (no calls; $off with -fno-builtin)"
done
