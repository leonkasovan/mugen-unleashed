#!/bin/sh
# Blend-factor gate: the GLES blend state must implement the engine's equation.
# Pure host test, no GPU. Usage: bash tools/test_blend_math.sh
set -eu
cd "$(dirname "$0")/.."
OUT=build/tmp/test-blend-math
. tools/lib_target.sh
mkdir -p "$OUT"
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall -Isrc \
    tools/test_blend_math.c -o "$OUT/test_blend_math"
run_harness "$OUT/test_blend_math"
