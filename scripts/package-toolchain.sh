#!/bin/sh
# Assemble a self-contained toolchain tree in build/toolchain:
#   bin/      clang, ld.lld, LLVM tools, pic16f1-cc, pic16f1-sim
#   include/  device headers          lib/pic16f1/  runtime and linker scripts
#   lib/clang/<ver>/include           compiler-provided headers (stdint.h ...)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bld=${LLVM_BUILD:-$root/build/llvm-build}
out=${1:-$root/build/toolchain}
mkdir -p "$out/bin" "$out/include/pic16f1" "$out/lib/pic16f1"

clang_real=$(cd "$bld/bin" && ls clang-[0-9]* | head -1)
cp "$bld/bin/$clang_real" "$out/bin/$clang_real"
ln -sf "$clang_real" "$out/bin/clang"
cp "$bld/bin/lld" "$out/bin/lld"
ln -sf lld "$out/bin/ld.lld"
for t in llvm-mc llvm-objdump llvm-readelf llvm-objcopy llvm-ar llvm-nm llvm-size; do
    cp "$bld/bin/$t" "$out/bin/$t"
done
if [ "$(readlink "$bld/bin/llvm-readelf" || true)" = "llvm-readobj" ]; then
    cp "$bld/bin/llvm-readobj" "$out/bin/llvm-readobj"
    ln -sf llvm-readobj "$out/bin/llvm-readelf"
fi
rm -rf "$out/lib/clang"
mkdir -p "$out/lib/clang"
cp -R "$bld/lib/clang/." "$out/lib/clang/"
cp "$root/toolchain/bin/pic16f1-cc" "$out/bin/"
cp "$root"/toolchain/include/*.h "$out/include/pic16f1/"
cp "$root"/toolchain/lib/*.ld "$out/lib/pic16f1/"
rt=$root/build/toolchain/lib/pic16f1
if [ "$out/lib/pic16f1" != "$rt" ]; then
    cp "$rt/crt0.o" "$rt/libpicrt.a" "$out/lib/pic16f1/"
fi
if [ -x "$root/sim/target/release/pic16f1-sim" ]; then
    cp "$root/sim/target/release/pic16f1-sim" "$out/bin/"
fi
cp "$root/LICENSE" "$out/"
echo "toolchain tree: $out"
