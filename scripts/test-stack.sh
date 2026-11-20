#!/bin/sh
# Check the linker's count of hardware stack levels: against the depth the
# simulator measures (the count must never be less, and here is equal), and
# that a program that needs more levels than the part has is warned about
# while one that just fits is not.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for level in -munoptimized -O1 -Os; do
    for depth in 3 9 15; do
        "$cc" $level -mcpu=pic16f1937 -mstack-report -DDEPTH=$depth -o "$tmp/d.elf" \
            "$root/tests/stack/deep.c" > "$tmp/out" 2>&1
        # The handler never runs here; its level and its calls are counted.
        said=$(sed -n 's/.*hardware stack: \([0-9]*\) of 16.*/\1/p' "$tmp/out")
        main=$(sed -n 's/.*; \([0-9]*\) by _start.*/\1/p' "$tmp/out")
        [ -n "$said" ] && [ -n "$main" ] || { echo "FAIL $level depth $depth: no report"; cat "$tmp/out"; exit 1; }
        if [ "$said" -gt 16 ]; then
            grep -q "warning: hardware stack" "$tmp/out" || { echo "FAIL $level depth $depth: $said levels and no warning"; exit 1; }
            continue
        fi
        if grep -q "warning" "$tmp/out"; then echo "FAIL $level depth $depth: warned at $said levels"; cat "$tmp/out"; exit 1; fi
        measured=$("$sim" run --json "$tmp/d.elf" | sed -n 's/.*"max_stack_depth": *\([0-9]*\).*/\1/p')
        [ "$main" = "$measured" ] || { echo "FAIL $level depth $depth: counted $main, the simulator measured $measured"; cat "$tmp/out"; exit 1; }
    done
    # Too deep: said at -O1 and below, where the calls are all kept.
    "$cc" $level -mcpu=pic16f1937 -DDEPTH=15 -o "$tmp/d.elf" "$root/tests/stack/deep.c" > "$tmp/out" 2>&1
    grep -q "needs more levels than the part has" "$tmp/out" || { echo "FAIL $level: 15 deep and no warning"; cat "$tmp/out"; exit 1; }
    echo "pass  $level"
done
