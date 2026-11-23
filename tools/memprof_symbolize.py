#!/usr/bin/env python3
"""Symbolize a mugen.memprof call-site dump (src/memprof.c).

Same split as tools/prof_symbolize.py: the target records raw program counters
(one per allocation site, with live/peak bytes), and this runs on the
workstation against the same unstripped binary to turn them into names.

The dump records `# base 0x...` because a Windows DLL/EXE is relocated per run;
the PCs are absolute, so the base is subtracted before addr2line is asked.

Usage:
  python3 tools/memprof_symbolize.py mugen.memprof \
      --binary build/mingw-sdl2/mugen-win32.exe \
      [--addr2line i686-w64-mingw32-addr2line] [--top 10]

If the binary is stripped, use the unstripped one from the build tree (the
default output of the Makefile is unstripped; "make install" strips).
"""

import argparse
import os
import subprocess
import sys


def parse_dump(path):
    """-> (abi, base, [(live, peak, allocs, frees, pc), ...]) with pc as int."""
    abi = "pe"
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
                elif len(parts) >= 2 and parts[0] == "abi":
                    abi = parts[1]
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
    return abi, base, rows


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

    abi, base, rows = parse_dump(args.dump)
    if not rows:
        sys.exit("no allocation sites in %s" % args.dump)

    # Runtime address -> the address addr2line wants.
    #
    # PE: the image is relocated per run, so the file address is
    #     ImageBase + (pc - runtime_base).
    # ELF: the dump's base is the load bias (0 for a non-PIE executable, the
    #     bias for a PIE), so pc - base is already the link-time address and
    #     adding a PE ImageBase would be wrong.
    if abi == "elf":
        to_file_addr = [pc - base for _, _, _, _, pc in rows]
    else:
        to_file_addr = [args.image_base + (pc - base) for _, _, _, _, pc in rows]
    sym = symbolize(to_file_addr, args.binary, args.addr2line)

    total_live = sum(live for live, _, _, _, _ in rows)
    total_peak = sum(peak for _, peak, _, _, _ in rows)
    print("sites: %d  abi: %s  base: 0x%x" % (len(rows), abi, base))
    print("live total: %.2f MB   peak total: %.2f MB\n"
          % (total_live / (1024.0 * 1024.0), total_peak / (1024.0 * 1024.0)))

    ranked = sorted(rows, key=lambda r: r[0], reverse=True)
    print("%-4s %10s %10s %8s %8s  %s" % ("#", "live KB", "peak KB", "allocs", "frees", "function"))
    print("-" * 96)
    for i, (live, peak, allocs, frees, pc) in enumerate(ranked[: args.top], 1):
        file_addr = (pc - base) if abi == "elf" else (args.image_base + (pc - base))
        func, where = sym.get(file_addr, ("??", "??:0"))
        name = func.split("(")[0]
        if not name or name == "??":
            name = "?? (0x%x) %s" % (file_addr, where)
        print("%-4d %10.1f %10.1f %8d %8d  %s"
              % (i, live / 1024.0, peak / 1024.0, allocs, frees, name))


if __name__ == "__main__":
    main()
