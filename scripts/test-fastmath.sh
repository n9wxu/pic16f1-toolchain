#!/bin/sh
# Check the fast arithmetic library (-mmath=fast: toolchain/runtime/
# fastmath.S, and float.c built with -DFLOAT_FAST):
#   1. its floating point compiled for this machine, against the machine's
#      own IEEE arithmetic, bit for bit, as scripts/test-float.sh does for
#      the standard one;
#   2. on the simulator, programs of operands and expected results that
#      tests/fastmath/gen.py makes (multiplication and division of 16 and
#      32 bits, signed and not, and float arithmetic and conversions), on
#      two devices, at two levels, in both code models. Each program is
#      first run with the standard library, which checks the program;
#   3. the float test of scripts/test-float.sh with the fast library;
#   4. that the fast library takes at most two thirds of the time, on a
#      program that multiplies and divides.
# ROUNDS sets the number of random rounds of the first part, SEEDS the
# programs of the second.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

${HOST_CC:-cc} -O2 -ffp-contract=off -DFLOAT_HOST_TEST -DFLOAT_FAST -o "$tmp/host" \
    "$root/tests/float/host.c" "$root/toolchain/runtime/float.c"
"$tmp/host" "${ROUNDS:-1000000}" > "$tmp/out" || { echo "FAIL against the host's arithmetic:"; cat "$tmp/out"; exit 1; }
echo "pass  host: $(tail -1 "$tmp/out")"

n=0
run() {  # run <device> <options...>: build $tmp/t.c, run it, expect 0x5A; prints the cycles
    device=$1; shift
    "$cc" "$@" -mcpu="$device" -o "$tmp/t.elf" "$tmp/t.c"
    code=0; "$sim" run --device "$device" --max-cycles 2000000000 --json "$tmp/t.elf" > "$tmp/run.json" || code=$?
    [ "$code" = 90 ] || { echo "FAIL $device $*: kind $code"; exit 1; }
    n=$((n + 1))
}
for seed in ${SEEDS:-1 2 3 4 5 6 7 8}; do
    python3 "$root/tests/fastmath/gen.py" "$seed" 40 > "$tmp/t.c"
    run pic16f1937 -Os -mmath=small
    run pic16f1937 -Os -mmath=fast
    run pic16f1937 -O1 -mmath=fast
    run pic16f18877 -Os -mmath=fast
done
# (A program small enough for one page: the library built for that.)
python3 "$root/tests/fastmath/gen.py" 9 6 int > "$tmp/t.c"
run pic16f1937 -Os -mcmodel=small -mmath=fast
echo "pass  $n programs of operands and results from tests/fastmath/gen.py"

# (Not -munoptimized: that program, as written, then no longer fits the part.)
for level in -O1 -O2 -Os; do
    "$cc" $level -mmath=fast -mcpu=pic16f1937 -o "$tmp/f.elf" "$root/tests/float/target.c"
    code=0; "$sim" run "$tmp/f.elf" > /dev/null || code=$?
    [ "$code" = 0 ] || { echo "FAIL tests/float/target.c $level -mmath=fast: mask $code"; exit 1; }
done
echo "pass  tests/float/target.c with the fast library (3 levels)"

cycles() { sed 's/.*"cycles":\([0-9]*\).*/\1/' "$tmp/run.json"; }
cp "$root/tests/fastmath/speed.c" "$tmp/t.c"
run pic16f1937 -Os -mmath=small; small=$(cycles)
run pic16f1937 -Os -mmath=fast; fast=$(cycles)
[ $((fast * 3)) -lt $((small * 2)) ] || { echo "FAIL the fast library took $fast cycles, the standard one $small"; exit 1; }
echo "pass  tests/fastmath/speed.c: $small cycles with the standard library, $fast with the fast one"
