#!/bin/sh
# Native Linux or MinGW host tests; actual Android software code, no window.
set -eu
cd "$(dirname "$0")/.."
TARGET_FLAGS=""
if [ -n "${MUGEN_TEST_TARGET:-}" ]; then
    . tools/lib_target.sh
    TARGET_FLAGS="$ARCHFLAGS -DTEST_EXPECT_BITS=$EXPECT_BITS"
    PKG_CONFIG="$PKGCONF"
fi
CC="${CC:-cc}"
PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
OUT="$PWD/build/tmp/review-regress-test"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
SDL_CFLAGS="${SDL_CFLAGS:-$("$PKG_CONFIG" --cflags sdl2)}"
SDL_LIBS="${SDL_LIBS:-$("$PKG_CONFIG" --libs sdl2 | sed 's/-lSDL2main//g; s/-mwindows//g')}"
FLAGS="$TARGET_FLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Werror=implicit-function-declaration"
FLAGS="$FLAGS -DANDROID_PORT -DMUGEN_ANDROID_SW -DSDL_MAIN_HANDLED"
FLAGS="$FLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
FLAGS="$FLAGS -Isrc -Iport/android/app/src/main/jni/src -Ithird_party/glad-2.1"
FLAGS="$FLAGS -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
FLAGS="$FLAGS -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL_CFLAGS"
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
# Only parser/test allocations need sanitizer instrumentation. Leaving the large
# callback tables in other modules uninstrumented lets gc-sections discard
# unrelated subsystems; their resource lifetime is asserted by the test loader.
for name in globals config system events console video anim title; do
    # shellcheck disable=SC2086
    $CC $FLAGS -c "src/$name.c" -o "$OUT/$name.o"
done
for name in cnsparse expr; do
    # shellcheck disable=SC2086
    $CC $FLAGS $SAN -c "src/$name.c" -o "$OUT/$name.o"
done
# shellcheck disable=SC2086
$CC $FLAGS $SAN tools/test_review_regress.c "$OUT"/*.o \
    -Wl,--gc-sections -Wl,--wrap=Video_SetMode ${LDFLAGS:-} $SDL_LIBS -lm \
    -o "$OUT/test_review_regress"
"$OUT/test_review_regress"
