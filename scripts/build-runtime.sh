#!/bin/sh
# Build the startup code and runtime library with the toolchain's own clang.
# Output: build/toolchain/lib/pic16f1/{crt0.o,libpicrt.a} plus the linker scripts.
#
# Usage: scripts/build-runtime.sh [llvm-bin-dir] [out-dir]
#   LLVM_SRC  llvm-project checkout, for the compiler-rt sources
#             (default build/llvm-project)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:-$root/build/llvm-build/bin}
out=${2:-$root/build/toolchain/lib/pic16f1}
rt=$root/toolchain/runtime
mkdir -p "$out"
# No outlining: a program links only the routines it calls, so a sequence
# shared between routines would cost more than it saves, and these are the
# routines whose speed matters.
cc="$bin/clang --target=pic16f1 -mcpu=pic16f1937 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Os -mllvm -enable-machine-outliner=never"

# The library is built for each code model: libpicrt.a sets PCLATH before
# every call; libpicrt-small.a (with crt0-small.o) is for programs that fit
# in the first page and never sets it.
build_all() {  # build_all <suffix> <extra flags>
suffix=$1
model=$2
objs=""
build() {  # build <source> : compile one file into the library
    obj="$out/$(basename "${1%.*}")$suffix.o"
    $cc $model -w -c "$1" -o "$obj"
    objs="$objs $obj"
}

$cc $model -c "$rt/crt0.S" -o "$out/crt0$suffix.o"
build "$rt/copydata.S"
build "$rt/clearbss.S"
build "$rt/shift.S"
build "$rt/muldiv.S"
build "$rt/muldiv64.c"
build "$rt/mem.c"

# Single-precision floating point comes from LLVM's compiler-rt. float and
# double are both 32 bits on this target, so no double routines are needed.
crt=${LLVM_SRC:-$root/build/llvm-project}/compiler-rt/lib/builtins
for f in addsf3 subsf3 mulsf3 divsf3 negsf2 comparesf2 fixsfsi fixunssfsi \
         fixsfdi fixunssfdi floatsisf floatunsisf floatdisf floatundisf fp_mode; do
    build "$crt/$f.c"
done

rm -f "$out/libpicrt$suffix.a"
# shellcheck disable=SC2086
"$bin/llvm-ar" rcs "$out/libpicrt$suffix.a" $objs
# shellcheck disable=SC2086
rm -f $objs
}
build_all "" ""
build_all "-small" "-mcmodel=small -DONE_PAGE"
cp "$root"/toolchain/lib/*.ld "$out/"
echo "runtime built in $out"
