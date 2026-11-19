#!/bin/sh
# Build every test program with the PIC16F1 LLVM toolchain at each
# optimization level, into <out>/<program>/<level>/, in the layout that
# tools/bench.py reads.
#
# Usage: scripts/build-llvm-tests.sh [out-dir] [program ...]
#   out-dir defaults to build/out/llvm-pic16f1
#   TESTS_DIR selects the test directory (default tests/programs)
#   LEVELS selects the levels (default "O0 O1 O2 Os"). The extra level O0u
#   is -O0 -munoptimized: clang's own -O0, which the driver's -O0 is not.
# Exit status is 1 if any program fails to build.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/out/llvm-pic16f1}
tests=${TESTS_DIR:-$root/tests/programs}
[ $# -gt 0 ] && shift
LEVELS=${LEVELS:-"O0 O1 O2 Os"}
cc=${PIC16F1_CC:-$root/toolchain/bin/pic16f1-cc}
llvm_bin=${PIC16F1_LLVM_BIN:-$root/build/llvm-build/bin}
status=0

[ $# -gt 0 ] || set -- $(ls "$tests")
for prog in "$@"; do
    # Assembly sources beside main.c are part of the program.
    asm=$(ls "$tests/$prog"/*.S 2>/dev/null || true)
    for level in $LEVELS; do
        # A file named skip-<level> in the test directory excludes that level.
        [ -e "$tests/$prog/skip-$level" ] && continue
        flags="-$level"
        [ "$level" = O0u ] && flags="-O0 -munoptimized"
        dir="$out/$prog/$level"
        mkdir -p "$dir"
        rm -f "$dir/$prog.hex" "$dir/$prog.elf"
        if ! "$cc" -mcpu=pic16f1937 $flags -std=c99 -I"$root/tests/common" \
                -o "$dir/$prog.hex" "$tests/$prog/main.c" $asm \
                "$root/tests/common/bench.c" > "$dir/build.log" 2>&1; then
            echo "BUILD FAILED: $prog $level (see $dir/build.log)" >&2
            status=1
            continue
        fi
        # RAM use: initialised data plus zero-initialised data. Stack is extra.
        data=$("$llvm_bin/llvm-size" -A "$dir/$prog.elf" |
               awk '$1 == ".data" || $1 == ".bss" || $1 == ".isr_stack" { s += $2 } END { print s + 0 }')
        cat > "$dir/info.json" <<JSON
{
 "program": "$prog",
 "compiler": "llvm-pic16f1",
 "device": "PIC16F1937",
 "flags": "$flags -std=c99",
 "data_bytes": $data
}
JSON
    done
done
exit $status
