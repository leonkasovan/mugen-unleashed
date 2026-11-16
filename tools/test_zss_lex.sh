#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/zss-lex}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -DMUGEN_MEMPROF_NO_WRAP \
 -DSDL_MAIN_HANDLED -Isrc $(pkg-config --cflags sdl2 libpng) \
 -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src \
 -Ithird_party/libresample/include src/zss_lex.c tools/test_zss_lex.c \
 ${LDFLAGS:-} -o "$OUT/test"
${RUNNER:-} "$OUT/test"
