#!/bin/sh
# Actual filesystem fixtures; SANITIZE=1 enables ASan/UBSan. No game data needed.
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
OUT="${OUT:-$PWD/build/tmp/test-selectupdate}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
FAULTS=""
case "$TARGET" in
linux-*) FAULTS="-DSELECTUPDATE_FAULTS -Wl,--wrap=rename,--wrap=fwrite,--wrap=fflush,--wrap=fclose,--wrap=malloc,--wrap=realloc" ;;
esac
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS $CFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -DSDL_MAIN_HANDLED -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
    $("$PKGCONF" --cflags sdl2 libpng) $SAN $FAULTS \
    tools/test_selectupdate.c src/selectupdate.c -o "$OUT/probe"
python3 tools/test_selectupdate.py "$OUT/probe" "$OUT/fixtures"
