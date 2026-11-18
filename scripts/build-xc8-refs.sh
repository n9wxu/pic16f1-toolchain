#!/bin/sh
# Build every test program with MPLAB XC8 at each optimization level and
# store the results under reference/xc8-v<version>/<program>/<level>/.
#
# XC8 and the device family pack are installed outside this repository:
#   XC8_HOME  default /Applications/microchip/xc8/v4.00
#   XC8_DFP   default $HOME/tools/microchip-packs/PIC12-16F1xxx_DFP/1.10.270
#
# Usage: scripts/build-xc8-refs.sh [program ...]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
XC8_HOME=${XC8_HOME:-/Applications/microchip/xc8/v4.00}
XC8_DFP=${XC8_DFP:-$HOME/tools/microchip-packs/PIC12-16F1xxx_DFP/1.10.270}
DEVICE=${DEVICE:-16F1937}
LEVELS=${LEVELS:-"O0 O1 O2 O3 Os"}
xc8="$XC8_HOME/bin/xc8-cc"

version=$("$xc8" --version | sed -n 's/.* V\([0-9.]*\).*/\1/p' | head -1)
dfp_version=$(basename "$XC8_DFP")
outroot="$root/reference/xc8-v$version"
# Build in a fixed scratch path so no local path ends up in the outputs.
work=${TMPDIR:-/tmp}/pic16f1-xc8-refs
rm -rf "$work"

[ $# -gt 0 ] || set -- $(ls "$root/tests/programs")
for prog in "$@"; do
    for level in $LEVELS; do
        dir="$work/$prog/$level"
        mkdir -p "$dir"
        cp "$root/tests/programs/$prog/main.c" "$root/tests/common/bench.c" "$root/tests/common/bench.h" "$dir/"
        if ! (cd "$dir" && "$xc8" -mcpu="$DEVICE" -mdfp="$XC8_DFP/xc8" -"$level" -std=c99 \
                -o "$prog.elf" main.c bench.c > build.log 2>&1); then
            echo "FAILED: $prog $level" >&2
            cat "$dir/build.log" >&2
            exit 1
        fi
        words=$(sed -n 's/.*Program space *used *[0-9A-Fa-f]*h *( *\([0-9]*\)).*/\1/p' "$dir/build.log")
        data=$(sed -n 's/.*Data space *used *[0-9A-Fa-f]*h *( *\([0-9]*\)).*/\1/p' "$dir/build.log")
        dest="$outroot/$prog/$level"
        mkdir -p "$dest"
        cp "$dir/$prog.hex" "$dest/$prog.hex"
        cp "$dir/$prog.sym" "$dest/$prog.sym"
        cat > "$dest/info.json" <<JSON
{
 "program": "$prog",
 "compiler": "xc8",
 "compiler_version": "$version",
 "device": "PIC$DEVICE",
 "device_pack": "PIC12-16F1xxx_DFP $dfp_version",
 "flags": "-$level -std=c99",
 "program_words": $words,
 "data_bytes": $data
}
JSON
        printf '%-10s %-3s %5s words %4s bytes\n' "$prog" "$level" "$words" "$data"
    done
done
