#!/bin/sh
# SDL2 scaler against committed, pre-removal synthetic reference hashes.
set -eu
cd "$(dirname "$0")/.."
. tools/lib_target.sh
out="${OUT:-$PWD/build/tmp/zoomdiff/$TARGET}"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
rm -f "$out"/*.raw
# Intentionally split compiler/pkg-config flags; CC may be a wrapper.
$CC $ARCHFLAGS ${CFLAGS:-} -std=gnu11 -O1 -g -DSDL_MAIN_HANDLED -Isrc \
    ${SDL_CFLAGS:-$("$PKGCONF" --cflags sdl2)} \
    tools/test_zoomdiff.c src/sdl2surface.c \
    ${LDFLAGS:-} ${SDL_LIBS:-$("$PKGCONF" --libs sdl2 | sed 's/-lSDL2main//g;s/-mwindows//g')} \
    -lm -o "$out/probe"
${RUNNER:-} "$out/probe" "$out"
python3 tools/check_fixtures.py tools/fixtures/scaler-baseline.json "$out"
