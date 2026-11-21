#!/bin/sh
# Check a packaged toolchain tree (tools/package_release.py) by using it
# as someone who unpacked it would, on the system it is for: start and
# build a project in C and in C++, build and run a benchmark, a program
# that uses the C library and the C++ library, a Forth program, and debug
# on the simulator. On Windows it is run from Git Bash and uses the .exe
# launchers, as cmd and an editor would.
#
# Usage: tools/test_package.sh <tree>
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
tree=$(cd "$1" && pwd)
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) exe=.exe ;; *) exe= ;; esac
bin=$tree/bin
tmp=$(mktemp -d)
simpid=
trap 'kill $simpid 2>/dev/null || true; rm -rf "$tmp"' EXIT
fail() { echo "FAIL $*"; exit 1; }

cat "$tree/VERSION"
"$bin/clang$exe" --version | head -1

# Projects, as pic16f1-new-project makes them and pic16f1-build builds them.
for lang in c c++ arduino; do
    "$bin/pic16f1-new-project$exe" --language $lang "$tmp/p-$lang" > /dev/null
    "$bin/pic16f1-build$exe" "$tmp/p-$lang" > "$tmp/build.out" || { cat "$tmp/build.out"; fail "building a $lang project"; }
    grep -q "words of flash used" "$tmp/build.out" || fail "no size from a $lang project"
    echo "ok   $lang project: $(cat "$tmp/build.out")"
done
[ -s "$tmp/p-c/build/p-c.hex" ] || fail "no p-c.hex"

# A benchmark: what it prints on the simulator is what it should.
"$bin/pic16f1-cc$exe" -mcpu=pic16f1937 -Os -std=c99 -I"$root/tests/common" -o "$tmp/hello.hex" \
    "$root/tests/programs/hello/main.c" "$root/tests/common/bench.c"
"$bin/pic16f1-sim$exe" run "$tmp/hello.hex" | tr -d '\r' > "$tmp/hello.out" || true
cmp -s "$tmp/hello.out" "$root/tests/programs/hello/expected.txt" || { cat "$tmp/hello.out"; fail "hello prints something else"; }
echo "ok   hello runs on the simulator"

# The C library and the C++ library.
cat > "$tmp/lib.cpp" <<'CPP'
#include <xc.h>
#include <vector>
#include <string.h>
int main() {
    std::vector<unsigned char> v;
    for (unsigned char i = 1; i <= 10; i++) v.push_back(i);
    LATD = (v[0] == 1 && v[9] == 10 && strlen("four") == 4) ? 0x5A : 1;
    __asm__("sleep");
    for (;;) {}
}
CPP
"$bin/pic16f1-cc$exe" -mcpu=pic16f1937 -Os -o "$tmp/lib.hex" "$tmp/lib.cpp"
code=0; "$bin/pic16f1-sim$exe" run "$tmp/lib.elf" > /dev/null || code=$?
[ "$code" = 90 ] || fail "the C++ library program ended with $code"
echo "ok   C++ with the standard library"

# Forth.
printf ': MAIN  $5A LATD C!  SLEEP ;\n' > "$tmp/t.fth"
"$bin/pic16f1-forth$exe" -mcpu=pic16f1937 -o "$tmp/forth.hex" "$tmp/t.fth"
code=0; "$bin/pic16f1-sim$exe" run "$tmp/forth.elf" > /dev/null || code=$?
[ "$code" = 90 ] || fail "the Forth program ended with $code"
echo "ok   Forth"

# The debugger, through the simulator's debug server: a breakpoint by
# name, the call stack, a register (which LLDB knows from the description
# the simulator sends: this is what needs libxml2).
elf=$tmp/p-c/build/p-c.elf
"$bin/pic16f1-sim$exe" gdb --device pic16f1937 --port 0 "$elf" 2> "$tmp/sim.err" &
simpid=$!
n=0
while ! grep -q "port" "$tmp/sim.err"; do
    sleep 0.2; n=$((n + 1))
    [ $n -lt 50 ] || { cat "$tmp/sim.err"; fail "the simulator did not start"; }
done
port=$(sed -n 's/.*port \([0-9]*\).*/\1/p' "$tmp/sim.err")
"$bin/lldb$exe" --batch -o "gdb-remote $port" -o "breakpoint set --name main" -o "continue" \
    -o "bt" -o "register read pc wreg" "$elf" > "$tmp/lldb.out" 2>&1 || true
kill $simpid 2>/dev/null || true
for want in "stop reason = breakpoint" "main" "PC = " "WREG = "; do
    grep -qF -- "$want" "$tmp/lldb.out" || { cat "$tmp/lldb.out"; fail "debugger: no '$want'"; }
done
"$bin/lldb-dap$exe" --version > /dev/null 2>&1 || "$bin/lldb-dap$exe" --help > /dev/null || fail "lldb-dap does not start"
echo "ok   debugger"
echo "package ok: $tree"
