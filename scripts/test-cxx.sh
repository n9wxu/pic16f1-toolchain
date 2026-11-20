#!/bin/sh
# Check C++: the language without a library (tests/cxx/features.cpp), the
# header-only part of the standard library (library.cpp), dynamic memory
# and the standard containers (heap.cpp), and the device library
# <pic/pic.hpp> (hal.cpp). Each program checks itself on the simulator.
# The last three need the C++ headers and library (scripts/build-cxx.sh,
# after scripts/build-picolibc.sh) and are skipped without them.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

n=0
try() {  # try <device> <source> <options...>: build, run, expect 0x5A
    device=$1; src=$2; shift 2
    "$cc" "$@" -mcpu="$device" -o "$tmp/t.hex" "$root/tests/cxx/$src"
    code=0; "$sim" run --device "$device" "$tmp/t.elf" > "$tmp/out" || code=$?
    [ "$code" = 90 ] || { echo "FAIL $device $src $*: exit code $code"; exit 1; }
    n=$((n + 1))
}

# The language: every level, both code models, whole program and file by
# file, both instruction selectors; -munoptimized where it fits.
for device in pic16f1937 pic16f1032; do
    try $device features.cpp -std=c++20 -munoptimized
    for level in -O1 -O2 -O3 -Os; do
        for more in "" "-mcmodel=medium" "-fno-lto" "-fno-lto -mllvm -global-isel=0"; do
            # shellcheck disable=SC2086
            try $device features.cpp -std=c++20 $level $more
        done
    done
    # shellcheck disable=SC2086
    try $device features.cpp -std=c++17 -Os
    echo "pass  $device, the language"
done
# -g changes no code.
"$cc" -std=c++20 -O1 -mcpu=pic16f1937 -o "$tmp/a.hex" "$root/tests/cxx/features.cpp"
"$cc" -std=c++20 -O1 -g -mcpu=pic16f1937 -o "$tmp/b.hex" "$root/tests/cxx/features.cpp"
cmp -s "$tmp/a.hex" "$tmp/b.hex" || { echo "FAIL: -g changed the code"; exit 1; }

if [ ! -e "$root/build/toolchain/include/c++/__config_site" ] && [ -z "${PIC16F1_CXX_INCLUDE:-}" ]; then
    echo "skip  the standard library (run scripts/build-cxx.sh)"
    echo "$n builds"
    exit 0
fi
for level in -O1 -O2 -Os; do
    try pic16f1937 library.cpp -std=c++20 $level
    try pic16f1937 hal.cpp -std=c++20 $level
    grep -q "pins 12345 A5" "$tmp/out" || { echo "FAIL hal.cpp $level: serial output"; cat "$tmp/out"; exit 1; }
done
try pic16f1064 library.cpp -std=c++20 -munoptimized
try pic16f1937 library.cpp -std=c++23 -Os -fno-lto
try pic16f1937 heap.cpp -std=c++20 -O1
try pic16f1937 heap.cpp -std=c++20 -Os
try pic16f18877 heap.cpp -std=c++20 -Os
echo "pass  the standard library, dynamic memory and <pic/pic.hpp>"
echo "$n builds"
