#!/bin/sh
# Assemble a self-contained toolchain tree in build/toolchain:
#   bin/      clang, ld.lld, LLVM tools, pic16f1-cc, pic16f1-sim,
#             lldb and lldb-dap with their pic16f1- wrappers,
#             pic16f1-new-project
#   share/pic16f1/project             the template pic16f1-new-project copies
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
cp "$root"/toolchain/include/*.h "$root"/toolchain/include/*.inc "$out/include/pic16f1/"
# The C++ device library. (The C++ standard headers and libc++.a are put
# in this tree by scripts/build-cxx.sh.)
mkdir -p "$out/include/pic16f1/pic"
cp "$root"/toolchain/include/pic/*.hpp "$out/include/pic16f1/pic/"
cp "$root"/toolchain/lib/*.ld "$root"/toolchain/lib/*.cfg "$out/lib/pic16f1/"
rt=$root/build/toolchain/lib/pic16f1
if [ "$out/lib/pic16f1" != "$rt" ]; then
    cp "$rt"/crt0*.o "$rt"/libpicrt*.a "$out/lib/pic16f1/"
fi
if [ -x "$root/sim/target/release/pic16f1-sim" ]; then
    cp "$root/sim/target/release/pic16f1-sim" "$out/bin/"
fi
# The debugger, if it was built (scripts/build-llvm.sh builds it unless
# NO_LLDB is set), with the library it loads from ../lib.
if [ -x "$bld/bin/lldb-dap" ]; then
    mkdir -p "$out/lib"
    cp "$bld/bin/lldb" "$bld/bin/lldb-dap" "$out/bin/"
    lib=$(cd "$bld/lib" && ls liblldb.*.*.*.dylib liblldb.so.*.*.* 2>/dev/null | head -1)
    cp "$bld/lib/$lib" "$out/lib/"
    for link in "$bld"/lib/liblldb.*; do
        [ -L "$link" ] && ln -sf "$(readlink "$link")" "$out/lib/$(basename "$link")"
    done
    cp "$root/toolchain/bin/pic16f1-lldb" "$root/toolchain/bin/pic16f1-lldb-dap" "$out/bin/"
fi
cp "$root/toolchain/bin/pic16f1-new-project" "$out/bin/"
# Other languages: the tools that bring a front end's LLVM IR to this
# target, the Rust crate and target description, the Zig device files.
cp "$root/toolchain/bin/pic16f1-retarget" "$root/toolchain/bin/pic16f1-rust-ld" \
    "$root/toolchain/bin/pic16f1-zig" "$root/toolchain/bin/pic16f1-forth" "$out/bin/"
rm -rf "$out/share/pic16f1/rust" "$out/share/pic16f1/zig"
mkdir -p "$out/share/pic16f1/rust" "$out/share/pic16f1/zig"
cp "$root/toolchain/rust/pic16f1.json" "$out/share/pic16f1/rust/"
cp -R "$root/toolchain/rust/pic16f1" "$out/share/pic16f1/rust/pic16f1"
rm -rf "$out/share/pic16f1/rust/pic16f1/target" "$out/share/pic16f1/rust/pic16f1/Cargo.lock"
cp -R "$root/toolchain/zig/." "$out/share/pic16f1/zig/"
rm -rf "$out/share/pic16f1/project" "$out"/share/pic16f1/project-*
mkdir -p "$out/share/pic16f1"
cp -R "$root/toolchain/project" "$out/share/pic16f1/project"
for lang in c++ rust zig arduino; do
    cp -R "$root/toolchain/project-$lang" "$out/share/pic16f1/project-$lang"
done
rm -rf "$out/share/pic16f1/arduino"
cp -R "$root/toolchain/arduino" "$out/share/pic16f1/arduino"
mkdir -p "$out/share/pic16f1/svd"
cp "$root"/toolchain/svd/*.svd "$out/share/pic16f1/svd/"
# The C library, if it was built somewhere else than in this tree.
if [ -d "$root/build/toolchain/picolibc" ] && [ "$out" != "$root/build/toolchain" ]; then
    rm -rf "$out/picolibc"
    cp -R "$root/build/toolchain/picolibc" "$out/picolibc"
fi
cp "$root/LICENSE" "$out/"
echo "toolchain tree: $out"
