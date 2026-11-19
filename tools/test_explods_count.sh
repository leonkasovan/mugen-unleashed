#!/bin/sh
# Equivalence test for the Explods_Count fast path (src/explod.c).
#
# Explods_Count stopped scanning all `capacity` slots and now stops once it has
# seen `list->count` live items. That is exact only while `count` equals the
# number of items with `used != 0`. This test checks the algorithm against the
# original scan over exhaustive and randomised layouts, and separately proves
# the invariant is load-bearing (an inconsistent count must make them diverge).
#
# Host-only, no engine, no GL, no device:
#   sh tools/test_explods_count.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/tmp/test_explods_count"
mkdir -p "$OUT"
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tools/test_explods_count.c" -o "$OUT/test_explods_count"
"$OUT/test_explods_count"
