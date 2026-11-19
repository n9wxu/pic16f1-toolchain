#!/bin/sh
# Check setjmp and longjmp (tests/setjmp/main.c): a jump from several calls
# down, the caller's registers afterwards, variable-length arrays given
# back, the builtin forms, and a hundred jumps in a row.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O0 -O1 -O2 -O3 -Os; do
        for mode in -flto -fno-lto; do
            "$cc" $level $mode -mcpu=$device -w -o "$tmp/a.hex" \
                "$root/tests/setjmp/main.c" "$root/tests/torture/harness.c"
            "$sim" run --device $device --json "$tmp/a.hex" > "$tmp/run.json"
            grep -q '"exit_code":0,"halt":"sleep"' "$tmp/run.json" || {
                echo "FAIL $device $level $mode: $(cut -c1-120 "$tmp/run.json")"; exit 1; }
        done
    done
    echo "pass  $device  (6 levels, whole program and file by file)"
done
