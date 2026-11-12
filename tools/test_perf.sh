#!/bin/sh
# Production frame/perf code with an injected clock and platform presentation.
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
out="${OUT:-$PWD/build/tmp/perf-test/$TARGET}"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
flags="$ARCHFLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter"
flags="$flags -ffunction-sections -fdata-sections -DSDL_MAIN_HANDLED -Isrc"
flags="$flags -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
flags="$flags -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
flags="$flags -DPTW32_STATIC_LIB $PLAT_INC -Iport/android/app/src/main/jni/src ${SDL_CFLAGS:-$("$PKGCONF" --cflags sdl2)}"
libs="${SDL_LIBS:-$("$PKGCONF" --libs $SDL_LINK_MODE sdl2 | sed 's/-lSDL2main//g;s/-mwindows//g')}"
# Keep unrelated globals/decoder dispatch tables outside sanitizer registration;
# frame.c and perf.c are included in the instrumented harness below.
$CC $flags -Wno-cast-function-type -c src/globals.c -o "$out/globals.o"
san=""
if [ "${SANITIZE:-0}" = 1 ]; then san="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
for platform in 0 1 2; do
    $CC $flags $san -DTEST_ANDROID=$platform tools/test_perf.c "$out/globals.o" \
        -Wl,--gc-sections ${LDFLAGS:-} $libs -lm -o "$out/test-$platform"
    echo "frame profile $platform (desktop, Android software, Android GLES)"
    run_harness "$out/test-$platform"
done
