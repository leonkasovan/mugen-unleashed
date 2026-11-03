#!/bin/sh
# R04/R05 host gates: layout contract + true-colour rasterizer.
# No video device needed (surfaces live in memory only).
#
# Usage: tools/test_render_host.sh       (from the repository root)
# Artefacts go under build/render-host per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-host
CC="${CC:-gcc}"

mkdir -p "$OUT"

# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    $(pkg-config --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
    tools/test_render_host.c src/sprite.c src/keyedarray.c src/softrender.c \
    -o "$OUT/test_render_host" \
    $(pkg-config --libs --static sdl2 | sed 's/-lSDL2main//') \
    -lwinmm -lm

echo "built $OUT/test_render_host"
"$OUT/test_render_host"
