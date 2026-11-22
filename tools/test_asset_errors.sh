#!/bin/sh
# Host regression for asset bounds and failed allocation cleanup.
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-asset-errors}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
. tools/lib_target.sh
INC="-Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lz4 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
case "$TARGET" in
win32) INC="$INC -Ibuild/sysroots/win32-sysroot/usr/include -Ibuild/sysroots/win32-sysroot/usr/include/SDL2" ;;
win64) INC="$INC -Ibuild/sysroots/win64-sysroot/usr/include -Ibuild/sysroots/win64-sysroot/usr/include/SDL2" ;;
*) INC="$INC $("$PKGCONF" --cflags sdl2 SDL2_mixer libpng)" ;;
esac
SAN_FLAGS=""
if [ -n "${SANITIZE:-}" ]; then
    SAN_FLAGS="-fsanitize=$SANITIZE -fno-omit-frame-pointer"
fi
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -DLIBRESAMPLE_STATIC -DSDL_MAIN_HANDLED -ffunction-sections -fdata-sections $SAN_FLAGS $INC \
    tools/test_asset_errors.c src/sff.c src/sprite.c src/spritecodec.c src/sound.c third_party/lz4/lz4.c -Wl,--gc-sections -lm -o "$OUT/test_asset_errors"
for case in valid-codecs pcx-palette pcx-overflow sprite-short sprite-rle-overflow sound-null sound-realloc sound-chunk; do
    run_harness "$OUT/test_asset_errors" "$case"
done
echo "PASS: asset bounds and failure cleanup"
