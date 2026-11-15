#!/usr/bin/env python3
"""Symbolize a mugen.memprof call-site dump (src/memprof.c).

Same split as tools/prof_symbolize.py: the target records raw program counters
(one per allocation site, with live/peak bytes), and this runs on the
workstation against the same unstripped binary to turn them into names.

The dump records `# base 0x...` because a Windows DLL/EXE is relocated per run;
the PCs are absolute, so the base is subtracted before addr2line is asked.

Usage:
  python3 tools/memprof_symbolize.py mugen.memprof \
      --binary build/mingw-sdl2/mugen-mingw-win32-sdl2.exe \
      [--addr2line i686-w64-mingw32-addr2line] [--top 10]

If the binary is stripped, use the unstripped one from the build tree (the
default output of the Makefile is unstripped; "make install" strips).
"""

import argparse
import os
import subprocess
import sys


def parse_dump(path):
    """-> (base, [(live, peak, allocs, frees, pc), ...]) with pc as int."""
    base = 0
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                parts = line[1:].split()
                if len(parts) >= 2 and parts[0] == "base":
                    try:
                        base = int(parts[1], 16)
                    except ValueError:
                        pass
                continue
            parts = line.split()
            if len(parts) < 5:
                continue
            try:
                live = int(parts[0])
                peak = int(parts[1])
                allocs = int(parts[2])
                frees = int(parts[3])
                pc = int(parts[4], 16)
            except ValueError:
                continue
            rows.append((live, peak, allocs, frees, pc))
    return base, rows


def symbolize(pcs, binary, addr2line):
    """-> {pc: (function, file:line)} using one batched addr2line call."""
    if not pcs:
        return {}
    unique = sorted(set(pcs))
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
    out = {}
    for i, pc in enumerate(unique):
        out[pc] = (lines[2 * i].strip(), lines[2 * i + 1].strip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--binary", required=True)
    ap.add_argument("--addr2line", default=os.environ.get("ADDR2LINE", "addr2line"))
    ap.add_argument(
        "--image-base",
        type=lambda s: int(s, 0),
        default=0x400000,
        help="PE link-time ImageBase of the binary (default 0x400000, MinGW's)",
    )
    ap.add_argument("--top", type=int, default=10)
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit("no such binary: %s" % args.binary)
    if not os.path.exists(args.dump):
        sys.exit("no such dump: %s (was MUGEN_MEMPROF=1 set?)" % args.dump)

    base, rows = parse_dump(args.dump)
    if not rows:
        sys.exit("no allocation sites in %s" % args.dump)

    # The dump's PCs are absolute runtime addresses. A symbolizer needs the
    # address as it appears in the image file, i.e. the PE link-time ImageBase
    # plus the offset inside the image. Both are needed: MinGW links at
    # 0x400000, but Windows relocates the image to `base` at run time.
    image_base = args.image_base
    pcs = [image_base + (pc - base) for _, _, _, _, pc in rows]
    sym = symbolize(pcs, args.binary, args.addr2line)

    total_live = sum(live for live, _, _, _, _ in rows)
    total_peak = sum(peak for _, peak, _, _, _ in rows)
    print("sites: %d  base: 0x%x" % (len(rows), base))
    print("live total: %.2f MB   peak total: %.2f MB\n"
          % (total_live / (1024.0 * 1024.0), total_peak / (1024.0 * 1024.0)))

    ranked = sorted(rows, key=lambda r: r[0], reverse=True)
    print("%-4s %10s %10s %8s %8s  %s" % ("#", "live KB", "peak KB", "allocs", "frees", "function"))
    print("-" * 96)
    for i, (live, peak, allocs, frees, pc) in enumerate(ranked[: args.top], 1):
        file_addr = image_base + (pc - base)
        func, where = sym.get(file_addr, ("??", "??:0"))
        name = func.split("(")[0]
        if not name or name == "??":
            name = "?? (0x%x) %s" % (file_addr, where)
        print("%-4d %10.1f %10.1f %8d %8d  %s"
              % (i, live / 1024.0, peak / 1024.0, allocs, frees, name))


if __name__ == "__main__":
    main()
