#!/bin/sh
# Builds and runs tools/test_sdl2shim.c against src/sdl2compat.c.
#
# The shim's pure functions (key translation, zoomSurface's palette handling)
# cannot be exercised by running the engine with -nosound: no key is pressed and
# no scaled text is drawn. This closes that gap without a display, and without
# needing the game data.
#
# Usage: tools/test_sdl2shim.sh        (from the repository root)
# Artefacts go under build/sdl2shim-test per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/sdl2shim-test
CC="${CC:-gcc}"
PKGS="sdl2"

mkdir -p "$OUT"

# -DMUGEN_SDL2 is what makes the shim compile; it includes mugen.h only for
# its own declarations, so the engine sources are not needed.
# -Dmain=SDL_main comes from sdl2.pc and would rename this test's main(); the
# engine build filters it out for the same reason (it keeps its own WinMain).
# glad.h is on the engine's include path because mugen.h puts glad before SDL;
# the shim only needs the header to exist, not to link GL.
# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    $(pkg-config --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_sdl2shim.c src/sdl2compat.c \
    -o "$OUT/test_sdl2shim" \
    $(pkg-config --libs --static $PKGS) \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm

echo "built $OUT/test_sdl2shim"
"$OUT/test_sdl2shim"
