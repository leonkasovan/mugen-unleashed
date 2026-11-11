#!/bin/sh
# Production desktop/GLES/Vita GL boundaries with a deterministic mock driver.
# No window/SDL library needed. CC/CFLAGS/RUNNER select an ABI; SANITIZE=1
# enables ASan/UBSan. This does not qualify an Android/Vita GPU or SDK build.
set -eu
cd "$(dirname "$0")/.."
TARGET_FLAGS=""
if [ -n "${MUGEN_TEST_TARGET:-}" ]; then
    . tools/lib_target.sh
    TARGET_FLAGS="$ARCHFLAGS -DTEST_EXPECT_BITS=$EXPECT_BITS"
fi
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/tmp/gl-errors}"
mkdir -p "$OUT/tmp" "$OUT/data/gles"
export TMPDIR="$OUT/tmp"
FLAGS="$TARGET_FLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections"
FLAGS="$FLAGS -fno-asynchronous-unwind-tables -DSDL_MAIN_HANDLED"
FLAGS="$FLAGS -Iport/psvita/src -Isrc ${SDL_CFLAGS:-$(${PKG_CONFIG:-pkg-config} --cflags sdl2)} -idirafter third_party/sdk/include"
FLAGS="$FLAGS -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
FLAGS="$FLAGS -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
case "$($CC -dumpmachine)" in *mingw*) FLAGS="$FLAGS -flto=auto" ;; esac
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
# globals.c has legacy decoder function-pointer casts outside this test.
$CC $FLAGS -Wno-cast-function-type -c src/globals.c -o "$OUT/globals.o"
$CC $FLAGS -c third_party/glad-2.1/glad.c -o "$OUT/glad.o"
for backend in desktop gles linux-gles vita; do
    DEFS=""
    case "$backend" in
        gles) DEFS="-DTEST_GLES=1" ;;
        linux-gles) DEFS="-DTEST_GLES=1 -DTEST_LINUX_GLES=1" ;;
        vita) DEFS="-DTEST_VITA=1" ;;
    esac
    $CC $FLAGS $SAN $DEFS tools/test_gl_errors.c src/gl_mvp.c "$OUT/globals.o" "$OUT/glad.o" \
        -Wl,--gc-sections ${LDFLAGS:-} -lm -o "$OUT/test_$backend"
    cp res/shaders/gles/* "$OUT/data/gles/"
    for name in indexed rgba; do
        printf 'void main(){}\n' > "$OUT/data/$name.vert"
        printf 'void main(){}\n' > "$OUT/data/$name.frag"
    done
    (cd "$OUT"; ${RUNNER:-} "./test_$backend")
done

# A refused context must fail before the backend calls any GL entry point.
$CC $FLAGS $SAN tools/test_video_startup.c "$OUT/globals.o" \
    -Wl,--gc-sections ${LDFLAGS:-} -lm -o "$OUT/test_video_startup"
${RUNNER:-} "$OUT/test_video_startup"
