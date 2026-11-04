#!/bin/sh
# ES01 structural gate, part 1 (host, no GPU): GLES shader dialect check.
#
# Usage: tools/test_gles_shader.sh
#   MUGEN_TEST_TARGET=win32|win64 selects compiler and pointer width.
# Artefacts go under build/render-gles per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-gles
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    tools/test_gles_shader.c \
    -o "$OUT/test_gles_shader"

echo "built $OUT/test_gles_shader"
run_harness "$OUT/test_gles_shader" data
