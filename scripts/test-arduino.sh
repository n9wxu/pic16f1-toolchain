#!/bin/sh
# Check the Arduino core (toolchain/arduino): sketches built by
# pic16f1-build as the Arduino IDE would build them (the .ino made into
# C++, functions declared ahead, the core as a library, the libraries a
# sketch includes) and run on the simulator. tests/arduino/Board is the
# part that needs the F1 Evaluation Platform; it is built here and run by
# tools/hwtest.py.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/toolchain/bin/pic16f1-build
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp -R "$root/tests/arduino/." "$tmp/"
find "$tmp" -name build -prune -exec rm -rf {} +

echo 0 > "$tmp/count"
words() {  # words <sketch> <options...>: build, print the flash words
    sketch=$1; shift
    "$build" build "$@" "$tmp/$sketch" > "$tmp/log" 2>&1 || { cat "$tmp/log"; echo "FAIL $sketch $*: does not build"; exit 1; }
    echo $(($(cat "$tmp/count") + 1)) > "$tmp/count"
    sed -n 's/ words of flash used//p' "$tmp/log" | tail -1
}

# The smallest sketch, at each oscillator setting; a constant pin is one
# instruction, so Blink has no table of pins in it.
for hz in 500000 1000000 4000000 16000000 32000000; do
    blink=$(words Blink --f-cpu $hz)
done
[ "$blink" -lt 300 ] || { echo "FAIL Blink is $blink words"; exit 1; }
echo "pass  Blink: $blink words at 32 MHz"

# The language and Serial, checked by the sketch itself and by what it prints.
for opt in -O1 -Os; do
    # (Not slower: millis then steps by more than the test allows.)
    for hz in 4000000 16000000 32000000; do
        basics=$(words Basics --opt $opt --f-cpu $hz)
        code=0; "$sim" run "$tmp/Basics/build/Basics.elf" > "$tmp/out" || code=$?
        [ "$code" = 90 ] || { cat "$tmp/out"; echo "FAIL Basics $opt $hz: exit code $code"; exit 1; }
        cmp -s "$tmp/out" "$root/tests/arduino/Basics/expected.txt" ||
            { echo "FAIL Basics $opt $hz: it printed"; cat "$tmp/out"; exit 1; }
    done
done
echo "pass  Basics: printing, pins, millis and delay, the helpers ($basics words)"

# Wire, SPI, EEPROM, analogRead, analogWrite and attachInterrupt compile
# and link, also for the debugger.
board=$(words Board)
words Board --debugger > /dev/null
echo "pass  Board builds: $board words"

# A new project is a sketch that builds and runs.
"$root/toolchain/bin/pic16f1-new-project" --language arduino "$tmp/Fresh" > /dev/null
[ -f "$tmp/Fresh/Fresh.ino" ] || { echo "FAIL new project: no Fresh.ino"; exit 1; }
(cd "$tmp/Fresh" && make > "$tmp/log" 2>&1) || { cat "$tmp/log"; echo "FAIL new project: make"; exit 1; }
echo $(($(cat "$tmp/count") + 1)) > "$tmp/count"
"$sim" run --max-cycles 6000000 "$tmp/Fresh/build/Fresh.elf" > "$tmp/out" 2>/dev/null || true
grep -q "^Fresh" "$tmp/out" && grep -Eq "^(99[0-9]|10[0-9][0-9])" "$tmp/out" ||
    { echo "FAIL new project: it printed"; cat "$tmp/out"; exit 1; }
echo "pass  a new Arduino project builds with make and prints millis"
echo "$(cat "$tmp/count") builds"
