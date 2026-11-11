#!/bin/sh
# SDL >= 2.24 for virtual descriptors. This is not a physical-device test.
set -eu
cd "$(dirname "$0")/.."
out="${OUT:-build/tmp/vita-sdl2-input}"
mkdir -p "$out"
${CC:-cc} ${CFLAGS:-} -DSDL_MAIN_HANDLED -std=c11 -O1 -g -Wall -Wextra -Werror \
    ${SDL_CFLAGS:-$(pkg-config --cflags sdl2)} -Isrc -Iport/psvita/src \
    tools/test_vita_sdl2input.c ${SDL_LIBS:-$(pkg-config --libs sdl2)} \
    -o "$out/test_vita_sdl2input"
SDL_VIDEODRIVER=dummy "$out/test_vita_sdl2input"
