#!/bin/sh
# Allocator regression tests; SANITIZE=1 enables ASan/UBSan on a host toolchain.
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
OUT="${OUT:-$PWD/build/tmp/test-decompbuffer}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
san=""
if [ "${SANITIZE:-0}" = 1 ]; then san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"; fi
# shellcheck disable=SC2086
"$CC" $ARCHFLAGS $CFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra $san \
 -ffunction-sections -fdata-sections -DSDL_MAIN_HANDLED \
 -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
 $("$PKGCONF" --cflags sdl2 libpng) tools/test_decompbuffer.c \
 -Wl,--gc-sections -o "$OUT/probe"
for case_name in normal zero upper config request clock oom null-key; do
    run_harness "$OUT/probe" "$case_name"
done
