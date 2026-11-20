#!/bin/sh
# Check the floating-point routines (toolchain/runtime/float.c):
#   1. compiled for this machine, against its own IEEE arithmetic, bit for
#      bit, over special values and a few hundred million random operands;
#   2. on the simulator, against results the compiler computes at compile
#      time, at three levels on two devices.
# ROUNDS sets the number of random rounds of the first part.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

${HOST_CC:-cc} -O2 -ffp-contract=off -DFLOAT_HOST_TEST -o "$tmp/host" \
    "$root/tests/float/host.c" "$root/toolchain/runtime/float.c"
"$tmp/host" "${ROUNDS:-1000000}" > "$tmp/out" || { echo "FAIL against the host's arithmetic:"; cat "$tmp/out"; exit 1; }
echo "pass  host: $(tail -1 "$tmp/out")"

for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O1 -Os; do
        "$cc" $level -mcpu=$device -o "$tmp/t.elf" "$root/tests/float/target.c"
        code=0; "$sim" run --device $device "$tmp/t.elf" > /dev/null || code=$?
        [ "$code" = 0 ] || { echo "FAIL $device $level: mask $code"; exit 1; }
    done
    echo "pass  $device  (3 levels)"
done
