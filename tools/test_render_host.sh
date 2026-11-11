#!/bin/sh
# R04/R05 host gates: layout contract + true-colour rasterizer.
# No video device needed (surfaces live in memory only).
#
# Usage: tools/test_render_host.sh       (from the repository root)
#   MUGEN_TEST_TARGET=win32|win64 selects the compiler and pointer width;
#   tools/test_qualify.sh sets it from --target. win32 uses the i686 kit with
#   -m32, win64 uses w64devkit with -m64. The harness prints its pointer width
#   and the script fails when it does not match the target, so a 64-bit
#   qualification cannot be met by a 32-bit build.
# Artefacts go under build/tmp/render-host per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-host
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src $PLAT_INC \
    $("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
    tools/test_render_host.c src/sprite.c src/keyedarray.c src/softrender.c \
    ${LDFLAGS:-} -o "$OUT/test_render_host" \
    $("$PKGCONF" --libs $SDL_LINK_MODE sdl2 | sed 's/-lSDL2main//') \
    $TEST_SYS_LIBS -lm

echo "built $OUT/test_render_host"
run_harness "$OUT/test_render_host"
