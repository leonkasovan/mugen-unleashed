#!/bin/sh
# Native/cross SDL2 harness; bitness follows lib_target, never the host label.
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
out="${OUT:-$PWD/build/tmp/test_sdl2helpers/$TARGET}"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
$CC $ARCHFLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -DSDL_MAIN_HANDLED \
    -ffunction-sections -fdata-sections -Isrc -Ithird_party/glad-2.1 \
    -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
    -Ithird_party/libpng-1.2.38 ${SDL_CFLAGS:-$("$PKGCONF" --cflags sdl2)} \
    tools/test_sdl2helpers.c src/sdl2keys.c src/sdl2surface.c \
    -Wl,--gc-sections ${LDFLAGS:-} ${SDL_LIBS:-$("$PKGCONF" --libs sdl2 | sed 's/-lSDL2main//g;s/-mwindows//g')} \
    -lm -o "$out/probe"
SDL_VIDEODRIVER=dummy ${RUNNER:-} "$out/probe"
