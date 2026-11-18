#!/bin/sh
# Build one test program with the native compiler and run it.
# Usage: scripts/host-run.sh <program>      (prints its output, exits with its code)
# timer_isr needs a real Timer0 and cannot run this way.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
prog=$1
out=${TMPDIR:-/tmp}/pic16f1-host-$prog
cc -std=c99 -O1 -fwrapv -Wno-main -Dmain=pic_main \
   -I"$root/tests/host" -I"$root/tests/common" \
   "$root/tests/programs/$prog/main.c" "$root/tests/host/bench_host.c" -o "$out"
"$out"
