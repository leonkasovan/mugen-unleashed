#!/usr/bin/env python3
"""Compare two mugen.log files the way this project validates a build.

The reconstruction is checked against the original by running both binaries on
the same scenario and diffing mugen.log (docs/ANALYSIS.md, "Validation").
The two logs never match byte for byte, because they contain numbers that legitimately
differ: timings, the session RNG seed, cache timings, palette reservation ids
(hexadecimal), frame counts. This script normalises those and then compares.

Usage:
    compare_logs.py ORIGINAL.log REBUILT.log [--show N]

Exit status 0 when the logs match after normalisation, 1 when they differ.
Differences are printed as a multiset (order-insensitive), because the two
binaries can emit the same lines in a different order (the cache/loading path
of players is not deterministic).

Deliberately NOT normalised: the words of a message. A different message is a
real difference; only the numbers inside it are ignored.
"""

import argparse
import re
import sys
from collections import Counter

# Lines that are pure environment noise and say nothing about behaviour.
IGNORE_PATTERNS = (
    r"^Command line:",          # argv[0] differs (mugen.exe vs mugen_unleashed.exe)
    r"^Session RNG seed is",    # seeded from the clock unless -sessionseed is given
    r"^Log file successfully closed",
)


def normalise(path, exe_names):
    """Return the list of normalised non-empty line fragments of a log."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    for name in exe_names:
        text = text.replace(name, "EXE")

    out = []
    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        if any(re.search(p, line) for p in IGNORE_PATTERNS):
            continue
        # Split on ':' as well: message punctuation and ellipses vary in where
        # the binary breaks a line, and the reconstructed code sometimes emits
        # the same text in one piece where the original emitted two.
        for part in re.split(r"[:\n]", line):
            part = part.strip()
            if not part:
                continue
            # Order matters. Alle numerics go first, so that the hex rule below
            # only sees tokens that were hexadecimal in the original text: a
            # bare number such as the 1 in "kfm1.def" must become N, not HEX.
            # Order matters, and the hex forms go first: once "\d+" has eaten
            # the digits of "(1F)" all that is left is "(F)", which no later
            # rule recognises.
            part = re.sub(r"0[xX][0-9A-Fa-f]+", "HEX", part)
            # Palette reservation ids print as (1F), (NB), (1):
            # a short uppercase hex number inside parentheses.
            part = re.sub(r"\([0-9A-F]{1,8}\)", "(HEX)", part)
            part = re.sub(r"\d+(?:\.\d+)?", "N", part)
            out.append(part)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original", help="mugen.log of the original mugen.exe")
    ap.add_argument("rebuilt", help="mugen.log of the rebuilt executable")
    ap.add_argument("--show", type=int, default=20,
                    help="how many differing fragments to print per side (default 20)")
    args = ap.parse_args(argv[1:])

    a = normalise(args.original, ("mugen_unleashed.exe", "mugen.exe"))
    b = normalise(args.rebuilt, ("mugen_unleashed.exe", "mugen.exe"))

    ca, cb = Counter(a), Counter(b)
    only_a, only_b = ca - cb, cb - ca
    n_a, n_b = sum(only_a.values()), sum(only_b.values())

    print("fragments: original %d, rebuilt %d" % (len(a), len(b)))
    if not n_a and not n_b:
        print("MATCH: logs are identical after numeric normalisation")
        return 0

    print("DIFFER: %d fragment(s) only in the original, %d only in the rebuilt"
          % (n_a, n_b))
    if only_a:
        print("\n-- only in ORIGINAL (%s) --" % args.original)
        for text, count in list(only_a.items())[: args.show]:
            print("  %dx %s" % (count, text))
    if only_b:
        print("\n-- only in REBUILT (%s) --" % args.rebuilt)
        for text, count in list(only_b.items())[: args.show]:
            print("  %dx %s" % (count, text))
    print("\nIf a difference is legitimate (a fixed bug, a new feature), say so "
          "explicitly; otherwise it is a regression.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
