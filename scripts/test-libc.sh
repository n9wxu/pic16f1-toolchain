#!/bin/sh
# Check the C library (picolibc) built by scripts/build-picolibc.sh:
#   1. a printf program gives the expected output on the PIC16F1937;
#   2. picolibc's own stdio and general tests pass on the PIC16F18877, which
#      has the flash to hold them. The other test directories pass too but
#      take billions of simulated cycles; see docs/toolchain.md.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$cc" --libc -Os -o "$tmp/hello.hex" "$root/tests/libc/hello/main.c"
"$sim" run "$tmp/hello.hex" | cmp - "$root/tests/libc/hello/expected.txt"
echo "printf program: output matches"

for dir in test/test-stdio test; do
    printf '%-16s' "$dir"
    PIC16F1_CC=$cc python3 "$root/tools/torture.py" "$root/build/picolibc/$dir" \
        --libc --device pic16f18877 --levels Os --sim "$sim" --max-cycles 300000000 | tail -1
done
