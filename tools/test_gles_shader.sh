#!/bin/sh
# ES01 structural gate, part 1 (host, no GPU): GLES shader dialect check.
#
# Usage: tools/test_gles_shader.sh       (from the repository root)
# Artefacts go under build/render-gles per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-gles
CC="${CC:-gcc}"

mkdir -p "$OUT"

$CC -m32 -std=gnu11 -O1 -g -Wall \
    tools/test_gles_shader.c \
    -o "$OUT/test_gles_shader"

echo "built $OUT/test_gles_shader"
"$OUT/test_gles_shader" data
