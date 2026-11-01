#!/bin/sh
# Builds and runs tools/test_sdl2audio.c against the engine's real bgmthread.c
# and sdlmixercompat.c.
#
# These paths cannot be reached by the -nosound runs used elsewhere: the custom
# music hook is never installed and the mixer is never opened. The test drives
# the hook callback directly and opens the mixer if the machine has an audio
# device, reporting a SKIP rather than passing silently when it does not.
#
# Usage: tools/test_sdl2audio.sh     (from the repository root)
# Artefacts go under build/sdl2audio-test per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/sdl2audio-test
CC="${CC:-gcc}"
PKGS="sdl2 SDL2_ttf SDL2_mixer zlib libpng16"

mkdir -p "$OUT"

# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -DPTW32_STATIC_LIB \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    -Ithird_party/libresample/include -Ithird_party/sdk/include \
    $(pkg-config --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_sdl2audio.c src/bgmthread.c src/sdlmixercompat.c src/stream.c third_party/minizip-0.15/unzip.c \
    -o "$OUT/test_sdl2audio" \
    $(pkg-config --libs --static $PKGS | sed 's/-lSDL2main//;s/-mwindows//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lpthread -lm

echo "built $OUT/test_sdl2audio"
"$OUT/test_sdl2audio"
