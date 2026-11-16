#!/bin/sh
# Decode every entry of a mixed WAV/Ogg bank with the real mixer and resampler.
# Usage: SANITIZE=address,undefined sh tools/test_soundcache.sh /absolute/bank.snd
set -eu
cd "$(dirname "$0")/.."
[ "$#" = 1 ] || { echo "usage: $0 /absolute/bank.snd" >&2; exit 2; }
OUT="${OUT:-$PWD/build/tmp/test-soundcache}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
. tools/lib_target.sh
SAN_FLAGS=""
if [ -n "${SANITIZE:-}" ]; then SAN_FLAGS="-fsanitize=$SANITIZE -fno-omit-frame-pointer"; fi
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -DSDL_MAIN_HANDLED \
    -DMUGEN_MEMPROF_NO_WRAP -DLIBRESAMPLE_STATIC $SAN_FLAGS \
    -ffunction-sections -fdata-sections -Isrc -Ithird_party/glad-2.1 \
    -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src \
    -Ithird_party/libresample/include -Ithird_party/libresample/src \
    $("$PKGCONF" --cflags sdl2 SDL2_mixer libpng) \
    tools/test_soundcache.c src/sound.c src/soundcache.c src/keyedarray.c src/stream.c src/wav.c src/bgmstream.c \
    third_party/minizip-0.15/unzip.c third_party/libresample/src/*.c \
    -Wl,--gc-sections $("$PKGCONF" --libs $SDL_LINK_MODE sdl2 SDL2_mixer zlib) \
    -lm -o "$OUT/test_soundcache"
run_harness "$OUT/test_soundcache" "$1"
