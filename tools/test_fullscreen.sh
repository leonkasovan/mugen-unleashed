#!/bin/sh
# Production input + real SDL2 fullscreen transitions in both renderers.
# Requires a display (exit 77 when SDL video is unavailable).
# CC/CFLAGS/LDFLAGS/SDL_CFLAGS/SDL_LIBS/OUT/RUNNER can select a host or cross kit.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/tmp/fullscreen-test}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_mixer SDL2_ttf)}"
case "$($CC -dumpmachine)" in
    *mingw*) GL_LIBS="-lopengl32 -lglu32" ;;
    *) GL_LIBS="-lGL -lGLU" ;;
esac
SDL_LIBS="${SDL_LIBS:-$(pkg-config --libs sdl2 | sed 's/-lSDL2main//g; s/-mwindows//g') $GL_LIBS}"
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wno-unused-parameter -Werror=implicit-function-declaration -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -DSDL_MAIN_HANDLED"
FLAGS="$FLAGS -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL_CFLAGS"
case "$($CC -dumpmachine)" in *mingw*) FLAGS="$FLAGS -flto=auto" ;; esac
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
$CC $FLAGS tools/test_fullscreen.c src/video.c src/keyboard.c src/events.c src/sdl2video.c src/sdl2keys.c src/sdl2surface.c third_party/glad-2.1/glad.c "$OUT/globals.o" -Wl,--gc-sections ${LDFLAGS:-} $SDL_LIBS -lm -o "$OUT/test_fullscreen"
${RUNNER:-} "$OUT/test_fullscreen"
