#!/bin/sh
# Check debugging on the simulator: build examples/debug with debug
# information, start the simulator's debug server and drive LLDB through a
# session. Checks breakpoints by function and by line, the call stack from
# the hardware return stack, parameters and variables (in registers, at
# fixed addresses and in stack frames, in the innermost frame and in a
# caller's), globals, pointers, expressions,
# stepping, a watchpoint and the exit code. Also that -g changes no code.
#
# Then the same debugger against the simulator behaving as the silicon's
# debug hardware does (PIC16F1_SIM_SILICON): one breakpoint, which stops the
# program after the instruction at its address. A stop must still be known
# as its breakpoint, and stepping over and out of calls must work, and land
# exactly, while the user's breakpoint holds the one there is.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=$root/toolchain/bin/pic16f1-cc
sim=$root/toolchain/bin/pic16f1-sim
lldb=$root/toolchain/bin/pic16f1-lldb
src=$root/examples/debug/main.c
tmp=$(mktemp -d)
trap 'kill $simpid 2>/dev/null || true; rm -rf "$tmp"' EXIT
simpid=
# The line of the recursive call, where the second breakpoint goes.
line=$(grep -n "return sum_to(n - 1) + here;" "$src" | cut -d: -f1)

session() {  # session <device> <elf> <lldb commands...>: output in $tmp/out
    device=$1; elf=$2; shift 2
    "$sim" gdb --device "$device" --port 0 "$elf" 2> "$tmp/sim.err" &
    simpid=$!
    while ! grep -q "port" "$tmp/sim.err"; do sleep 0.1; done
    port=$(sed -n 's/.*port \([0-9]*\).*/\1/p' "$tmp/sim.err")
    set -- -o "gdb-remote $port" "$@"
    "$lldb" --batch "$@" "$elf" > "$tmp/out" 2>&1 || true
    kill $simpid 2>/dev/null || true
    wait $simpid 2>/dev/null || true
}
expect() {  # expect <label> <text that must be in the output>
    grep -qF -- "$2" "$tmp/out" || { echo "FAIL $label: $1: no '$2' in:"; cat "$tmp/out"; exit 1; }
}

for device in pic16f1937 pic16f18877; do
    for level in -munoptimized -O1 -Os; do
        label="$device $level"
        "$cc" -g $level -mcpu=$device -o "$tmp/g.hex" "$src"
        "$cc" $level -mcpu=$device -o "$tmp/n.hex" "$src"
        cmp -s "$tmp/g.hex" "$tmp/n.hex" || { echo "FAIL $label: -g changed the code"; exit 1; }

        session $device "$tmp/g.elf" \
            -o "breakpoint set --name scale" -o "continue" -o "bt" \
            -o "frame variable x k" -o "target variable gain total" \
            -o "expression gain * 2 + 1" -o "finish" -o "frame variable n" \
            -o "breakpoint delete 1" \
            -o "breakpoint set --file main.c --line $line" -o "continue" \
            -o "continue" -o "frame variable here" -o "bt" \
            -o "up" -o "frame variable here" \
            -o "breakpoint delete 2" -o "watchpoint set variable total" \
            -o "continue" -o "watchpoint delete 1" -o "continue"
        expect "breakpoint by name" "stop reason = breakpoint 1.1"
        expect "call stack" "g.elf\`fill"
        expect "call stack" "g.elf\`main"
        expect "parameters" "(unsigned char) k = '\\x05'"
        expect "global" "(volatile unsigned char) gain = '\\x05'"
        expect "expression" "= 11"
        expect "step out" "stop reason = step out"
        expect "caller's parameter" "(unsigned char) n = '\\x04'"
        expect "watchpoint" "Watchpoint 1 hit"
        expect "breakpoint by line" "stop reason = breakpoint 2.1"
        expect "stack-frame variable" "(volatile unsigned char) here = '\\x04'"
        expect "the caller's stack-frame variable" "(volatile unsigned char) here = '\\x05'"
        expect "recursion in the call stack" "g.elf\`sum_to"
        expect "exit code" "exited with status = 83"
        echo "pass  $label"
    done
