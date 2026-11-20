#!/bin/sh
# Check switch statements, with and without tables of jumps: the test
# program compares every switch with what it should give for every value.
# Run in both code models (an entry of the table is one word in one page,
# two otherwise), through both instruction selectors, and with tables
# turned off; and check that a dense switch does become a table.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
off="-mllvm -pic16f1-no-jump-tables -Wl,-mllvm,-pic16f1-no-jump-tables"

n=0
for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O1 -O2 -O3 -Os; do
        for model in small medium; do
            for more in "" "$off" "-fno-lto" "-fno-lto -mllvm -global-isel=0"; do
                # shellcheck disable=SC2086
                "$cc" $level -mcmodel=$model $more -mcpu=$device -o "$tmp/s.elf" "$root/tests/switch/switch.c"
                code=0; "$sim" run --device $device "$tmp/s.elf" > /dev/null || code=$?
                [ "$code" = 90 ] || { echo "FAIL $device $level $model $more: exit code $code"; exit 1; }
                tables=$("$sim" disasm --device $device "$tmp/s.elf" | grep -c "BRW" || true)
                if [ "$more" = "$off" ] && [ "$tables" != 0 ]; then echo "FAIL $device $level $model: a table with tables off"; exit 1; fi
                if [ -z "$more" ] && [ "$tables" = 0 ]; then echo "FAIL $device $level $model: no table"; exit 1; fi
                n=$((n + 1))
            done
        done
    done
    echo "pass  $device"
done
echo "$n builds"
