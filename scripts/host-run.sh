#!/bin/sh
# Build one test program with the native compiler and run it.
# Usage: scripts/host-run.sh <program> [tests-dir]
#   Prints the program's output and exits with its code. tests-dir defaults
#   to tests/programs. Programs that need a real Timer0 cannot run this way.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
prog=$1
tests=${2:-$root/tests/programs}
out=${TMPDIR:-/tmp}/pic16f1-host-$prog
cc -std=c99 -O1 -fwrapv -Wno-main -Wno-shift-count-overflow -Dmain=pic_main \
   -I"$root/tests/host" -I"$root/tests/common" \
   "$tests/$prog/main.c" "$root/tests/host/bench_host.c" -o "$out"
"$out"
