#!/bin/sh
# Optional real SDL2/GLES context recovery test, independent of game assets.
# SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 works with host Mesa.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/tmp/sdl2-gles-context}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SDL2_CFLAGS="${SDL2_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_mixer SDL2_ttf)}"
SDL2_LIBS="${SDL2_LIBS:-$(pkg-config --libs sdl2)}"
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wno-unused-parameter -ffunction-sections -fdata-sections"
FLAGS="$FLAGS -DMUGEN_SDL2 -DMUGEN_GLES -DSDL_MAIN_HANDLED -Isrc -Ithird_party/glad-es-3.0"
FLAGS="$FLAGS -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL2_CFLAGS"
$CC $FLAGS tools/test_sdl2_gles_context.c third_party/glad-es-3.0/glad.c \
    -Wl,--gc-sections ${LDFLAGS:-} $SDL2_LIBS -lm -ldl -o "$OUT/test_sdl2_gles_context"
"$OUT/test_sdl2_gles_context"
