#!/bin/sh
# Runtime checks for defects diagnosed by the w64devkit 32-bit -Wall build.
# CC/CFLAGS/LDFLAGS/RUNNER support cross builds; SANITIZE=1 enables ASan/UBSan.
set -eu
cd "$(dirname "$0")/.."
TARGET_FLAGS=""
if [ -n "${MUGEN_TEST_TARGET:-}" ]; then
    . tools/lib_target.sh
    TARGET_FLAGS="$ARCHFLAGS -DTEST_EXPECT_BITS=$EXPECT_BITS"
fi
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/build-warnings-test}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
FLAGS="$TARGET_FLAGS ${CFLAGS:-} -std=gnu11 -O2 -g -UNDEBUG -ffunction-sections -fdata-sections"
FLAGS="$FLAGS -fno-strict-aliasing -fno-asynchronous-unwind-tables -Wall -Werror -Wno-unused-parameter -Wno-unused-result -Wno-stringop-truncation"
FLAGS="$FLAGS -D_SDL_config_h -DSTDC_HEADERS=1 -DHAVE_STDINT_H=1 -DLIBRESAMPLE_STATIC"
FLAGS="$FLAGS -Isrc -Ithird_party/sdk/include/SDL -idirafter third_party/sdk/include"
FLAGS="$FLAGS -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
FLAGS="$FLAGS -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
# Leave unrelated callback tables uninstrumented so gc-sections can drop them.
# shellcheck disable=SC2086
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
# shellcheck disable=SC2086
$CC $FLAGS $SAN -Dfopen=Test_Fopen -Dfclose=Test_Fclose -c src/file.c -o "$OUT/file.o"
# shellcheck disable=SC2086
$CC $FLAGS $SAN tools/test_build_warnings.c src/ai.c src/playerinput.c src/wav.c \
    src/hashtable.c src/ini.c src/util.c src/compat_linux.c \
    src/storyboard.c src/keyedarray.c src/misc.c \
    "$OUT/file.o" "$OUT/globals.o" -Wl,--gc-sections ${LDFLAGS:-} -lm -o "$OUT/test_build_warnings"
# shellcheck disable=SC2086
${RUNNER:-} "$OUT/test_build_warnings"
