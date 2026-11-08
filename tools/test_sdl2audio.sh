#!/bin/sh
# Builds and runs tools/test_sdl2audio.c against the engine's real bgmthread.c
# and sdlmixercompat.c.
#
# These paths cannot be reached by the -nosound runs used elsewhere: the custom
# music hook is never installed and the mixer is never opened. The test drives
# the hook callback directly and opens the mixer if the machine has an audio
# device, reporting a SKIP rather than passing silently when it does not.
#
# Usage: tools/test_sdl2audio.sh     (from the repository root)
# Artefacts go under build/tmp/sdl2audio-test per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

# The qualifier exports MUGEN_TEST_TARGET; use its compiler, kit and ABI.
. tools/lib_target.sh
OUT="build/tmp/sdl2audio-test/$TARGET"
PKGS="sdl2 SDL2_mixer zlib"
PLATFORM_SOURCE=""
mkdir -p "$OUT/tmp"
export TMPDIR="$PWD/$OUT/tmp"

SDL_CFLAGS="${SDL_CFLAGS:-$("$PKGCONF" --cflags $PKGS | sed 's/-Dmain=SDL_main//g')}"
SDL_LIBS="${SDL_LIBS:-$("$PKGCONF" --libs --static $PKGS | sed 's/-lSDL2main//g;s/-mwindows//g')}"
# A native Linux compiler is useful for host regression runs with CC/PKG_CONFIG
# overrides. Windows kits still supply the actual win32/win64 qualification.
case "$("$CC" -dumpmachine)" in
*w64-mingw32*) SYSTEM_LIBS="-lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lpthread" ;;
*) SYSTEM_LIBS="-pthread"; PLATFORM_SOURCE="src/compat_linux.c" ;;
esac

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine))"
# shellcheck disable=SC2086
$CC $ARCHFLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -Wall \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED -DPTW32_STATIC_LIB -DTEST_EXPECT_BITS=$EXPECT_BITS \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38 \
    -Ithird_party/libresample/include $PLAT_INC $SDL_CFLAGS \
    tools/test_sdl2audio.c src/bgmthread.c src/sdlmixercompat.c src/stream.c third_party/minizip-0.15/unzip.c $PLATFORM_SOURCE \
    -o "$OUT/test_sdl2audio" ${LDFLAGS:-} $SDL_LIBS $SYSTEM_LIBS -lm

echo "built $OUT/test_sdl2audio"
run_harness "$OUT/test_sdl2audio"
