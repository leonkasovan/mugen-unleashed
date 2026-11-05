#!/usr/bin/env bash
#
# Compile gates for translation units that must build on every backend.
#
# Why this exists: the Android input path shares src/pads.c with the desktop,
# and a change that touched the Android-only controller sidecar compiled fine
# under -DANDROID_PORT but broke the desktop SDL2 build with "'g_padController'
# undeclared". Nothing in the test suite compiled that module off-Android, so
# the breakage reached a pushed commit. This gate compiles the shared modules
# under each backend's real defines and fails on any diagnostic.
#
# Usage: bash tools/test_compile_gates.sh [--target win32|win64]
set -u

. tools/lib_target.sh

OUT="build/test-compile-gates"
mkdir -p "$OUT"

#include dirs, valid for every backend below
INCS="-Isrc"
INCS="$INCS -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15"
INCS="$INCS -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38"
INCS="$INCS -Ithird_party/libresample/include -Ithird_party/glad-es-3.0"
INCS="$INCS -Iport/android/app/src/main/jni/src $PLAT_INC"
# SDL's own include dir comes from the target's pkg-config, the same way the
# other harnesses get it.
SDL_CFLAGS=$("$PKGCONF" --cflags sdl2 | sed 's/-Dmain=SDL_main//')

# Modules that are compiled for more than one backend and must stay portable.
MODULES="src/pads.c src/config.c src/events.c src/sprite.c src/gfx.c"

fails=0
check() {
    id="$1"
    shift
    # shellcheck disable=SC2086
    if ! $CC $ARCHFLAGS -std=gnu11 -fsyntax-only "$@" $INCS $SDL_CFLAGS $MODULE >"$OUT/$id.log" 2>&1; then
        echo "FAIL: $id"
        # Strip the pre-existing '/*' within-comment warning, which is not ours.
        grep -v 'within comment' "$OUT/$id.log" | head -8
        fails=$((fails + 1))
        return
    fi
    # -Wall is on; a portability break is often a warning before it is an error.
    if grep -qE 'warning:' "$OUT/$id.log"; then
        if grep -v 'within comment' "$OUT/$id.log" | grep -qE 'warning:'; then
            echo "FAIL: $id (warnings)"
            grep -v 'within comment' "$OUT/$id.log" | head -8
            fails=$((fails + 1))
            return
        fi
    fi
    echo "ok:   $id"
}

for MODULE in $MODULES; do
    name=$(basename "$MODULE" .c)
    # Desktop SDL2: the combination that was broken.
    check "sdl2-$name" -DMUGEN_SDL2_API -DMUGEN_SDL2
    # Android, software renderer.
    check "android-$name" -DANDROID_PORT -DMUGEN_ANDROID_SW
    # Android with the GLES backend compiled in.
    check "android-gles-$name" -DANDROID_PORT -DMUGEN_ANDROID_SW -DMUGEN_GLES
done

if [ "$fails" -ne 0 ]; then
    echo "compile-gates: $fails FAILURES"
    exit 1
fi
echo "compile-gates: all backends compile cleanly"
