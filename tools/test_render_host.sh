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
# Artefacts go under build/render-host per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

TARGET="${MUGEN_TEST_TARGET:-win32}"
OUT=build/render-host

case "$TARGET" in
win32)
    CC="${CC:-gcc}"
    ARCHFLAGS="-m32"
    EXPECT_BITS=32
    PKGCONF="${PKG_CONFIG:-pkg-config}"
    PLAT_INC="-Ithird_party/sdk/include"
    ;;
win64)
    CC="${CC:-/c/w64devkit/bin/gcc}"
    # The kit's gcc is a wrapper that needs ITS OWN bin dir ahead of everything
    # else on PATH to find cc1/as for the right target. Merely checking that
    # the dir appears somewhere was not enough: with i686/bin first, the
    # inherited i686 `as` still won and rejected --64 ("as: unrecognized
    # option `--64'"). Prepend the directory the chosen compiler lives in, so
    # the assembler always matches the compiler, whatever the incoming order.
    case "$CC" in
    */*) CC_DIR=${CC%/*} ;;
    *) CC_DIR=$(command -v "$CC") ; CC_DIR=${CC_DIR%/*} ;;
    esac
    case ":$PATH:" in
    "$CC_DIR:"*) ;;
    *) export PATH="$CC_DIR:$PATH" ;;
    esac
    ARCHFLAGS="-m64"
    EXPECT_BITS=64
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
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src $PLAT_INC \
    $("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
    tools/test_render_host.c src/sprite.c src/keyedarray.c src/softrender.c \
    -o "$OUT/test_render_host" \
    $("$PKGCONF" --libs --static sdl2 | sed 's/-lSDL2main//') \
    -lwinmm -lm

echo "built $OUT/test_render_host"
result="$("$OUT/test_render_host")"
printf '%s\n' "$result"
if ! printf '%s\n' "$result" | grep -q "^abi: $EXPECT_BITS-bit build$"; then
    echo "FAIL: harness is not a $EXPECT_BITS-bit build" >&2
    exit 1
fi
