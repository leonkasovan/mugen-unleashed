#!/bin/sh
# Batch-rendering profiler gate (src/batchprof.c, MUGEN_DEBUG only).
#
# The profiler is compiled out of every release build, so no other harness
# exercises it. This one builds it with MUGEN_DEBUG, stubs the SDL counter with
# a fixed 1 GHz clock and asserts the counters and the averaging arithmetic.
# Pure host test, no GPU and no SDL library needed.
#
# Usage: bash tools/test_batchprof.sh [--target win32|win64]
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-batchprof}"
. tools/lib_target.sh
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

# SDL headers come from the same dependency tree the Makefile resolves by
# itself on Windows (build/sysroots/<target>-sysroot), not from whatever the
# shell happens to have: the kit ships no SDL at all, and pointing pkg-config at
# MSYS2's copy would compile this test against a different SDL than the build.
# A native pkg-config.exe needs Windows spelling, so cygpath -m converts.
case "$TARGET" in
win*)
    SYSROOT_TREE="$PWD/build/sysroots/$TARGET-sysroot"
    if [ -d "$SYSROOT_TREE/usr/lib/pkgconfig" ]; then
        PC_DIR=$(cygpath -m "$SYSROOT_TREE/usr/lib/pkgconfig" 2>/dev/null || printf '%s' "$SYSROOT_TREE/usr/lib/pkgconfig")
        SDL_CFLAGS=$(PKG_CONFIG_LIBDIR="$PC_DIR" "$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//')
    else
        SDL_CFLAGS=$("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//')
    fi
    ;;
*)
    SDL_CFLAGS=$("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//')
    ;;
esac

# batchprof.c includes mugen.h, which pulls the engine's headers; the includes
# below are the same set the other host harnesses use. Only batchprof.o is
# linked, so no engine object and no SDL library is needed -- the two SDL
# functions it calls are supplied by the test itself.
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -DSDL_MAIN_HANDLED -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra \
    -Wno-unused-parameter -DMUGEN_DEBUG \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38 \
    -Ithird_party/libresample/include -Ithird_party/glad-es-3.0 \
    $PLAT_INC $SDL_CFLAGS \
    tools/test_batchprof.c src/batchprof.c \
    -lm -o "$OUT/test_batchprof"

run_harness "$OUT/test_batchprof"
