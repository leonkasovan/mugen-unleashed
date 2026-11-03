#!/bin/sh
# Host gate for the renderer selection policy (RENDERING_PLAN.md 10.1/12.2).
# Pure logic: no GL, no SDL, no engine globals.
#
# Usage: tools/test_render_policy.sh
# Artefacts go under build/render-policy per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-policy
CC="${CC:-gcc}"

mkdir -p "$OUT"

"$CC" -m32 -std=gnu11 -O1 -g -Wall \
    -Isrc \
    tools/test_render_policy.c src/render_policy.c \
    -o "$OUT/test_render_policy"

echo "built $OUT/test_render_policy"
"$OUT/test_render_policy"
