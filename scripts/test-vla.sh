#!/bin/sh
# Check variable-length arrays and alloca (tests/vla/main.c): arrays with
# calls after them, a callee with an array of its own, arrays given back at
# the end of a block, recursion, alloca, and arguments passed on the stack
# below an array. The program exits with 0, or the number of the check
# that failed.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O0 -O1 -O2 -O3 -Os; do
        for mode in -flto -fno-lto; do
            "$cc" $level $mode -mcpu=$device -fhosted -w \
                -isystem "$root/tests/torture/include" -o "$tmp/a.hex" \
                "$root/tests/vla/main.c" "$root/tests/torture/harness.c"
            "$sim" run --device $device --json "$tmp/a.hex" > "$tmp/run.json"
            grep -q '"exit_code":0,"halt":"sleep"' "$tmp/run.json" || {
                echo "FAIL $device $level $mode: $(cut -c1-120 "$tmp/run.json")"; exit 1; }
        done
    done
    echo "pass  $device  (6 levels, whole program and file by file)"
done
