#!/bin/sh
# Install the C++ standard library (libc++, from the pinned LLVM release)
# for the PIC16F1: its headers, configured for a machine with no threads,
# files, locales, wide characters or exceptions, and a small library
# (libc++.a) of the few things the headers leave to compiled code: sorting
# of the built-in types, the string class, shared pointers, and what the
# containers call when they would throw.
#
# What is not there: streams, locales, <chrono>, <atomic>, <thread>,
# <filesystem>, <regex>, <format>, floating-point <charconv>, exceptions
# and run-time type information. The headers build on the C library's, so
# picolibc must be installed first (scripts/build-picolibc.sh); pic16f1-cc
# then links it for a C++ program without being asked.
#
# Usage: scripts/build-cxx.sh [include-dir] [lib-dir]
#   include-dir defaults to build/toolchain/include/c++
#   lib-dir     defaults to build/toolchain/lib/pic16f1
#   LLVM_SRC    llvm-project checkout (default build/llvm-project)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
src=${LLVM_SRC:-$root/build/llvm-project}/libcxx
out=${1:-$root/build/toolchain/include/c++}
[ -d "$src/include" ] || { echo "no libc++ sources in $src" >&2; exit 1; }
mkdir -p "$out"
# The headers, without the build's own files.
(cd "$src/include" && find . -type f ! -name CMakeLists.txt ! -name '*.in' ! -name '*.py') |
while read -r f; do
    mkdir -p "$out/$(dirname "$f")"
    cp "$src/include/$f" "$out/$f"
done
cat > "$out/__config_site" <<'SITE'
// libc++ configured for the PIC16F1 (scripts/build-cxx-headers.sh).
#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE

#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_ABI_FORCE_ITANIUM 0
#define _LIBCPP_ABI_FORCE_MICROSOFT 0
#define _LIBCPP_HAS_THREADS 0
#define _LIBCPP_HAS_MONOTONIC_CLOCK 0
#define _LIBCPP_HAS_MUSL_LIBC 0
#define _LIBCPP_HAS_THREAD_API_PTHREAD 0
#define _LIBCPP_HAS_THREAD_API_EXTERNAL 0
#define _LIBCPP_HAS_THREAD_API_WIN32 0
#define _LIBCPP_HAS_THREAD_API_C11 0
#define _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
#define _LIBCPP_HAS_VENDOR_AVAILABILITY_ANNOTATIONS 0
#define _LIBCPP_HAS_FILESYSTEM 0
#define _LIBCPP_HAS_RANDOM_DEVICE 0
#define _LIBCPP_HAS_LOCALIZATION 0
#define _LIBCPP_HAS_UNICODE 0
#define _LIBCPP_HAS_WIDE_CHARACTERS 0
#define _LIBCPP_HAS_TIME_ZONE_DATABASE 0
#define _LIBCPP_INSTRUMENTED_WITH_ASAN 0
#define _LIBCPP_PSTL_BACKEND_SERIAL
#define _LIBCPP_HARDENING_MODE_DEFAULT _LIBCPP_HARDENING_MODE_NONE
#define _LIBCPP_ASSERTION_SEMANTIC_DEFAULT _LIBCPP_ASSERTION_SEMANTIC_HARDENING_DEPENDENT
#define _LIBCPP_LIBC_PICOLIBC 1
#define _LIBCPP_LIBC_NEWLIB 0
#define _LIBCPP_LIBC_LLVM_LIBC 0

#endif
SITE
sed -e 's/@_LIBCPP_ASSERTION_HANDLER@//' "$src/vendor/llvm/default_assertion_handler.in" > "$out/__assertion_handler"
echo "C++ headers installed in $out"

# The compiled part. Only what a program uses is linked.
lib=${2:-$root/build/toolchain/lib/pic16f1}
cc=$root/toolchain/bin/pic16f1-cc
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
build_lib() {  # build_lib <suffix> <extra flags>
    objs=""
    for f in algorithm hash string memory new_helpers optional variant expected bind valarray stdexcept random_shuffle any; do
        # shellcheck disable=SC2086
        PIC16F1_CXX_INCLUDE="$out" "$cc" -Os -std=c++23 -fno-lto $2 -mcpu=pic16f1937 \
            -mllvm -enable-machine-outliner=never \
            -D_LIBCPP_BUILDING_LIBRARY -I"$src/src" -w -c -o "$tmp/$f$1.o" "$src/src/$f.cpp"
        objs="$objs $tmp/$f$1.o"
    done
    # The runtime library's own abort, which stops the processor, ahead of
    # the C library's, which raises a signal first (300 words).
    # shellcheck disable=SC2086
    "$cc" -Os -fno-lto $2 -mcpu=pic16f1937 -c -o "$tmp/abort$1.o" "$root/toolchain/runtime/abort.c"
    objs="$objs $tmp/abort$1.o"
    rm -f "$lib/libc++$1.a"
    # shellcheck disable=SC2086
    "${PIC16F1_LLVM_BIN:-$root/build/llvm-build/bin}/llvm-ar" rcs "$lib/libc++$1.a" $objs
}
build_lib "" ""
build_lib "-small" "-mcmodel=small"
echo "libc++.a built in $lib"
