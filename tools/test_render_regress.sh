#!/bin/sh
# M0 regression gates for R01-R05 that run without a display.
#
# Usage: tools/test_render_regress.sh      (from the repository root)
#   MUGEN_TEST_TARGET=win32|win64 selects the compiler and pointer width;
#   tools/test_qualify.sh sets it from --target. A harness SKIP exits 77,
#   which the runner records as SKIP rather than PASS.
# Artefacts go under build/tmp/render-regress per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-regress
. tools/lib_target.sh
PKGS="sdl2"

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -DSDL_MAIN_HANDLED -DMUGEN_SDL2_DIAG \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src $PLAT_INC \
    $("$PKGCONF" --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_render_regress.c src/sdl2video.c src/sdl2keys.c src/sdl2surface.c \
    ${LDFLAGS:-} -o "$OUT/test_render_regress" \
    $("$PKGCONF" --libs $SDL_LINK_MODE $PKGS | sed 's/-lSDL2main//') \
    $TEST_SYS_LIBS -lm

echo "built $OUT/test_render_regress"
# No SDL_VIDEODRIVER override: the static kit only ships the `windows`
# driver (dummy is unavailable), and a hidden window works headless here.
run_harness "$OUT/test_render_regress"
