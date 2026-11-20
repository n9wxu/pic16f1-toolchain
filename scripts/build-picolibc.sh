#!/bin/sh
# Build picolibc (the C library) with the PIC16F1 toolchain.
#
# Fetches the picolibc commit named in toolchain/picolibc/VERSION, applies
# toolchain/picolibc/pic16f1.patch (a machine directory and two header
# entries), builds with meson, and installs into build/toolchain/picolibc,
# once for 32-bank and once for 64-bank devices.
# meson is installed into a private Python environment under build/.
#
# Usage: scripts/build-picolibc.sh [llvm-bin-dir] [install-dir]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:-$root/build/llvm-build/bin}
dest=${2:-$root/build/toolchain/picolibc}
src=$root/build/picolibc
rev=$(cat "$root/toolchain/picolibc/VERSION")

if [ ! -d "$src/.git" ]; then
    mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" remote add origin https://github.com/picolibc/picolibc
    git -C "$src" fetch -q --depth 1 origin "$rev"
    git -C "$src" checkout -q FETCH_HEAD
    git -C "$src" apply "$root/toolchain/picolibc/pic16f1.patch"
fi

if [ ! -x "$root/build/venv/bin/meson" ]; then
    python3 -m venv "$root/build/venv"
    "$root/build/venv/bin/pip" -q install meson
fi

# No outlining in a library: a program links a few of its functions, so a
# sequence shared between functions would cost more than it saves.
#
# Code that names a global selects its bank with MOVLB, which is encoded
# differently on 32- and 64-bank devices, so the library is built once for
# each. The driver picks the one that matches the device.
build_variant() {  # build_variant <banks> <cpu>
    banks=$1
    cpu=$2
    cross=$root/build/picolibc-cross-banks$banks.txt
    bld=$root/build/picolibc-build-banks$banks
    out=$dest/banks$banks
    cat > "$cross" <<CROSS
[binaries]
c = ['$bin/clang', '--target=pic16f1', '-mcpu=$cpu', '-nostdlib', '-ffunction-sections', '-fdata-sections', '-mllvm', '-enable-machine-outliner=never']
ar = '$bin/llvm-ar'
nm = '$bin/llvm-nm'
strip = '$bin/llvm-objcopy'

[host_machine]
system = 'none'
cpu_family = 'pic16f1'
cpu = 'pic16f1'
endian = 'little'

[properties]
skip_sanity_check = true
CROSS
    rm -rf "$bld" "$out"
    # Integer-only printf and scanf by default; no threads, locales or TLS.
    "$root/build/venv/bin/meson" setup --cross-file "$cross" \
        -Dmultilib=false -Dtests=false -Dpicocrt=false -Dsemihost=false \
        -Dthread-local-storage=false -Dsingle-thread=true -Dformat-default=integer \
        -Dio-long-long=false -Dio-float-exact=false -Datomic-ungetc=false -Dspecsdir=none \
        -Dnewlib-global-errno=true -Dposix-console=false -Dmb-capable=false \
        -Dwant-math-errno=false -Doptimization=s -Dprefix="$out" \
        "$bld" "$src" > "$bld.setup.log" 2>&1 || { tail -20 "$bld.setup.log"; exit 1; }
    "$root/tools/memguard.py" --max-mb 1500 -- ninja -C "$bld" install > "$bld.log" 2>&1 || {
        grep -E "error|FAILED" "$bld.log" | head -20; exit 1; }
    echo "picolibc for $banks-bank devices installed in $out"
}

build_variant 32 pic16f1937
build_variant 64 pic16f18877
