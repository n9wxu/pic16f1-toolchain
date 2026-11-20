#!/bin/sh
# Check <xc.h>'s delays on the simulator, which counts instruction cycles:
# _delay(n) must take at least n cycles and at most 12 more, at every level,
# and __delay_ms must do its arithmetic at compile time (no floating point
# in the program).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cycles() {  # cycles <device> <level> <statement>: cycles the program takes
    printf '#include <xc.h>\n#define _XTAL_FREQ 4000000UL\nvoid main(void) { %s LATD = 1; SLEEP(); }\n' "$3" > "$tmp/d.c"
    "$cc" -mcpu="$1" $2 -o "$tmp/d.elf" "$tmp/d.c"
    "$sim" run --device "$1" --json "$tmp/d.elf" | sed -n 's/.*"cycles": *\([0-9]*\).*/\1/p'
}

for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O1 -Os; do
        base=$(cycles $device "$level" "")
        for n in 17 100 1000 65432 1000000; do
            got=$(($(cycles $device "$level" "_delay($n);") - base))
            if [ "$got" -lt "$n" ] || [ "$got" -gt $((n + 12)) ]; then
                echo "FAIL $device $level: _delay($n) took $got cycles"; exit 1
            fi
        done
        # 4 MHz is 1000 cycles a millisecond.
        got=$(($(cycles $device "$level" "__delay_ms(25);") - base))
        if [ "$got" -lt 25000 ] || [ "$got" -gt 25012 ]; then
            echo "FAIL $device $level: __delay_ms(25) took $got cycles"; exit 1
        fi
        if "$root/build/llvm-build/bin/llvm-nm" "$tmp/d.elf" | grep -q "sf[23]\|fix"; then
            echo "FAIL $device $level: __delay_ms brought in floating point"; exit 1
        fi
    done
    echo "pass  $device  (3 levels)"
done
