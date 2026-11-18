#!/bin/sh
# Assemble tests/assembler/instructions.s with llvm-mc and compare every
# word with what Microchip's pic-as produced for the same source.
#
# Usage: scripts/test-assembler.sh [llvm-bin-dir]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:-$root/build/llvm-build/bin}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$bin/llvm-mc" -triple=pic16f1 -filetype=obj -o "$tmp/t.o" "$root/tests/assembler/instructions.s"
"$bin/llvm-objcopy" -O binary -j .text "$tmp/t.o" "$tmp/t.bin"
python3 - "$tmp/t.bin" "$root/tests/assembler/instructions.picas.txt" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
mine = struct.unpack("<%dH" % (len(data) // 2), data)
ref = [l.split(None, 1) for l in open(sys.argv[2]) if l.strip() and not l.startswith("#")]
bad = 0
if len(mine) != len(ref):
    print(f"word count differs: llvm-mc {len(mine)}, reference {len(ref)}")
    bad = 1
for got, (want, text) in zip(mine, ref):
    if got != int(want, 16):
        print(f"MISMATCH {text.strip():24s} llvm-mc {got:04X}  pic-as {want}")
        bad += 1
print(f"{len(ref)} instructions compared with pic-as, {bad} mismatches")
sys.exit(1 if bad else 0)
PY
