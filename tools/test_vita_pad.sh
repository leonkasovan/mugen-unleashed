#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=build/vita-pad-test
CC="${CC:-gcc}"
PKGS="sdl2"
mkdir -p "$OUT"
$CC -m32 -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    -Ithird_party/libresample/include \
    $(pkg-config --cflags $PKGS | sed 's/-Dmain=SDL_main//') \
    tools/test_vita_pad.c src/pads.c src/keyboard.c src/events.c src/sdl2compat.c \
    -o "$OUT/test_vita_pad" \
    $(pkg-config --libs --static $PKGS | sed 's/-lSDL2main//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lpthread -lm
echo "built $OUT/test_vita_pad"
"$OUT/test_vita_pad"
