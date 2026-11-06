#!/bin/sh
# Optional Linux EGL/GLES smoke test with actual SDL2/GLES declarations.
# Requires SDL2/add-on and EGL development headers, Mesa or another EGL driver.
# Exit 77 means no usable EGL display. This is not Android/Vita device evidence.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/gles-smoke}"
mkdir -p "$OUT/tmp" "$OUT/data/gles"
export TMPDIR="$OUT/tmp"
SDL2_CFLAGS="${SDL2_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_mixer SDL2_ttf)}"
EGL_CFLAGS="${EGL_CFLAGS:-$(pkg-config --cflags egl)}"
EGL_LIBS="${EGL_LIBS:-$(pkg-config --libs egl)}"
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wno-unused-parameter -ffunction-sections -fdata-sections"
FLAGS="$FLAGS -DANDROID_PORT -DMUGEN_GLES -DMUGEN_ANDROID_SW -Isrc"
FLAGS="$FLAGS -Iport/android/app/src/main/jni/src -Ithird_party/glad-es-3.0"
FLAGS="$FLAGS -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
FLAGS="$FLAGS $SDL2_CFLAGS $EGL_CFLAGS"
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
$CC $FLAGS tools/test_gles_smoke.c src/gl_mvp.c third_party/glad-es-3.0/glad.c \
    "$OUT/globals.o" -Wl,--gc-sections ${LDFLAGS:-} $EGL_LIBS -lm -ldl -o "$OUT/test_gles_smoke"
cp res/shaders/gles/* "$OUT/data/gles/"
(cd "$OUT"; ./test_gles_smoke)
