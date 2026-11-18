#!/bin/sh
# Configure and build clang, lld and the LLVM tools with the PIC16F1 target,
# then build the runtime library and assemble an installable tree in
# build/toolchain.
#
# Usage: scripts/build-llvm.sh
#   LLVM_SRC    llvm-project checkout (default build/llvm-project)
#   LLVM_BUILD  build directory        (default build/llvm-build)
#   CMAKE_EXTRA extra cmake arguments, e.g. a compiler launcher
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
src=${LLVM_SRC:-$root/build/llvm-project}
bld=${LLVM_BUILD:-$root/build/llvm-build}
tools="clang lld llc llvm-mc llvm-objdump llvm-readelf llvm-objcopy llvm-ar llvm-nm llvm-size FileCheck not"

mkdir -p "$bld"
cmake -G Ninja -S "$src/llvm" -B "$bld" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD="PIC16F1" \
    -DLLVM_DEFAULT_TARGET_TRIPLE="pic16f1" \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_UTILS=ON \
    -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_INCLUDE_TESTS=OFF \
    -DCLANG_INCLUDE_DOCS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_BINDINGS=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF ${CMAKE_EXTRA:-}
# shellcheck disable=SC2086
ninja -C "$bld" $tools

"$root/scripts/build-runtime.sh" "$bld/bin"
"$root/scripts/package-toolchain.sh"
