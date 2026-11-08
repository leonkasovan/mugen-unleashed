#!/bin/sh
# R02 gate: Video_OnResize projection idempotence with a real GL context.
#
# Usage: tools/test_render_video.sh       (from the repository root)
#   MUGEN_TEST_TARGET=win32|win64 selects the compiler and pointer width;
#   tools/test_qualify.sh sets it from --target. A missing GL context is a
#   SKIP (exit 77), never a pass.
# Artefacts go under build/tmp/render-video per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-video
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -DMUGEN_SDL2_DIAG \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src $PLAT_INC \
    $("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
    tools/test_render_video.c src/video.c src/sdl2compat.c src/sdl2keys.c src/sdl2surface.c \
    third_party/glad-2.1/glad.c \
    -o "$OUT/test_render_video" \
    $("$PKGCONF" --libs --static sdl2 | sed 's/-lSDL2main//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm

echo "built $OUT/test_render_video"
run_harness "$OUT/test_render_video"
