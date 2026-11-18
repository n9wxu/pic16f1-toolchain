#!/bin/sh
# 1. Assemble tests/assembler/instructions.s with llvm-mc and compare every
#    word with what Microchip's pic-as produced for the same source.
# 2. Round trip: assemble tests/assembler/roundtrip.s, disassemble it,
#    assemble the disassembly, and require identical bytes.
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

"$bin/llvm-mc" -triple=pic16f1 -filetype=obj -o "$tmp/a.o" "$root/tests/assembler/roundtrip.s"
"$bin/llvm-objdump" -d --no-show-raw-insn --no-leading-addr "$tmp/a.o" |
    sed -n '/<.text>/,$p' | tail -n +2 > "$tmp/b.s"
"$bin/llvm-mc" -triple=pic16f1 -filetype=obj -o "$tmp/b.o" "$tmp/b.s"
"$bin/llvm-objcopy" -O binary -j .text "$tmp/a.o" "$tmp/a.bin"
"$bin/llvm-objcopy" -O binary -j .text "$tmp/b.o" "$tmp/b.bin"
cmp "$tmp/a.bin" "$tmp/b.bin"
echo "round trip: $(($(wc -c < "$tmp/a.bin") / 2)) instructions reassemble to identical bytes"
