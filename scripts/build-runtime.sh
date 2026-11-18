#!/bin/sh
# Build the startup code and runtime library with the toolchain's own clang.
# Output: build/toolchain/lib/pic16f1/{crt0.o,libpicrt.a} plus the linker scripts.
#
# Usage: scripts/build-runtime.sh [llvm-bin-dir] [out-dir]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:-$root/build/llvm-build/bin}
out=${2:-$root/build/toolchain/lib/pic16f1}
rt=$root/toolchain/runtime
mkdir -p "$out"
cc="$bin/clang --target=pic16f1 -mcpu=pic16f1937 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Os"

$cc -c "$rt/crt0.S" -o "$out/crt0.o"
$cc -c "$rt/shift.S" -o "$out/shift.o"
$cc -c "$rt/muldiv.c" -o "$out/muldiv.o"
$cc -c "$rt/mem.c" -o "$out/mem.o"
rm -f "$out/libpicrt.a"
"$bin/llvm-ar" rcs "$out/libpicrt.a" "$out/shift.o" "$out/muldiv.o" "$out/mem.o"
rm -f "$out/shift.o" "$out/muldiv.o" "$out/mem.o"
cp "$root"/toolchain/lib/*.ld "$out/"
echo "runtime built in $out"
