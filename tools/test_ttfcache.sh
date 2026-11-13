#!/bin/sh
# Gate for the rasterized-TrueType-line cache (src/ttfcache.c).
#
# The cache exists to skip SDL_ttf work on repeated lines, so what must not go
# wrong is a hit returning the wrong pixels: a stale or colliding key would draw
# the wrong text, colour or size. The rasterizer is stubbed with key-derived,
# deterministic bytes, so tools/test_ttfcache.c tests the bookkeeping (identity,
# key separation, failures, eviction) rather than SDL_ttf. Host test, no font.
#
# Usage: sh tools/test_ttfcache.sh
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-ttfcache}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

. tools/lib_target.sh

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -Isrc \
    tools/test_ttfcache.c src/ttfcache.c \
    -o "$OUT/test_ttfcache"

run_harness "$OUT/test_ttfcache"