done

# Every instruction of a recursive function, prologue and epilogue too: the
# callers' frames must be right after each step.
for device in pic16f1937 pic16f18877; do
    "$cc" -g -munoptimized -mcpu=$device -o "$tmp/g.hex" "$src"
    python3 "$root/tests/debug/stepcheck.py" "$lldb" "$sim" "$tmp/g.elf" $device 250 > "$tmp/out" 2>&1 ||
        { echo "FAIL $device: frames while stepping by instruction"; cat "$tmp/out"; exit 1; }
    echo "pass  $device, stepping by instruction ($(cat "$tmp/out"))"
done

# The call stack from inside an interrupt handler that makes a call, down
# through the recursion it interrupted, with that recursion's variables.
for level in -munoptimized -O1; do
    label="pic16f1937 $level, interrupt"
    "$cc" -g $level -mcpu=pic16f1937 -o "$tmp/i.hex" "$root/tests/debug/isr.c"
    session pic16f1937 "$tmp/i.elf" \
        -o "breakpoint set --name tick" -o "continue" -o "bt" \
        -o "frame select 3" -o "frame variable here" \
        -o "frame select 6" -o "frame variable here" \
        -o "breakpoint delete 1" -o "continue"
    expect "the handler" "i.elf\`isr at isr.c"
    expect "the interrupted function" "frame #2: "
    expect "the bottom of the stack" "frame #7: "
    grep -q "frame #7: .*i.elf\`main" "$tmp/out" || { echo "FAIL $label: main is not frame 7"; cat "$tmp/out"; exit 1; }
    expect "a variable of an interrupted caller" "(volatile unsigned char) here = '\\x01'"
    expect "a variable further out" "(volatile unsigned char) here = '\\x04'"
    expect "exit code" "exited with status = 26"
    echo "pass  $label"
done

export PIC16F1_SIM_SILICON=1
device=pic16f1937
label="$device as silicon, -munoptimized"
"$cc" -g -munoptimized -mcpu=$device -o "$tmp/g.hex" "$src"
call=$(grep -n "samples\[i\] = scale(i, gain);" "$src" | cut -d: -f1)
session $device "$tmp/g.elf" \
    -o "breakpoint set --file main.c --line $call" -o "continue" \
    -o "breakpoint set --name main" \
    -o "next" -o "next" -o "next" -o "next" -o "continue" -o "finish" \
    -o "breakpoint delete 1" -o "continue"
expect "late stop known as its breakpoint" "stop reason = breakpoint 1.1"
expect "a second breakpoint is refused" "warning: failed to set breakpoint site"
expect "step over a call" "at main.c:$((call + 1))"
[ "$(grep -c "stop reason = step over" "$tmp/out")" = 3 ] || { echo "FAIL $label: steps"; cat "$tmp/out"; exit 1; }
[ "$(grep -c "stop reason = breakpoint 1.1" "$tmp/out")" -ge 3 ] || { echo "FAIL $label: the breakpoint did not survive stepping"; cat "$tmp/out"; exit 1; }
expect "step out lands on the return address" "stop reason = step out"
expect "exit code" "exited with status = 83"
if grep -A1 "^(lldb) continue\|^(lldb) next\|^(lldb) finish" "$tmp/out" | grep -q SIGTRAP; then
    echo "FAIL $label: a stop was not attributed"; cat "$tmp/out"; exit 1
fi
echo "pass  $label"

label="$device as silicon, -O1"
"$cc" -g -O1 -mcpu=$device -o "$tmp/g.hex" "$src"
session $device "$tmp/g.elf" \
    -o "breakpoint set --name sum_to" -o "continue" -o "bt" \
    -o "next" -o "next" -o "breakpoint delete 1" -o "finish" -o "continue"
expect "late stop known as its breakpoint" "stop reason = breakpoint 1.1"
expect "call stack" "g.elf\`main"
expect "step over" "stop reason = step over"
expect "step out" "stop reason = step out"
expect "exit code" "exited with status = 83"
echo "pass  $label"
