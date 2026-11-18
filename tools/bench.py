#!/usr/bin/env python3
"""Run every reference and toolchain-built binary on the simulator.

For each program under tests/programs and each build under the given roots
(default: every directory in reference/), this checks that the UART output
matches tests/programs/<prog>/expected.txt and that the exit code is 0, and
records flash words, RAM bytes and instruction cycles.

Layout of a build root:  <root>/<program>/<level>/<program>.hex + info.json

Usage: tools/bench.py [--sim PATH] [--json OUT] [--markdown OUT] [root ...]
Exit status is 1 if any binary fails.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_sim(explicit):
    if explicit:
        return explicit
    for cand in ("sim/target/release/pic16f1-sim", "sim/target/debug/pic16f1-sim"):
        p = os.path.join(ROOT, cand)
        if os.path.exists(p):
            return p
    sys.exit("simulator not built: run `cargo build --release` in sim/")


def run_one(sim, hexfile, max_cycles):
    out = subprocess.run([sim, "run", "--json", "--max-cycles", str(max_cycles), hexfile],
                         capture_output=True, text=True)
    try:
        return json.loads(out.stdout)
    except json.JSONDecodeError:
        return {"halt": "sim-error", "detail": out.stderr.strip(), "exit_code": -1,
                "cycles": 0, "uart": "", "program_words": 0, "max_stack_depth": 0}


def comparison(results):
    """Size and speed of each toolchain's -Os build next to the first one's."""
    tools = []
    for r in results:
        if r["toolchain"] not in tools:
            tools.append(r["toolchain"])
    if len(tools) < 2:
        return ""
    by = {(r["toolchain"], r["program"]): r for r in results if r["level"] == "Os" and r["pass"]}
    base = tools[0]
    out = ["", f"### -Os builds compared with {base}", ""]
    head = "| Program |"
    rule = "|---|"
    for t in tools:
        head += f" {t} words | {t} cycles |"
        rule += "---:|---:|"
    for t in tools[1:]:
        head += f" {t} size ratio | {t} cycle ratio |"
        rule += "---:|---:|"
    out += [head, rule]
    totals = {t: 0 for t in tools}
    progs = sorted({r["program"] for r in results})
    for p in progs:
        if any((t, p) not in by for t in tools):
            continue
        row = f"| {p} |"
        for t in tools:
            row += f' {by[(t, p)]["flash_words"]} | {by[(t, p)]["cycles"]} |'
            totals[t] += by[(t, p)]["flash_words"]
        for t in tools[1:]:
            row += (f' {by[(t, p)]["flash_words"] / by[(base, p)]["flash_words"]:.2f} |'
                    f' {by[(t, p)]["cycles"] / by[(base, p)]["cycles"]:.2f} |')
        out.append(row)
    row = "| **total** |"
    for t in tools:
        row += f" {totals[t]} | |"
    for t in tools[1:]:
        row += f" {totals[t] / totals[base]:.2f} | |" if totals[base] else " | |"
    out.append(row)
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*")
    ap.add_argument("--sim")
    ap.add_argument("--json")
    ap.add_argument("--markdown")
    ap.add_argument("--max-cycles", type=int, default=200_000_000)
    ap.add_argument("--programs", default=os.path.join(ROOT, "tests", "programs"),
                    help="directory of test programs (default tests/programs)")
    args = ap.parse_args()
    sim = find_sim(args.sim)
    roots = args.roots or sorted(
        os.path.join(ROOT, "reference", d) for d in os.listdir(os.path.join(ROOT, "reference"))
        if os.path.isdir(os.path.join(ROOT, "reference", d)))
    progs = sorted(os.listdir(args.programs))

    results, failures = [], 0
    for root in roots:
        tool = os.path.basename(os.path.normpath(root))
        for prog in progs:
            pdir = os.path.join(root, prog)
            if not os.path.isdir(pdir):
                continue
            expected = open(os.path.join(args.programs, prog, "expected.txt")).read()
            for level in sorted(os.listdir(pdir)):
                hexfile = os.path.join(pdir, level, prog + ".hex")
                if not os.path.exists(hexfile):
                    continue
                info = {}
                ipath = os.path.join(pdir, level, "info.json")
                if os.path.exists(ipath):
                    info = json.load(open(ipath))
                r = run_one(sim, hexfile, args.max_cycles)
                ok = r["halt"] == "sleep" and r["exit_code"] == 0 and r["uart"] == expected
                why = ""
                if not ok:
                    failures += 1
                    if r["halt"] != "sleep":
                        why = f'{r["halt"]}: {r.get("detail", "")}'
                    elif r["uart"] != expected:
                        why = f'output mismatch: got {r["uart"]!r}'
                    else:
                        why = f'exit code {r["exit_code"]}'
                results.append({
                    "toolchain": tool, "program": prog, "level": level, "pass": ok, "why": why,
                    "flash_words": r["program_words"], "ram_bytes": info.get("data_bytes"),
                    "cycles": r["cycles"], "max_stack_depth": r["max_stack_depth"],
                })

    lines = ["| Toolchain | Program | Level | Result | Flash (words) | RAM (bytes) | Cycles | Stack depth |",
             "|---|---|---|---|---:|---:|---:|---:|"]
    for r in results:
        ram = "" if r["ram_bytes"] is None else r["ram_bytes"]
        lines.append(f'| {r["toolchain"]} | {r["program"]} | {r["level"]} | '
                     f'{"pass" if r["pass"] else "FAIL"} | {r["flash_words"]} | {ram} | '
                     f'{r["cycles"]} | {r["max_stack_depth"]} |')
    table = "\n".join(lines) + "\n"
    table += comparison(results)
    if args.markdown:
        open(args.markdown, "w").write(table)
    if args.json:
        json.dump(results, open(args.json, "w"), indent=1)
    if not args.markdown:
        print(table)
    for r in results:
        if not r["pass"]:
            print(f'FAIL {r["toolchain"]} {r["program"]} {r["level"]}: {r["why"]}', file=sys.stderr)
    print(f"{len(results) - failures}/{len(results)} passed", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
