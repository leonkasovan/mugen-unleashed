#!/bin/sh
# Primitive colour/blend gate: the GLES primitives must decode colours and blend
# the way the desktop's prim.c does. Pure host test, no GPU.
# Usage: bash tools/test_prim_math.sh
set -eu
cd "$(dirname "$0")/.."
OUT=build/tmp/test-prim-math
. tools/lib_target.sh
mkdir -p "$OUT"
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -DSDL_MAIN_HANDLED -std=gnu11 -O1 -g -Wall -Isrc \
    tools/test_prim_math.c -o "$OUT/test_prim_math"
run_harness "$OUT/test_prim_math"
