#!/bin/sh
# Mock SDL driver, production pads.c. No SDL runtime or physical device needed.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/pad-detection}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_mixer SDL2_ttf)}"
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wno-unused-parameter -Werror=implicit-function-declaration -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
FLAGS="$FLAGS -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15"
FLAGS="$FLAGS -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL_CFLAGS"
case "$($CC -dumpmachine)" in *mingw*) FLAGS="$FLAGS -flto=auto" ;; esac
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
$CC $FLAGS -Wextra $SAN tools/test_pad_detection.c "$OUT/globals.o" -Wl,--gc-sections ${LDFLAGS:-} -lm -o "$OUT/test_pad_detection"
${RUNNER:-} "$OUT/test_pad_detection"
