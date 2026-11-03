#!/bin/sh
# Host gate for the GLES draw path's CPU math (RENDERING_PLAN.md V3 / 12.5):
# MVP construction and quad vertex/texcoord generation. No GL context needed.
#
# Usage: tools/test_gl_mvp.sh
# Artefacts go under build/render-gl per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-gl
CC="${CC:-gcc}"

mkdir -p "$OUT"

"$CC" -m32 -std=gnu11 -O1 -g -Wall \
    -Isrc \
    tools/test_gl_mvp.c src/gl_mvp.c \
    -o "$OUT/test_gl_mvp" -lm

echo "built $OUT/test_gl_mvp"
"$OUT/test_gl_mvp"
