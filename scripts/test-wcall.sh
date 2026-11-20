#!/bin/sh
# Check arguments and results in W, which the compiler chooses function by
# function: the test program checks every value, compiled with the choice
# on and off, as a whole program and file by file, through both instruction
# selectors. Also that the choice is made where it should be (and makes the
# program smaller), that -g does not change the code, and that a function
# called through a pointer is left alone.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
src=$root/tests/wcall/wcall.c
off="-mllvm -pic16f1-w-conventions=0 -Wl,-mllvm,-pic16f1-w-conventions=0"
report="-mllvm -pic16f1-w-report -Wl,-mllvm,-pic16f1-w-report"
words() { "$sim" disasm --device "$1" "$2" | grep -c "^ *[0-9A-Fa-f]*:"; }

n=0
for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O1 -O2 -O3 -Os; do
        for model in small medium; do
            for more in "" "$off" "-fno-lto" "-fno-lto -mllvm -global-isel=0"; do
                # shellcheck disable=SC2086
                "$cc" $level -mcmodel=$model $more -mcpu=$device -o "$tmp/w.hex" "$src"
                code=0; "$sim" run --device $device "$tmp/w.elf" > /dev/null || code=$?
                [ "$code" = 90 ] || { echo "FAIL $device $level $model $more: exit code $code"; exit 1; }
                n=$((n + 1))
            done
        done
        [ "$level" = -munoptimized ] && continue
        # shellcheck disable=SC2086
        "$cc" $level $report -mcpu=$device -o "$tmp/on.hex" "$src" 2> "$tmp/report"
        # shellcheck disable=SC2086
        "$cc" $level $off -mcpu=$device -o "$tmp/off.hex" "$src"
        # shellcheck disable=SC2086
        "$cc" -g $level -mcpu=$device -o "$tmp/g.hex" "$src"
        cmp -s "$tmp/on.hex" "$tmp/g.hex" || { echo "FAIL $device $level: -g changed the code"; exit 1; }
        for f in classify get; do
            grep -q "pic16f1: $f: result in W" "$tmp/report" || { echo "FAIL $device $level: $f does not return in W"; cat "$tmp/report"; exit 1; }
        done
        grep -q "pic16f1: put: the argument of R0 in W" "$tmp/report" || { echo "FAIL $device $level: put does not take its argument in W"; cat "$tmp/report"; exit 1; }
        if grep -q "pic16f1: \(plus7\|minus7\|main\):" "$tmp/report"; then echo "FAIL $device $level: a function called through a pointer was changed"; exit 1; fi
        a=$(wc -c < "$tmp/on.hex"); b=$(wc -c < "$tmp/off.hex")
        [ "$a" -lt "$b" ] || { echo "FAIL $device $level: not smaller ($a, $b without)"; exit 1; }
    done
    echo "pass  $device"
done
echo "$n builds"
