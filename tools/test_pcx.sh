#!/bin/sh
# PCX regression tests against real stream code; Linux fault injection uses fopencookie.
set -eu
cd "$(dirname "$0")/.."
OUT="$PWD/build/tmp/test-pcx"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
san=""
if [ "${SANITIZE:-0}" = 1 ]; then san="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
${CC:-cc} -std=gnu11 -g -O1 $san -ffunction-sections -fdata-sections \
 -DMUGEN_MEMPROF_NO_WRAP=1 -DSDL_MAIN_HANDLED -Isrc -Ithird_party/glad-2.1 \
 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
 $(pkg-config --cflags sdl2 libpng) tools/test_pcx.c src/sff.c src/stream.c \
 -Wl,--gc-sections,--wrap=calloc -o "$OUT/probe"
"$OUT/probe"
timeout 5s "$OUT/probe" io-error
