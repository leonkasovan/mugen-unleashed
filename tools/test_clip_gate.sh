#!/usr/bin/env bash
#
# CLIP01 gate: Gfx_SetClipRect must reach the GL clip path on every backend
# that has a GL renderer, and must not on the ones that do not.
#
# Compiles the real src/gfx.c three times -- desktop SDL2, Android software,
# Android + GLES -- links it against a counting Gl_ApplyClipRect stub, and runs
# the result. Only Gfx_SetClipRect and what it touches survive the link, which
# is why no engine, no SDL build and no GL context is needed here.
#
# Usage: bash tools/test_clip_gate.sh [--target win32|win64]
set -u

cd "$(dirname "$0")/.."
. tools/lib_target.sh

OUT=build/test-clip-gate
mkdir -p "$OUT"

SDL_CFLAGS=$("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//')
# SDL_main.h renames main() unless this is set.
SDL_CFLAGS="$SDL_CFLAGS -DSDL_MAIN_HANDLED"
SDL_LIBS=$("$PKGCONF" --libs --static sdl2 | sed 's/-lSDL2main//')

INCS="-Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15"
INCS="$INCS -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38"
INCS="$INCS -Ithird_party/libresample/include -Ithird_party/glad-es-3.0"
INCS="$INCS -Iport/android/app/src/main/jni/src $PLAT_INC"

fails=0

build_and_run() {
    id="$1"
    shift
    # shellcheck disable=SC2086
    if ! $CC $ARCHFLAGS -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -fsyntax-only \
        "$@" $INCS $SDL_CFLAGS tools/test_clip_gate.c >"$OUT/$id.c.log" 2>&1; then
        echo "FAIL: $id (compile)"
        grep -v 'within comment' "$OUT/$id.c.log" | head -8
        fails=$((fails + 1))
        return
    fi
    # -ffunction-sections + --gc-sections: gfx.c carries the whole graphics core
    # and none of it is reachable from this test except the clip rect.
    #
    # -fno-asynchronous-unwind-tables is part of making that work on 64-bit PE.
    # There, the compiler emits .xdata$<fn>/.pdata$<fn> beside every function,
    # and the PE linker does not associate them with the function when it drops
    # unreachable sections, so six of them (Screen_Create, Gfx_Present,
    # Gfx_SetRenderTarget, Gfx_LogRenderer, Gfx_Capability, Fade_Update) stayed
    # in the image and every global they touch had to resolve: the android-gles
    # configuration failed to link with 52 undefined symbols, and MUGEN_TEST_TARGET
    # win64 reported CLIP01 FAIL for this reason alone. The 32-bit PE object has
    # no such tables, which is why only the 64-bit run was affected. Unwind data
    # is worthless in a test harness, so drop it.
    # shellcheck disable=SC2086
    if ! $CC $ARCHFLAGS -std=gnu11 -O1 -g -Wall -Wno-unused-parameter \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        "$@" $INCS $SDL_CFLAGS \
        tools/test_clip_gate.c src/gfx.c \
        -Wl,--gc-sections -o "$OUT/$id" \
        $SDL_LIBS -lwinmm -lm >>"$OUT/$id.c.log" 2>&1; then
        echo "FAIL: $id (link)"
        grep -v 'within comment' "$OUT/$id.c.log" | tail -8
        fails=$((fails + 1))
        return
    fi
    if ! "./$OUT/$id" >"$OUT/$id.log" 2>&1; then
        echo "FAIL: $id"
        grep -v 'within comment' "$OUT/$id.log" | head -8
        fails=$((fails + 1))
        return
    fi
    echo "ok:   $id"
}

# Desktop SDL2: a GL renderer exists, so the clip rect must reach it.
build_and_run desktop -DMUGEN_SDL2_API -DMUGEN_SDL2 -DCLIP_GATE_EXPECT_GL

# Android without the GLES backend: there is no GL renderer to notify.
build_and_run android -DANDROID_PORT -DMUGEN_ANDROID_SW

# Android with GLES: the configuration the title-screen regression ran in.
build_and_run android-gles -DANDROID_PORT -DMUGEN_ANDROID_SW -DMUGEN_GLES -DCLIP_GATE_EXPECT_GL

if [ "$fails" -ne 0 ]; then
    echo "clip-gate: $fails FAILURES"
    exit 1
fi
echo "clip-gate: the clip rect reaches the GL renderer exactly when one exists"
