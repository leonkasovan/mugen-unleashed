#!/bin/sh
# Gate for Ini_CountGroups keeping the selected group intact.
#
# Stage_Load counts the [bg] groups while [StageInfo] is selected. When the
# count restored only the group index, the cursor stayed at the end of the
# file and the reads after it (autoturn, resetbg, xscale, yscale) silently
# failed. autoturn then stayed 0, which disables the engine's auto-turn loop:
# players kept facing the wrong way after crossing up. The harness compiles
# src/ini.c with stdio-based stubs; no game data and no window are needed.
#
# Usage: sh tools/test_ini_groups.sh
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-ini-groups}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

. tools/lib_target.sh

case "$TARGET" in
win32) SYSROOT="$PWD/build/sysroots/win32-sysroot" ;;
win64) SYSROOT="$PWD/build/sysroots/win64-sysroot" ;;
linux-x64|linux-arm64) SYSROOT="" ;;
esac

INC="-Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src"
INC="$INC -Ithird_party/libresample/include"
if [ -n "$SYSROOT" ]; then
    INC="$INC -I$SYSROOT/usr/include -I$SYSROOT/usr/include/SDL2"
    SDL_CFLAGS=""
else
    SDL_CFLAGS=$("$PKGCONF" --cflags-only-I sdl2)
fi

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -DSDL_MAIN_HANDLED $INC $SDL_CFLAGS \
    tools/test_ini_groups.c src/ini.c \
    -o "$OUT/test_ini_groups"

run_harness "$OUT/test_ini_groups" "$OUT/test.ini"
