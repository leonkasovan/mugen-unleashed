#!/bin/sh
# Host gate for the GLES draw path's CPU math (RENDERING_PLAN.md V3 / 12.5):
# MVP construction and quad vertex/texcoord generation. No GL context needed.
#
# Usage: tools/test_gl_mvp.sh
#   MUGEN_TEST_TARGET=win32|win64 selects compiler and pointer width.
# Artefacts go under build/tmp/render-gl per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-gl
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -Isrc \
    tools/test_gl_mvp.c src/gl_mvp.c \
    -o "$OUT/test_gl_mvp" -lm

echo "built $OUT/test_gl_mvp"
run_harness "$OUT/test_gl_mvp"
