#!/bin/sh
# Check the XC8 source forms: #pragma config (the linker makes the
# configuration and user ID words from the device's table), __EEPROM_DATA,
# __interrupt(), __bit, __persistent, ei()/di(). The words are compared with
# known values, the HEX file must carry all three areas, and wrong or
# conflicting settings must be refused by name.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
dump="$root/build/llvm-build/bin/llvm-objdump -s"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL $*"; exit 1; }

for level in -munoptimized -O1 -Os "-O1 -fno-lto"; do
    "$cc" $level -mcpu=pic16f1937 -o "$tmp/x.hex" "$root/tests/xc8/xc8.c"
    $dump -j .config "$tmp/x.elf" | grep -q "1000e c409ff3e" || fail "$level: configuration words"
    $dump -j .userid "$tmp/x.elf" | grep -q "10000 01000200 03000400" || fail "$level: user IDs"
    $dump -j .eeprom "$tmp/x.elf" | grep -q "1e000 70006b00 32007000 72006f00 62006500" || fail "$level: EEPROM data"
    $dump -j .eeprom "$tmp/x.elf" | grep -q "1e010 00001100 22003300 44005500 66007700" || fail "$level: EEPROM data, second line"
    grep -q "^:04000E00C409FF3E" "$tmp/x.hex" || fail "$level: configuration words in the HEX file"
    grep -q "^:10E0000070006B00" "$tmp/x.hex" || fail "$level: EEPROM data in the HEX file"
    code=0; "$sim" run "$tmp/x.hex" > /dev/null || code=$?
    [ "$code" = 81 ] || fail "$level: exit code $code"
done
echo "pass  the words, at 4 settings"

# A setting on a device that has it in another word.
printf '#include <xc.h>\n#pragma config WDTE = OFF\nvoid main(void) { SLEEP(); }\n' > "$tmp/w.c"
"$cc" -mcpu=pic16f18877 -o "$tmp/w.elf" "$tmp/w.c"
$dump -j .config "$tmp/w.elf" | grep -q "1000e ff3fff3f 9f3fff3f ff3f" || fail "pic16f18877: WDTE"
echo "pass  another device"

refused() {  # refused <what the linker must say> <source lines...>
    want=$1; shift
    printf '%s\n' "#include <xc.h>" "$@" "void main(void) {}" > "$tmp/b.c"
    if "$cc" -mcpu=pic16f1937 -o "$tmp/b.elf" "$tmp/b.c" > "$tmp/out" 2>&1; then fail "accepted: $*"; fi
    grep -qF -- "$want" "$tmp/out" || { cat "$tmp/out"; fail "no '$want' for: $*"; }
}
refused "b.c:2: #pragma config: FOSC cannot be BOGUS (it can be" "#pragma config FOSC = BOGUS"
refused "this device has no setting NOPE" "#pragma config NOPE = 1"
refused "WDTE is also set, differently, at" "#pragma config WDTE = OFF" "#pragma config WDTE = ON"
refused "both by #pragma config and as words" "#pragma config WDTE = OFF" "PIC_CONFIG_WORDS(0x09C4, 0x3EFF);"
refused "expected '='" "#pragma config LVP"
echo "pass  wrong settings are refused"
