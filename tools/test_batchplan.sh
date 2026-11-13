#!/bin/sh
# Gate for the sprite batch planner (src/batchplan.c).
#
# The planner picks draw-call boundaries and texture-unit slots; both fail
# silently (reordered layers, a sprite sampled from its neighbour's texture), so
# tools/test_batchplan.c pins the rules on the host. No GL, no engine.
#
# Usage: sh tools/test_batchplan.sh
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-batchplan}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

. tools/lib_target.sh

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -Isrc \
    tools/test_batchplan.c src/batchplan.c \
    -o "$OUT/test_batchplan"

run_harness "$OUT/test_batchplan"
