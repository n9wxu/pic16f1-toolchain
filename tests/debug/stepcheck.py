#!/usr/bin/env python3
"""stepcheck.py <lldb> <sim> <elf> <device> [steps]: stop in sum_to three calls
deep, then step by instruction and check after every one that each calling
frame of sum_to shows the right n (5 in the outermost, one less in each
frame inward). Frame 0 is exempt: its own frame is being made or undone."""
import re, subprocess, sys, time
lldb, sim, elf, device = sys.argv[1:5]
steps = int(sys.argv[5]) if len(sys.argv) > 5 else 150
simp = subprocess.Popen([sim, "gdb", "--device", device, "--port", "0", elf], stderr=subprocess.PIPE, text=True)
port = re.search(r"port (\d+)", simp.stderr.readline()).group(1)
cmds = ["gdb-remote " + port, "breakpoint set --name sum_to --condition 'n == 3'", "continue", "breakpoint delete 1"]
for _ in range(steps):
    cmds += ["stepi", "bt"]
args = [lldb, "--batch"]
for c in cmds:
    args += ["-o", c]
out = subprocess.run(args + [elf], capture_output=True, text=True, timeout=600).stdout
simp.kill()
bad = checked = 0
for block in out.split("(lldb) bt\n")[1:]:
    frames = re.findall(r"frame #(\d+): (0x[0-9a-f]+) \S+`(\w+)(?:\(n='\\x(\w\w)'\))?", block.split("(lldb)")[0])
    if not frames:
        continue
    checked += 1
    callers = [f for f in frames[1:] if f[2] == "sum_to"]
    want = 5
    ok = frames[-2][2] == "main" if len(frames) >= 2 else False
    for f in reversed(callers):
        if f[3] == "" or int(f[3], 16) != want:
            ok = False
        want -= 1
    if not ok:
        bad += 1
        if bad <= 3:
            print("wrong at pc", frames[0][1], [(f[2], f[3]) for f in frames])
print(f"{checked} steps checked, {bad} wrong")
sys.exit(1 if bad or checked < steps // 2 else 0)
