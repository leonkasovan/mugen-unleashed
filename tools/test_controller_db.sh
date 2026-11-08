#!/bin/sh
# Real SDL2 parser, virtual devices, production startup/hotplug and fixups.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/tmp/controller-db-test}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 SDL2_mixer SDL2_ttf)}"
SDL_LIBS="${SDL_LIBS:-$(pkg-config --libs sdl2 | sed 's/-lSDL2main//g; s/-mwindows//g')}"
case "${TEST_PLATFORM:-desktop}" in
    desktop) PLATFORM="-DMUGEN_SDL2" ;;
    android) PLATFORM="-DANDROID_PORT -DMUGEN_ANDROID_SW -Iport/android/app/src/main/jni/src" ;;
    *) echo "TEST_PLATFORM must be desktop or android" >&2; exit 2 ;;
esac
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wno-unused-parameter -Werror=implicit-function-declaration -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
FLAGS="$FLAGS $PLATFORM -DSDL_MAIN_HANDLED -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL_CFLAGS"
case "$($CC -dumpmachine)" in *mingw*) FLAGS="$FLAGS -flto=auto" ;; esac
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
$CC $FLAGS $SAN tools/test_controller_db.c "$OUT/globals.o" -Wl,--gc-sections ${LDFLAGS:-} $SDL_LIBS -lm -o "$OUT/test_controller_db"
for scenario in missing empty invalid platform startup hotplug hotplug-no-scan reload; do
    mkdir -p "$OUT/$scenario/data"
    rm -f "$OUT/$scenario/data/gamecontrollerdb.txt"
    (cd "$OUT/$scenario" && ${RUNNER:-} "$OUT/test_controller_db" "$scenario")
done
