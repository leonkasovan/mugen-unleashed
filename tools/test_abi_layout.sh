#!/bin/sh
# Record actual target layouts; a missing comparison compiler is SKIP (77).
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
out="${OUT:-$PWD/build/tmp/render-abi}"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
flags="-DSDL_MAIN_HANDLED -std=gnu11 -O1 -g -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38 -Ithird_party/libresample/include"
sdl="${SDL_CFLAGS:-$("$PKGCONF" --cflags sdl2)}"
$CC $ARCHFLAGS ${CFLAGS:-} $flags $PLAT_INC $sdl tools/test_abi_layout.c \
    ${LDFLAGS:-} -o "$out/layout-$EXPECT_BITS"
${RUNNER:-} "$out/layout-$EXPECT_BITS" > "$out/layout-$EXPECT_BITS.txt"
cat "$out/layout-$EXPECT_BITS.txt"
$CC $ARCHFLAGS ${CFLAGS:-} $flags $PLAT_INC $sdl -fsyntax-only \
    src/camera.c src/ai.c src/char.c src/bgmstream.c
case "$TARGET" in
    win32) other="${CC64:-/c/w64devkit/bin/gcc}"; other_bits=64; other_inc="" ;;
    win64) other="${CC32:-/c/x86devkit/bin/gcc}"; other_bits=32; other_inc="-Ithird_party/sdk/include" ;;
    *) other="${CC32:-}"; other_bits=32; other_inc="" ;;
esac
if [ -z "$other" ] || ! command -v "$other" >/dev/null 2>&1; then
    echo "SKIP: $EXPECT_BITS-bit layout/syntax recorded; the other ABI compiler is unavailable"
    exit 77
fi
# Compiler subprograms must come from this compiler's kit, too.
case "$other" in */*) PATH="${other%/*}:$PATH"; export PATH ;; esac
$other -m$other_bits ${CFLAGS:-} $flags $other_inc ${SDL_CFLAGS_OTHER:-$sdl} \
    tools/test_abi_layout.c ${LDFLAGS:-} -o "$out/layout-$other_bits"
${RUNNER_OTHER:-} "$out/layout-$other_bits" > "$out/layout-$other_bits.txt"
cat "$out/layout-$other_bits.txt"
diff "$out/layout-32.txt" "$out/layout-64.txt" || true
printf 'ok: both ABI layouts recorded; differences require member access, not raw offsets\n'
