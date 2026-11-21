#!/bin/sh
# Check Forth: tests/forth/features.fth, compiled by pic16f1-forth and run
# on the simulator on two devices and in both code models; that a wrong
# result is noticed; what a small program costs; Forth calling C; the
# linker's count of hardware stack levels; and a build for the debugger.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
forth=$root/toolchain/bin/pic16f1-forth
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
words() { "$sim" disasm --device "$1" "$2" | grep -c "^ *[0-9A-Fa-f]*:"; }

n=0
for device in pic16f1937 pic16f1032; do
    for model in "" -mcmodel=medium; do
        "$forth" -mcpu=$device $model -o "$tmp/f.hex" "$root/tests/forth/features.fth"
        code=0; "$sim" run --device $device "$tmp/f.elf" > "$tmp/out" || code=$?
        [ "$code" = 90 ] || { echo "FAIL features $device $model: exit code $code"; exit 1; }
        grep -q "forth 7 -123 60000 A5" "$tmp/out" && grep -q "ok!" "$tmp/out" ||
            { echo "FAIL features $device $model: serial output"; cat "$tmp/out"; exit 1; }
        n=$((n + 1))
    done
done
# A check that does not hold is reported by its number.
sed 's/10 sum-to 55 =/10 sum-to 56 =/' "$root/tests/forth/features.fth" > "$tmp/bad.fth"
"$forth" -mcpu=pic16f1937 -o "$tmp/bad.hex" "$tmp/bad.fth"
code=0; "$sim" run --device pic16f1937 "$tmp/bad.elf" > /dev/null || code=$?
[ "$code" = 50 ] || { echo "FAIL: a wrong result gave exit code $code, not 50"; exit 1; }
# Mistakes in the source are refused, with the line.
printf ': MAIN 1 IF 2 ;\n' > "$tmp/open.fth"
if "$forth" -mcpu=pic16f1937 -o "$tmp/open.hex" "$tmp/open.fth" 2> "$tmp/err"; then echo "FAIL: an unclosed IF compiled"; exit 1; fi
grep -q "open.fth:1: IF is not closed" "$tmp/err" || { echo "FAIL: message for an unclosed IF"; cat "$tmp/err"; exit 1; }
printf ': MAIN nothing ;\n' > "$tmp/unknown.fth"
if "$forth" -mcpu=pic16f1937 -o "$tmp/unknown.hex" "$tmp/unknown.fth" 2> "$tmp/err"; then echo "FAIL: an unknown word compiled"; exit 1; fi
grep -q "nothing is not a word or a number" "$tmp/err" || { echo "FAIL: message for an unknown word"; exit 1; }

# A register written and a bit set are single instructions, and only the
# kernel routines a program uses are in it.
cat > "$tmp/tiny.fth" <<'F'
: MAIN  0 TRISE C!  BEGIN LATE 0 BIT-TOGGLE AGAIN ;
F
"$forth" -mcpu=pic16f1937 -o "$tmp/tiny.hex" "$tmp/tiny.fth"
w=$(words pic16f1937 "$tmp/tiny.elf")
[ "$w" -le 20 ] || { echo "FAIL: the smallest program is $w words"; exit 1; }

# Forth calling C, and the linker counting the levels of the two together.
cat > "$tmp/helper.c" <<'C'
unsigned c_scale(unsigned x, unsigned k) { return x * k + 1; }
unsigned char seen;
void c_note(unsigned x) { seen = (unsigned char)x; }
unsigned c_seen(void) { return seen; }
C
cat > "$tmp/mixed.fth" <<'F'
C-FUNCTION: scale c_scale 2 1
C-FUNCTION: note  c_note  1 0
C-FUNCTION: seen  c_seen  0 1
: MAIN  7 11 12 scale ( 7 133 )  note ( 7 )  seen + ( 140 )  50 -  0 TRISD C!  LATD C!  SLEEP ;
F
"$forth" -mcpu=pic16f1937 -mstack-report -o "$tmp/mixed.hex" "$tmp/mixed.fth" "$tmp/helper.c" > "$tmp/report"
code=0; "$sim" run --device pic16f1937 "$tmp/mixed.elf" > /dev/null || code=$?
[ "$code" = 90 ] || { echo "FAIL Forth calling C: exit code $code"; exit 1; }
grep -q "hardware stack: [0-9]* of 16 levels" "$tmp/report" || { echo "FAIL: no stack count"; cat "$tmp/report"; exit 1; }

"$forth" -mcpu=pic16f1937 -g -mdebugger -o "$tmp/hw.hex" "$root/tests/forth/hardware.fth"
"$root/build/llvm-build/bin/llvm-nm" "$tmp/hw.elf" | grep -q " T hw_done" || { echo "FAIL: EXPORT made no symbol"; exit 1; }
n=$((n + 6))
echo "pass  Forth: the words on 2 devices in both code models, mistakes refused, $w words for the smallest program, with C"
echo "$n builds"
