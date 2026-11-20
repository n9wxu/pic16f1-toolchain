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

# The kinds of printf the driver chooses among (-mprintf=): what each makes
# of integers and of floating point.
for kind in minimal integer float; do
    case $kind in
    minimal) want="n=42 big=123456789 x=%f e=%g" ;;
    integer) want="n=42 big=123456789 x=*float* e=*float*" ;;
    float) want="n=42 big=123456789 x=3.142 e=3141.59" ;;
    esac
    "$cc" -Os -w -mprintf=$kind -mcpu=pic16f18877 -o "$tmp/p.elf" "$root/tests/libc/printf/main.c"
    got=$("$sim" run --device pic16f18877 "$tmp/p.elf")
    [ "$got" = "$want" ] || { echo "FAIL -mprintf=$kind: '$got'"; exit 1; }
    echo "-mprintf=$kind: $("$sim" size --device pic16f18877 "$tmp/p.elf") words, output as expected"
done

for dir in test/test-stdio test; do
    printf '%-16s' "$dir"
    PIC16F1_CC=$cc python3 "$root/tools/torture.py" "$root/build/picolibc/$dir" \
        --libc --device pic16f18877 --levels Os --sim "$sim" --max-cycles 300000000 | tail -1
done
