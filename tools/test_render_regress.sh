#!/bin/sh
# M0 regression gates for R01-R05 that run without a display.
#
# Usage: tools/test_render_regress.sh      (from the repository root)
# Artefacts go under build/render-regress per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-regress
CC="${CC:-gcc}"
PKGS="sdl2"

mkdir -p "$OUT"

# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -DMUGEN_SDL2_DIAG \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    $(pkg-config --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_render_regress.c src/sdl2compat.c src/sdl2keys.c src/sdl2surface.c \
    -o "$OUT/test_render_regress" \
    $(pkg-config --libs --static $PKGS | sed 's/-lSDL2main//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm

echo "built $OUT/test_render_regress"
# No SDL_VIDEODRIVER override: the static kit only ships the `windows`
# driver (dummy is unavailable), and a hidden window works headless here.
"$OUT/test_render_regress"
