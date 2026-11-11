#!/bin/sh
# CC/CFLAGS select the host ABI. SANITIZE=1 enables ASan/UBSan.
set -eu
cd "$(dirname "$0")/.."
out="$PWD/build/tmp/configmerge-tests"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_ttf SDL2_mixer)}"
flags="${CFLAGS:-} $SDL_CFLAGS -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -Isrc"
flags="$flags -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
flags="$flags -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
san=""
[ "${SANITIZE:-0}" != 1 ] || san="-fsanitize=address,undefined -fno-omit-frame-pointer"
${CC:-cc} $flags -c src/globals.c -o "$out/globals.o"
${CC:-cc} $flags -w -c third_party/minizip-0.15/unzip.c -o "$out/unzip.o"
${CC:-cc} $flags $san tools/test_configmerge.c src/configmerge.c src/ini.c \
    src/stream.c src/util.c src/compat_linux.c "$out/globals.o" "$out/unzip.o" \
    -Wl,--gc-sections ${LDFLAGS:-} -lm -lz -o "$out/probe"
${RUNNER:-} "$out/probe"
