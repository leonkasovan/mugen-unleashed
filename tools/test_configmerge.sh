#!/bin/sh
# CC/CFLAGS select the host ABI. SANITIZE=1 enables ASan/UBSan.
set -eu
cd "$(dirname "$0")/.."
out="$PWD/build/tmp/configmerge-tests"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_ttf SDL2_mixer)}"
# Some kits' sdl2.pc emits -Dmain=SDL_main, which renames this harness's main()
# and then collides with SDL_main.h's own declaration of SDL_main (the build
# failed with "conflicting types for 'SDL_main'"). SDL_MAIN_HANDLED below is
# the supported way to keep main(), so the .pc file's rename is stripped rather
# than trusted. A kit that does not emit it is unaffected by the substitution.
SDL_CFLAGS=$(printf '%s\n' "$SDL_CFLAGS" | sed 's/-Dmain=SDL_main//')
flags="${CFLAGS:-} $SDL_CFLAGS -DSDL_MAIN_HANDLED -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -Isrc"
flags="$flags -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
flags="$flags -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
# This harness does not link src/memprof.c, so the malloc-family redirects
# mugen.h installs have no definitions behind them (the link failed on
# Memprof_Free, Memprof_Calloc and Memprof_Realloc). tools/lib_target.sh makes
# the same switch for the other standalone harnesses.
flags="$flags -DMUGEN_MEMPROF_NO_WRAP=1"
san=""
[ "${SANITIZE:-0}" != 1 ] || san="-fsanitize=address,undefined -fno-omit-frame-pointer"
${CC:-cc} $flags -c src/globals.c -o "$out/globals.o"
${CC:-cc} $flags -w -c third_party/minizip-0.15/unzip.c -o "$out/unzip.o"
${CC:-cc} $flags $san tools/test_configmerge.c src/configmerge.c src/ini.c \
    src/stream.c src/util.c src/compat_linux.c "$out/globals.o" "$out/unzip.o" \
    -Wl,--gc-sections ${LDFLAGS:-} -lm -lz -o "$out/probe"
${RUNNER:-} "$out/probe"
