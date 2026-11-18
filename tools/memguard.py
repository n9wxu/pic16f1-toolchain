#!/usr/bin/env python3
"""Run a command and kill any process under it that uses too much memory.

    tools/memguard.py [--max-mb N] [--total-mb N] [--timeout S] -- command ...

macOS does not enforce address-space limits (setrlimit fails), so a compiler
that allocates without bound takes the whole machine down with it. This
watches the command and everything it starts, a few times a second, and
kills a process whose resident memory passes --max-mb (default 1500), or the
largest one when together they pass --total-mb (default 8000). Each kill is
reported on standard error. The exit status is the command's.
"""
import argparse
import os
import signal
import subprocess
import sys
import time


def descendants(root):
    """{pid: resident KiB} for every live process below root."""
    out = subprocess.run(["ps", "-axo", "pid=,ppid=,rss="], capture_output=True, text=True).stdout
    kids, rss = {}, {}
    for line in out.splitlines():
        try:
            pid, ppid, kb = (int(x) for x in line.split())
        except ValueError:
            continue
        kids.setdefault(ppid, []).append(pid)
        rss[pid] = kb
    found, todo = {}, [root]
    while todo:
        p = todo.pop()
        for k in kids.get(p, []):
            if k not in found:
                found[k] = rss[k]
                todo.append(k)
    return found


def kill(pid, why):
    try:
        cmd = subprocess.run(["ps", "-o", "command=", "-p", str(pid)],
                             capture_output=True, text=True).stdout.strip()
        os.kill(pid, signal.SIGKILL)
    except OSError:
        return
    if len(cmd) > 400:
        cmd = cmd[:150] + " ... " + cmd[-250:]
    sys.stderr.write(f"memguard: killed {pid} ({why}): {cmd}\n")
    sys.stderr.flush()


def check(root, max_kb, total_kb):
    procs = descendants(root)
    for pid, kb in procs.items():
        if kb > max_kb:
            kill(pid, f"{kb // 1024} MB resident, limit {max_kb // 1024}")
            procs[pid] = 0
    if sum(procs.values()) > total_kb:
        pid = max(procs, key=procs.get)
        kill(pid, f"largest process, {sum(procs.values()) // 1024} MB in total, limit {total_kb // 1024}")


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--max-mb", type=int, default=1500)
    ap.add_argument("--total-mb", type=int, default=8000)
    ap.add_argument("--timeout", type=float, default=0, help="seconds; 0 for none")
    ap.add_argument("--interval", type=float, default=0.2)
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not cmd:
        ap.error("no command")
    child = subprocess.Popen(cmd)
    start = time.time()
    try:
        while True:
            try:
                return child.wait(timeout=args.interval)
            except subprocess.TimeoutExpired:
                pass
            check(os.getpid(), args.max_mb * 1024, args.total_mb * 1024)
            if args.timeout and time.time() - start > args.timeout:
                sys.stderr.write(f"memguard: timeout after {args.timeout:.0f} s\n")
                for pid in descendants(os.getpid()):
                    kill(pid, "timeout")
                return 124
    except KeyboardInterrupt:
        for pid in descendants(os.getpid()):
            kill(pid, "interrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main())
