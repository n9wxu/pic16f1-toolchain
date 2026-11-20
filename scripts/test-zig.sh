#!/bin/sh
# Check Zig: the language (tests/zig/features.zig) and the library pic.zig
# (tests/zig/hal.zig), compiled by pic16f1-zig in each of Zig's four build
# modes and run on the simulator. Needs a Zig compiler ($PIC16F1_ZIG,
# build/zig/zig, or zig on the PATH; tested with 0.15.2); skipped without.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
zigcc=$root/toolchain/bin/pic16f1-zig
sim=$root/toolchain/bin/pic16f1-sim
if [ -z "${PIC16F1_ZIG:-}" ] && [ ! -x "$root/build/zig/zig" ] && ! command -v zig > /dev/null 2>&1; then
    echo "skip  Zig: no Zig compiler"
    exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"   # Zig keeps its cache where it is run

n=0
for mode in ReleaseSmall ReleaseFast ReleaseSafe Debug; do
    for prog in features hal; do
        "$zigcc" -mcpu=pic16f1937 -O $mode -o "$tmp/$prog.hex" "$root/tests/zig/$prog.zig"
        code=0; "$sim" run --device pic16f1937 "$tmp/$prog.elf" > "$tmp/out" || code=$?
        [ "$code" = 90 ] || { echo "FAIL $prog $mode: exit code $code"; exit 1; }
        n=$((n + 1))
    done
    grep -q "pins 12345 A5" "$tmp/out" || { echo "FAIL hal $mode: serial output"; cat "$tmp/out"; exit 1; }
done
# The linker made the configuration words from pic.config: the internal
# oscillator and no watchdog in the first, nothing changed in the second.
words=$("$root/build/llvm-build/bin/llvm-objdump" -s -j .config "$tmp/hal.elf" | tail -1 | awk '{print $2}')
[ "$words" = e43fff3f ] || { echo "FAIL hal: configuration words $words"; exit 1; }
# For the in-circuit debugger, with debug information, and with a C file
# in the same program.
cat > "$tmp/helper.c" <<'C'
unsigned char c_twice(unsigned char x) { return x * 2; }
C
cat > "$tmp/mixed.zig" <<'Z'
const pic = @import("pic");
extern fn c_twice(x: u8) u8;
export fn main() noreturn {
    pic.device.TRISD.* = 0;
    pic.device.LATD.* = c_twice(pic.device.PORTB.* + 45);
    while (true) pic.sleep();
}
Z
"$zigcc" -mcpu=pic16f1937 -o "$tmp/mixed.hex" "$tmp/mixed.zig" "$tmp/helper.c"
code=0; "$sim" run --device pic16f1937 "$tmp/mixed.elf" > /dev/null || code=$?
[ "$code" = 90 ] || { echo "FAIL Zig calling C: exit code $code"; exit 1; }
"$zigcc" -mcpu=pic16f1937 -g -mdebugger -o "$tmp/hardware.hex" "$root/tests/zig/hardware.zig"
"$root/build/llvm-build/bin/llvm-readelf" -S "$tmp/hardware.elf" | grep -q debug_info || { echo "FAIL hardware: no debug information"; exit 1; }
n=$((n + 2))
echo "pass  Zig: the language and pic.zig in four modes, with C, and a build for the debugger"
echo "$n builds"
