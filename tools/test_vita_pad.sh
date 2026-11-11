#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=build/tmp/vita-pad-test
CC="${CC:-gcc}"
. tools/lib_target.sh
PKGS="sdl2"
mkdir -p "$OUT"
$CC $ARCHFLAGS -ffunction-sections -fdata-sections -std=gnu11 -O1 -g -Wall \
    -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src $PLAT_INC \
    -Ithird_party/libresample/include \
    ${SDL_CFLAGS:-$("$PKGCONF" --cflags $PKGS)} \
    tools/test_vita_pad.c src/pads.c src/keyboard.c src/events.c src/sdl2video.c src/sdl2keys.c src/sdl2surface.c \
    -Wl,--gc-sections ${LDFLAGS:-} -o "$OUT/test_vita_pad" \
    ${SDL_LIBS:-$("$PKGCONF" --libs $SDL_LINK_MODE $PKGS)} \
    $TEST_SYS_LIBS -lpthread -lm
echo "built $OUT/test_vita_pad"
"$OUT/test_vita_pad"
