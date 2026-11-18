#!/bin/sh
# Host regression for decoder result alignment and resampler failure paths.
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-bgmstream}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
. tools/lib_target.sh
INC="-Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
case "$TARGET" in
win32) INC="$INC -Ibuild/sysroots/win32-sysroot/usr/include -Ibuild/sysroots/win32-sysroot/usr/include/SDL2" ;;
win64) INC="$INC -Ibuild/sysroots/win64-sysroot/usr/include -Ibuild/sysroots/win64-sysroot/usr/include/SDL2" ;;
*) INC="$INC $(pc_cflags sdl2 libpng16)" ;;
esac
SAN_FLAGS=""
if [ -n "${SANITIZE:-}" ]; then
    SAN_FLAGS="-fsanitize=$SANITIZE -fno-omit-frame-pointer"
fi
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -DSDL_MAIN_HANDLED -DMUGEN_MEMPROF_NO_WRAP -ffunction-sections -fdata-sections $SAN_FLAGS $INC \
    tools/test_bgmstream.c src/bgmstream.c -Wl,--gc-sections -o "$OUT/test_bgmstream"
run_harness "$OUT/test_bgmstream"
