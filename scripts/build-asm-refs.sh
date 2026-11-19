#!/bin/sh
# Build the hand-written assembly versions of the benchmark programs
# (tests/asm), in the layout that tools/bench.py reads:
#   <out>/<program>/asm/<program>.hex           from main.S: smallest and
#                                               fastest, results worked out
#                                               beforehand where they can be
#   <out>/<program>/asm-runtime/<program>.hex   from runtime.S: everything
#                                               computed when it runs
# They are stand-alone: no startup code and no library.
#
# Usage: scripts/build-asm-refs.sh [out-dir] [program ...]
#   out-dir defaults to build/out/asm-pic16f1
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/out/asm-pic16f1}
[ $# -gt 0 ] && shift
bin=${PIC16F1_LLVM_BIN:-$root/build/llvm-build/bin}
lib=${PIC16F1_LIB:-$root/build/toolchain/lib/pic16f1}
sim=${PIC16F1_SIM:-$root/sim/target/release/pic16f1-sim}
status=0

[ $# -gt 0 ] || set -- $(ls "$root/tests/asm" | grep -v '^common$')
for prog in "$@"; do
    for version in main runtime; do
        src="$root/tests/asm/$prog/$version.S"
        [ -e "$src" ] || continue
        level=asm
        [ $version = runtime ] && level=asm-runtime
        dir="$out/$prog/$level"
        mkdir -p "$dir"
        rm -f "$dir/$prog.hex" "$dir/$prog.elf"
        if ! { "$bin/clang" --target=pic16f1 -mcpu=pic16f1937 -I"$root/tests/asm/common" \
                   -I"$root/toolchain/include" -c "$src" -o "$dir/$prog.o" &&
               "$bin/ld.lld" -T "$lib/pic16f1937.ld" "$dir/$prog.o" -o "$dir/$prog.elf" &&
               "$sim" elf2hex "$dir/$prog.elf" "$dir/$prog.hex"; } > "$dir/build.log" 2>&1; then
            echo "BUILD FAILED: $prog $version (see $dir/build.log)" >&2
            status=1
            continue
        fi
        data=$("$bin/llvm-size" -A "$dir/$prog.elf" |
               awk '$1 == ".data" || $1 == ".bss" { s += $2 } END { print s + 0 }')
        cat > "$dir/info.json" <<JSON
{
 "program": "$prog",
 "compiler": "hand assembly ($version)",
 "device": "PIC16F1937",
 "flags": "",
 "data_bytes": $data
}
JSON
    done
done
exit $status
