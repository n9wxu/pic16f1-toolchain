#!/bin/sh
# Build tests/devices/blink for every device in the database and run each
# on the simulator configured as that device.
#
# Usage: scripts/test-devices.sh [out-dir]
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/out/devices}
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
[ -x "$sim" ] || sim=$(dirname "$cc")/pic16f1-sim
status=0
mkdir -p "$out"
for json in "$root"/devices/*.json; do
    dev=$(basename "$json" .json)
    for level in O0 Os; do
        hex="$out/blink-$dev-$level.hex"
        if ! "$cc" -mcpu="$dev" -"$level" -o "$hex" "$root/tests/devices/blink/main.c" 2> "$out/$dev-$level.log"; then
            echo "FAIL  $dev -$level: does not build"; cat "$out/$dev-$level.log"; status=1; continue
        fi
        result=$("$sim" run --device "$dev" --json "$hex")
        halt=$(printf '%s' "$result" | sed 's/.*"halt":"\([^"]*\)".*/\1/')
        code=$(printf '%s' "$result" | sed 's/.*"exit_code":\([0-9]*\).*/\1/')
        words=$(printf '%s' "$result" | sed 's/.*"program_words":\([0-9]*\).*/\1/')
        if [ "$halt" = sleep ] && [ "$code" = 0 ]; then
            echo "pass  $dev -$level  ($words words)"
        else
            echo "FAIL  $dev -$level: halt=$halt exit=$code"; status=1
        fi
    done
done
exit $status
