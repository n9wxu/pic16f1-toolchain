#!/bin/sh
# Check that static frames share memory only where they may: the test
# program's functions each keep a pattern in their variables across the
# calls they make, with an interrupt handler running meanwhile, and report
# any that changed. Also that sharing happens: the frames take less memory
# than they do with the overlay turned off.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
size="$root/build/llvm-build/bin/llvm-size -A"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
frames() { $size "$1" | awk '$1 == ".noinit" { print $2 }'; }

for device in pic16f1937 pic16f1032; do
    for level in -munoptimized -O1 -O2 -Os "-O1 -fno-lto"; do
        # shellcheck disable=SC2086
        "$cc" $level -mcpu=$device -o "$tmp/o.elf" "$root/tests/overlay/overlay.c"
        code=0; "$sim" run --device $device "$tmp/o.elf" > /dev/null || code=$?
        [ "$code" = 90 ] || { echo "FAIL $device $level: exit code $code (a mask of the frames that were overwritten, if not 64)"; exit 1; }
        # shellcheck disable=SC2086
        "$cc" $level -mcpu=$device -Wl,-z,pic16f1-no-frame-overlay -o "$tmp/n.elf" "$root/tests/overlay/overlay.c"
        code=0; "$sim" run --device $device "$tmp/n.elf" > /dev/null || code=$?
        [ "$code" = 90 ] || { echo "FAIL $device $level without the overlay: exit code $code"; exit 1; }
        with=$(frames "$tmp/o.elf"); without=$(frames "$tmp/n.elf")
        [ "$with" -le "$without" ] || { echo "FAIL $device $level: $with bytes with the overlay, $without without"; exit 1; }
        if [ "$level" = -O1 ] && [ "$with" -ge "$without" ]; then echo "FAIL $device: nothing shared ($with bytes)"; exit 1; fi
        [ "$level" = -O1 ] && shared="$with bytes of frames, $without without sharing"
    done
    echo "pass  $device  (5 settings; at -O1 $shared)"
done
