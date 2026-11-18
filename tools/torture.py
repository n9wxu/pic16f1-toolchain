#!/usr/bin/env python3
"""Run a directory of single-file C tests through the toolchain and simulator.

Each test is a C file whose main() returns 0 (or calls exit(0)) on success
and calls abort() on failure, as in the GCC torture suite's "execute" tests.
The tests themselves are not part of this repository:

    git clone --depth 1 --filter=blob:none --sparse https://github.com/gcc-mirror/gcc build/gcc-torture
    git -C build/gcc-torture sparse-checkout set gcc/testsuite/gcc.c-torture/execute
    tools/torture.py build/gcc-torture/gcc/testsuite/gcc.c-torture/execute

Results per optimization level:
    pass         ran and reported success
    fail         ran and reported failure (abort or a non-zero exit code)
    fault        the simulator stopped it (stack overflow, ran off the program)
    timeout      did not finish within the cycle limit
    crash        the compiler crashed: always a compiler bug
    unsupported  did not compile or link (missing library, too big, C the
                 compiler rejects)
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELAX = ["-std=gnu17", "-fhosted", "-w", "-Wno-error=implicit-function-declaration", "-Wno-error=implicit-int",
         "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types",
         "-Wno-error=return-mismatch", "-Wno-error=incompatible-function-pointer-types"]


# DejaGnu directives in the test source. Options that change the meaning of
# the C (such as -fwrapv) must be honoured; a test that states it needs a
# 32-bit int or other things this target lacks is not applicable.
PASS_THROUGH = ("-fwrapv", "-fno-strict-aliasing", "-fsigned-char", "-funsigned-char",
                "-fno-builtin", "-fgnu89-inline", "-fno-delete-null-pointer-checks",
                "-fno-strict-overflow", "-fwrapv-pointer", "-fno-tree-vrp", "-fno-inline")
NOT_APPLICABLE = ("int32plus", "int32", "size32plus", "size20plus", "ptr32plus", "trampolines",
                  "alloca", "int128", "float128", "dfp", "label_values_32", "mmap", "untyped_assembly")


def directives(path):
    """Returns (extra options, reason the test does not apply or None)."""
    opts, reason = [], None
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return opts, None
    for m in re.finditer(r"dg-(?:additional-)?options\s+\"([^\"]*)\"", text):
        for o in m.group(1).split():
            if o.startswith(PASS_THROUGH):
                opts.append(o)
    for m in re.finditer(r"dg-require-effective-target\s+(\w+)", text):
        if m.group(1) in NOT_APPLICABLE:
            reason = "requires " + m.group(1)
    return opts, reason


def run_test(path, level, cc, sim, harness, max_cycles, tmp):
    name = os.path.splitext(os.path.basename(path))[0]
    extra, reason = directives(path)
    if reason:
        return name, level, "unsupported", reason
    out = os.path.join(tmp, f"{name}-{level}.hex")
    inc = os.path.join(os.path.dirname(harness), "include")
    cmd = [cc, f"-{level}", "-isystem", inc] + RELAX + extra + ["-o", out, path, harness]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return name, level, "crash", "compiler timed out"
    if r.returncode != 0:
        err = r.stderr
        if "is not supported on PIC16F1" in err or "are not supported on PIC16F1" in err:
            return name, level, "unsupported", "feature not supported by the backend"
        if "PLEASE submit a bug report" in err or "Assertion failed" in err or "LLVM ERROR" in err \
                or "UNREACHABLE" in err:
            lines = [l for l in err.splitlines() if "Assertion" in l or "LLVM ERROR" in l
                     or "UNREACHABLE" in l or "Cannot select" in l or "fatal error" in l]
            return name, level, "crash", (lines[0] if lines else err.strip().splitlines()[0])[:300]
        lines = [l for l in err.splitlines() if "error" in l]
        return name, level, "unsupported", (lines[0] if lines else err.strip()[:200])[:300]
    r = subprocess.run([sim, "run", "--json", "--max-cycles", str(max_cycles), out],
                       capture_output=True, text=True)
    for ext in (".hex", ".elf"):
        try:
            os.remove(out[:-4] + ext)
        except OSError:
            pass
    try:
        res = json.loads(r.stdout)
    except json.JSONDecodeError:
        return name, level, "fault", r.stderr.strip()[:200]
    if res["halt"] == "sleep":
        if res["exit_code"] == 0:
            return name, level, "pass", ""
        return name, level, "fail", f'exit code {res["exit_code"]}'
    if res["halt"] == "max-cycles":
        return name, level, "timeout", ""
    return name, level, "fault", f'{res["halt"]}: {res["detail"]}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tests", help="directory of .c files")
    ap.add_argument("--levels", default="O0,Os")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--max-cycles", type=int, default=30_000_000)
    ap.add_argument("--json", help="write full results here")
    ap.add_argument("--only", help="comma-separated test names")
    ap.add_argument("--cc", default=os.environ.get("PIC16F1_CC", os.path.join(ROOT, "toolchain", "bin", "pic16f1-cc")))
    ap.add_argument("--sim", default=os.path.join(ROOT, "sim", "target", "release", "pic16f1-sim"))
    args = ap.parse_args()

    files = sorted(os.path.join(args.tests, f) for f in os.listdir(args.tests) if f.endswith(".c"))
    if args.only:
        want = set(args.only.split(","))
        files = [f for f in files if os.path.splitext(os.path.basename(f))[0] in want]
    levels = args.levels.split(",")
    harness = os.path.join(ROOT, "tests", "torture", "harness.c")
    results = []
    with tempfile.TemporaryDirectory() as tmp, \
            concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_test, f, lv, args.cc, args.sim, harness, args.max_cycles, tmp)
                   for f in files for lv in levels]
        for fut in concurrent.futures.as_completed(futures):
            results.append(fut.result())
    results.sort()

    for lv in levels:
        counts = {}
        for _, level, status, _ in results:
            if level == lv:
                counts[status] = counts.get(status, 0) + 1
        ran = sum(counts.get(k, 0) for k in ("pass", "fail", "fault", "timeout"))
        rate = 100.0 * counts.get("pass", 0) / ran if ran else 0.0
        print(f"-{lv}: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
              + f"  |  {rate:.1f}% of the {ran} that built pass")
    if args.json:
        json.dump([{"test": n, "level": l, "status": s, "detail": d} for n, l, s, d in results],
                  open(args.json, "w"), indent=1)
    bad = [r for r in results if r[2] in ("crash", "fail", "fault", "timeout")]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
