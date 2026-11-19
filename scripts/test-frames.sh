#!/bin/sh
# Check the linker's choice between stack frames and static frames
# (docs/toolchain.md, "Static frames"): a program whose functions fall in
# each case must get the frames listed here, and must run correctly.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0

for level in O1 O2 Os; do
    # -fno-lto: compiled as a whole program the compiler would settle all
    # of this itself and leave the linker nothing to choose.
    "$cc" -mcpu=pic16f1937 -mcmodel=small -fno-lto -$level -std=c99 -I"$root/tests/common" -Wl,--verbose \
        -o "$tmp/frames.hex" "$root/tests/frames/main.c" "$root/tests/frames/a.c" \
        "$root/tests/common/bench.c" > "$tmp/link.log" 2>&1
    expect() {  # expect <kind> <function>
        if ! grep -q "$1 frame for $2\b" "$tmp/link.log"; then
            echo "FAIL -$level: expected a $1 frame for $2" >&2
            status=1
        fi
    }
    expect static plain
    expect static through_pointer
    expect static bench_putc
    expect stack odd
    expect stack even
    expect stack reenters
    expect stack fill
    # shared is called from main and from the handler: one copy with a stack
    # frame, or two with static frames if those are no larger together.
    if ! grep -q "frame for shared" "$tmp/link.log"; then
        echo "FAIL -$level: no decision for shared" >&2
        status=1
    fi
    # And the same program compiled as a whole.
    "$cc" -mcpu=pic16f1937 -$level -std=c99 -I"$root/tests/common" \
        -o "$tmp/whole.hex" "$root/tests/frames/main.c" "$root/tests/frames/a.c" \
        "$root/tests/common/bench.c"
    if ! "$sim" run "$tmp/whole.hex" | cmp -s - "$root/tests/frames/expected.txt"; then
        echo "FAIL -$level: wrong output from the whole-program build" >&2
        status=1
    fi
    if "$sim" run "$tmp/frames.hex" | cmp -s - "$root/tests/frames/expected.txt"; then
        echo "pass  -$level"
    else
        echo "FAIL -$level: wrong output" >&2
        status=1
    fi
done
# Calls through constant function pointers: a program put together by
# dependency injection (tests/inject) has no indirect call left when it is
# compiled as a whole, and gives the right output either way.
bin=${PIC16F1_LLVM_BIN:-$root/build/llvm-build/bin}
for mode in "" -fno-lto; do
    "$cc" -mcpu=pic16f1937 -Os -std=c99 $mode -I"$root/tests/common" \
        -o "$tmp/inject.hex" "$root"/tests/inject/*.c "$root/tests/common/bench.c"
    if ! "$sim" run "$tmp/inject.hex" | cmp -s - "$root/tests/inject/expected.txt"; then
        echo "FAIL inject $mode: wrong output" >&2
        status=1
    fi
    indirect=$("$bin/llvm-objdump" -d "$tmp/inject.elf" | grep -c callw || true)
    words=$("$sim" size "$tmp/inject.hex")
    if [ -z "$mode" ] && [ "$indirect" != 0 ]; then
        echo "FAIL inject: $indirect indirect calls left in the whole-program build" >&2
        status=1
    fi
    echo "inject ${mode:-whole program}: $words words, $indirect indirect calls"
done
exit $status
