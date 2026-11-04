#!/bin/sh
# Host gate for the renderer selection policy (RENDERING_PLAN.md 10.1/12.2).
# Pure logic: no GL, no SDL, no engine globals.
#
# Usage: tools/test_render_policy.sh
#   MUGEN_TEST_TARGET=win32|win64 selects compiler, kit and pointer width.
# Artefacts go under build/render-policy per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-policy
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -Isrc \
    tools/test_render_policy.c src/render_policy.c \
    -o "$OUT/test_render_policy"

echo "built $OUT/test_render_policy"
run_harness "$OUT/test_render_policy"
