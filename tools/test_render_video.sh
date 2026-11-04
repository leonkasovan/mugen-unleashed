#!/bin/sh
# R02 gate: Video_OnResize projection idempotence with a real GL context.
#
# Usage: tools/test_render_video.sh       (from the repository root)
#   MUGEN_TEST_TARGET=win32|win64 selects the compiler and pointer width;
#   tools/test_qualify.sh sets it from --target. A missing GL context is a
#   SKIP (exit 77), never a pass.
# Artefacts go under build/render-video per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

TARGET="${MUGEN_TEST_TARGET:-win32}"
OUT=build/render-video

case "$TARGET" in
win32)
    CC="${CC:-gcc}"
    ARCHFLAGS="-m32"
    PKGCONF="${PKG_CONFIG:-pkg-config}"
    PLAT_INC="-Ithird_party/sdk/include"
    ;;
win64)
    CC="${CC:-/c/w64devkit/bin/gcc}"
    # The kit's gcc needs ITS OWN bin dir ahead of everything else on PATH for
    # cc1/as; an inherited i686 `as` rejects --64. Checking only that the dir
    # appears somewhere left i686 first when the caller's PATH had both, so the
    # selected compiler's directory is always prepended.
    case "$CC" in
    */*) CC_DIR=${CC%/*} ;;
    *) CC_DIR=$(command -v "$CC") ; CC_DIR=${CC_DIR%/*} ;;
    esac
    case ":$PATH:" in
    "$CC_DIR:"*) ;;
    *) export PATH="$CC_DIR:$PATH" ;;
    esac
    ARCHFLAGS="-m64"
    PKGCONF="${PKG_CONFIG:-/c/w64devkit/bin/pkg-config}"
    PLAT_INC=""
    ;;
*)
    echo "unsupported MUGEN_TEST_TARGET $TARGET (win32|win64)" >&2
    exit 2
    ;;
esac

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
"$OUT/test_render_video"
