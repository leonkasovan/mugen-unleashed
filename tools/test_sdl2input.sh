#!/bin/sh
# Builds and runs tools/test_sdl2input.c against the engine's real input
# modules: pads.c, keyboard.c and events.c plus the SDL2 shim.
#
# These paths (pad slot mapping, key delivery, UTF-8 text decoding) cannot be
# reached by running the engine and diffing mugen.log: no key is pressed and no
# joystick is attached. The pad test uses SDL's virtual joystick, so no hardware
# is needed.
#
# Usage: tools/test_sdl2input.sh     (from the repository root)
# Artefacts go under build/sdl2input-test per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/sdl2input-test
CC="${CC:-gcc}"
PKGS="sdl2 SDL2_ttf SDL2_mixer zlib libpng16"

mkdir -p "$OUT"

# -Dmain=SDL_main comes from sdl2.pc and would rename this test's main(); the
# engine build filters it out for the same reason (it keeps its own WinMain).
# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    -Ithird_party/libresample/include \
    $(pkg-config --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_sdl2input.c src/pads.c src/keyboard.c src/events.c src/sdl2compat.c \
    -o "$OUT/test_sdl2input" \
    $(pkg-config --libs --static $PKGS | sed 's/-lSDL2main//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm

echo "built $OUT/test_sdl2input"
"$OUT/test_sdl2input"
