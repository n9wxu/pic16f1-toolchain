#!/bin/sh
# Configure and build clang, lld and the LLVM tools with the PIC16F1 target,
# then build the runtime library and assemble an installable tree in
# build/toolchain.
#
# Usage: scripts/build-llvm.sh
#   LLVM_SRC    llvm-project checkout (default build/llvm-project)
#   LLVM_BUILD  build directory        (default build/llvm-build)
#   CMAKE_EXTRA extra cmake arguments, e.g. a compiler launcher
#   NINJA_JOBS  how many compilations at once (default: ninja's own choice)
#   NINJA_KEEP  how many failures before stopping (default 1; 0: go on, as
#               CI does, to show every error of a compiler in one run)
#   HOST_ONLY   if set, stop after the host programs: no runtime library
#               and no build/toolchain (scripts/build-release.sh uses this
#               for the builds a release is made of)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
src=${LLVM_SRC:-$root/build/llvm-project}
bld=${LLVM_BUILD:-$root/build/llvm-build}
tools="clang lld llc llvm-mc llvm-objdump llvm-readelf llvm-objcopy llvm-ar llvm-nm llvm-size FileCheck not"
# The debugger and its adapter for editors (VS Code). They add about 1,300
# files to the build; NO_LLDB=1 leaves them out.
[ -n "${NO_LLDB:-}" ] || tools="$tools lldb lldb-dap"

mkdir -p "$bld"
cmake -G Ninja -S "$src/llvm" -B "$bld" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_ENABLE_PROJECTS="clang;lld$([ -n "${NO_LLDB:-}" ] || echo ';lldb')" \
    -DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_INCLUDE_TESTS=OFF \
    -DLLDB_USE_SYSTEM_DEBUGSERVER=ON -DLLDB_ENABLE_CURSES=OFF \
    -DLLDB_ENABLE_LZMA=OFF -DLLDB_ENABLE_LIBXML2=ON \
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
ninja -C "$bld" ${NINJA_JOBS:+-j$NINJA_JOBS} ${NINJA_KEEP:+-k$NINJA_KEEP} $tools
[ -z "${HOST_ONLY:-}" ] || exit 0

"$root/scripts/build-runtime.sh" "$bld/bin"
"$root/scripts/package-toolchain.sh"
