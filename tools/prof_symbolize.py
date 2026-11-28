#!/usr/bin/env python3
"""Symbolize a mugen.prof sampling dump (src/profiler.c).

The target only records program counters; this runs on the workstation against
the unstripped binary, the same split as `go tool pprof`: cheap collection on the
device, expensive symbolization off it.

Output, from the dump's own stacks:
  SELF       samples whose leaf program counter is that function
  INCLUSIVE  samples with the function anywhere on the stack (who paid for it)

Dumps written since the map was added carry "# map: <line>" comments holding
/proc/self/maps. Those name the module each program counter belongs to, so a
sample inside libc or the GL driver is reported as that library instead of "??",
and --lib can give such a module its own symbols:

  --lib libmali.so:0=/path/to/libmali.so
  --lib libc.so.6=/path/to/libc.so.6

Usage:
  python3 tools/prof_symbolize.py mugen.prof --binary build/linux-arm64-.../mugen-...
        [--addr2line aarch64-none-linux-gnu-addr2line] [--top 25]
        [--lib NAME=PATH ...]
"""

import argparse
import collections
import os
import subprocess
import sys


def parse_dump(path):
    """-> (header_lines, [(count, [pc, ...]), ...], mappings, exe_path).

    mappings is [(start, end, file_offset, module_name), ...], empty for dumps
    written before the map was recorded. exe_path is the program's own path from
    the map's "# exe:" line, or None when it is absent.
    """
    header = []
    stacks = []
    mappings = []
    exe_path = None
    with open(path, "r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                header.append(line)
                if line.startswith("# exe:"):
                    exe_path = line[len("# exe:"):].strip()
                entry = parse_map_line(line[len("# map:"):].strip())
                if entry is not None:
                    mappings.append(entry)
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
    return header, stacks, mappings, exe_path


def parse_map_line(line):
    """'/proc/self/maps' line -> (start, end, offset, module) or None.

    The module is the file's basename, or a "[heap]"-style bracket name; a line
    with neither (an anonymous mapping) yields None because it names nothing.
    """
    parts = line.split()
    if len(parts) < 2 or "-" not in parts[0]:
        return None
    try:
        start_s, end_s = parts[0].split("-", 1)
        start, end = int(start_s, 16), int(end_s, 16)
    except ValueError:
        return None
    offset = int(parts[2], 16) if len(parts) > 2 else 0
    if len(parts) >= 6:
        module = os.path.basename(parts[5])
    elif len(parts) == 5 and parts[4].startswith("["):
        module = parts[4]
    else:
        return None
    return (start, end, offset, module)


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


def find_mapping(mappings, pc):
    """The (start, end, offset, module) mapping a pc falls in, or None."""
    for entry in mappings:
        if entry[0] <= pc < entry[1]:
            return entry
    return None


def module_symbols(pcs, mappings, binary, libs, addr2line):
    """-> {pc: (function, location)} for pcs outside `binary`.

    A pc in one of --lib's files is rebased to that file's address (pc - the
    mapping's load address + its file offset) and symbolized normally. Any other
    mapped pc is named after its module, which is the honest answer: the sample
    was in libc, or the GPU driver, not in this program.
    """
    out = {}
    by_path = {}
    for pc in pcs:
        entry = find_mapping(mappings, pc)
        if entry is None:
            out[pc] = ("<unmapped>", "?:0")
            continue
        start, _end, offset, module = entry
        path = libs.get(module)
        if path is None:
            out[pc] = ("%s+??" % module, "?:0")
            continue
        if path not in by_path:
            by_path[path] = []
        by_path[path].append((pc, pc - start + offset))
    for path, items in by_path.items():
        rebased = [addr for _pc, addr in items]
        syms = symbolize(rebased, path, addr2line)
        for pc, addr in items:
            out[pc] = syms.get(addr, ("%s+%#x" % (os.path.basename(path), addr), "?:0"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--binary", required=True, help="unstripped binary the dump came from")
    ap.add_argument(
        "--addr2line",
        default=os.environ.get("ADDR2LINE", "aarch64-none-linux-gnu-addr2line"),
    )
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument(
        "--lib",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="give the module NAME (as it appears in /proc/self/maps) symbols "
        "from the unstripped file at PATH; repeatable",
    )
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit("no such binary: %s" % args.binary)
    libs = {}
    for spec in args.lib:
        name, sep, path = spec.partition("=")
        if not sep or not name or not path:
            sys.exit("--lib wants NAME=PATH, not %r" % spec)
        if not os.path.exists(path):
            sys.exit("no such library: %s" % path)
        libs[name] = path

    header, stacks, mappings, exe_path = parse_dump(args.dump)
    if not stacks:
        sys.exit("no samples in %s (was MUGEN_PROF=1 set?)" % args.dump)

    for line in header:
        if not line.startswith("# map:"):
            print(line)
    total = sum(c for c, _ in stacks)
    print("samples: %d, distinct stacks: %d" % (total, len(stacks)))
    if mappings:
        print("mapped modules: %d" % len(mappings))
    else:
        print("no map in the dump: pcs outside the binary cannot be named")
    print()

    all_pcs = [pc for _, pcs in stacks for pc in pcs]
    # Split the engine's own pcs from the libraries'. The program's mapping comes
    # from the dump's "# exe:" line, not from the local file's basename: the
    # binary is usually copied to the target under another name, and a basename
    # that matches nothing would send every library pc to addr2line, which cannot
    # name an address that is not in the file it was given.
    # A dump without a map predates this: symbolize all of it against the binary.
    exe_names = set()
    if exe_path:
        exe_names.add(os.path.basename(exe_path))
    else:
        exe_names.add(os.path.basename(args.binary))
        exe_names.add(os.path.basename(mappings[0][3]) if mappings else "")
    exe_ranges = [(lo, hi) for lo, hi, _off, module in mappings if module in exe_names]
    if exe_ranges:
        in_set = set()
        for lo, hi in exe_ranges:
            in_set.update(pc for pc in all_pcs if lo <= pc < hi)
        in_exe = sorted(in_set)
        outside = sorted(set(all_pcs) - in_set)
    else:
        in_exe, outside = sorted(set(all_pcs)), []
    sym = symbolize(in_exe, args.binary, args.addr2line)
    sym.update(module_symbols(outside, mappings, args.binary, libs, args.addr2line))

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
