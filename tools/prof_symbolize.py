#!/usr/bin/env python3
"""Symbolize a mugen.prof sampling dump (src/profiler.c).

The target only records program counters; this runs on the workstation against
the unstripped binary, the same split as `go tool pprof`: cheap collection on the
device, expensive symbolization off it.

Output, from the dump's own stacks:
  SELF       samples whose leaf program counter is that function
  INCLUSIVE  samples with the function anywhere on the stack (who paid for it)

Usage:
  python3 tools/prof_symbolize.py mugen.prof --binary build/linux-arm64-.../mugen-...
        [--addr2line aarch64-none-linux-gnu-addr2line] [--top 25]
"""

import argparse
import collections
import os
import subprocess
import sys


def parse_dump(path):
    """-> (header_lines, [(count, [pc, ...]), ...]) with pcs as ints."""
    header = []
    stacks = []
    with open(path, "r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                header.append(line)
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                count = int(parts[0])
                pcs = [int(p, 16) for p in parts[1:]]
            except ValueError:
                continue
            stacks.append((count, pcs))
    return header, stacks


def symbolize(pcs, binary, addr2line):
    """-> {pc: 'function  file:line'} using one batched addr2line call."""
    if not pcs:
        return {}
    unique = sorted(set(pcs))
    out = {}
    # addr2line takes its input on stdin, which avoids a command line per address.
    # No -i: inline expansion makes the line count per address variable, and a
    # fixed two-line stride then silently misaligns every name (the first version
    # of this script used -i and reported one function for 100% of the samples).
    cmd = [addr2line, "-f", "-C", "-e", binary]
    try:
        proc = subprocess.run(
            cmd,
            input="\n".join("0x%x" % pc for pc in unique),
            capture_output=True,
            text=True,
            timeout=600,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        sys.exit("cannot run %s: %s" % (addr2line, exc))
    if proc.returncode != 0:
        sys.exit("%s failed: %s" % (addr2line, proc.stderr.strip()))

    lines = proc.stdout.splitlines()
    if len(lines) != 2 * len(unique):
        sys.exit(
            "addr2line returned %d lines for %d addresses; expected %d"
            % (len(lines), len(unique), 2 * len(unique))
        )
    for i, pc in enumerate(unique):
        out[pc] = (lines[2 * i].strip(), lines[2 * i + 1].strip())
    return out


def short(func):
    """Trim the C++/static noise but keep the symbol readable."""
    name = func.split("(")[0]
    return name if len(name) <= 64 else name[:61] + "..."


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--binary", required=True, help="unstripped binary the dump came from")
    ap.add_argument(
        "--addr2line",
        default=os.environ.get("ADDR2LINE", "aarch64-none-linux-gnu-addr2line"),
    )
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit("no such binary: %s" % args.binary)

    header, stacks = parse_dump(args.dump)
    if not stacks:
        sys.exit("no samples in %s (was MUGEN_PROF=1 set?)" % args.dump)

    for line in header:
        print(line)
    total = sum(c for c, _ in stacks)
    print("samples: %d, distinct stacks: %d\n" % (total, len(stacks)))

    all_pcs = [pc for _, pcs in stacks for pc in pcs]
    sym = symbolize(all_pcs, args.binary, args.addr2line)

    self_hits = collections.Counter()
    incl_hits = collections.Counter()
    for count, pcs in stacks:
        leaf = sym.get(pcs[0], ("??", "??:0"))
        self_hits[short(leaf[0])] += count
        for name in {short(sym.get(pc, ("??", "??:0"))[0]) for pc in pcs}:
            incl_hits[name] += count

    def report(title, counter, note):
        print("%s  %s" % (title, note))
        print("  %8s %6s  %s" % ("samples", "pct", "function"))
        for name, n in counter.most_common(args.top):
            print("  %8d %5.1f%%  %s" % (n, 100.0 * n / total, name))
        print()

    report("SELF", self_hits, "(leaf frame: where the CPU actually was)")
    report("INCLUSIVE", incl_hits, "(anywhere on the stack: who caused it)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
