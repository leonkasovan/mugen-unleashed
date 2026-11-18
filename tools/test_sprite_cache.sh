#!/bin/sh
# Linux host regression; uses the real PNG decoder and stream close contract.
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/sprite-cache}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
${CC:-cc} -std=gnu11 -O1 -g -ffunction-sections -fdata-sections \
 -DMUGEN_MEMPROF_NO_WRAP -DANDROID_PORT -DSDL_MAIN_HANDLED \
 -Isrc -Iport/android/app/src/main/jni/src -Ithird_party/glad-2.1 \
 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
 $(pkg-config --cflags sdl2 libpng16) tools/test_sprite_cache.c \
 src/sprite.c src/spritecodec.c src/misc.c src/pngio.c src/stream.c \
 -Wl,--gc-sections -Wl,--wrap=free $(pkg-config --libs sdl2 libpng16) -lz -lm -o "$OUT/test"
MUGEN_SFF_EAGER=0 "$OUT/test"
