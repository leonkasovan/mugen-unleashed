#!/bin/sh
# Differential test: SDL_gfx's zoomSurface versus the SDL2 shim's replacement.
#
# Both directories are built from one source file (tools/test_zoomdiff.c): one
# against SDL 1.2 + the kit's libSDL_gfx.a, one against SDL2 + src/sdl2compat.c,
# each writing a .raw image per case. Then the files are compared byte for byte.
#
# SDL2_PLAN.md requires this before the replacement counts as equivalent to the
# library the engine was built against.
#
# Usage: tools/test_zoomdiff.sh     (from the repository root)
# Artefacts go under build/zoomdiff per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/zoomdiff
SDLKIT=C:/x86devkit
CC="${CC:-gcc}"
FAIL=0

mkdir -p "$OUT/sdl1" "$OUT/sdl2"

echo "== SDL 1.2 + SDL_gfx =="
# -mconsole -Dmain=SDL_main is how the engine builds its SDL 1.2 target.
# SDL 1.2's <SDL_main.h> renames main() to SDL_main and its libmingw32 wants a
# WinMain, so the test is a console program with the rename undone.
$CC -m32 -std=gnu11 -O1 -g -mconsole \
    -I"$SDLKIT/include/SDL" \
    tools/test_zoomdiff.c \
    -o "$OUT/sdl1/test_zoomdiff.exe" \
    "$SDLKIT/lib/libSDL.a" "$SDLKIT/lib/libSDL_gfx.a" \
    -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lshell32
"$OUT/sdl1/test_zoomdiff.exe" "$OUT/sdl1"

echo "== SDL2 + src/sdl2compat.c =="
# shellcheck disable=SC2086
$CC -m32 -std=gnu11 -O1 -g -mconsole \
    -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    $(pkg-config --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
    tools/test_zoomdiff.c src/sdl2compat.c \
    -o "$OUT/sdl2/test_zoomdiff.exe" \
    $(pkg-config --libs --static sdl2 | sed 's/-lSDL2main//;s/-mwindows//') \
    -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm
"$OUT/sdl2/test_zoomdiff.exe" "$OUT/sdl2"

echo "== compare =="
# No case is exempt. Every comparison must be byte-identical: the fixed-point
# sampler (step = floor(65536*src/dst), coord = (x*step)>>16) and the 16-bit
# channel widening (shift for RGB555, scale for RGB565) both reproduced SDL_gfx
# exactly, so there is nothing left to allow for.
EXPECTED_DIFF=''

for f in "$OUT"/sdl1/*.raw; do
    name=$(basename "$f")
    if [ ! -f "$OUT/sdl2/$name" ]; then
        echo "MISSING: $name was not produced by the SDL2 build"
        FAIL=1
        continue
    fi
    if cmp -s "$f" "$OUT/sdl2/$name"; then
        printf '  %-20s identical\n' "${name%.raw}"
    else
        # Report the first difference so it is diagnosable, not just "differs".
        first=$(cmp "$f" "$OUT/sdl2/$name" 2>&1 | head -1)
        hdr1=$(head -1 "$f")
        hdr2=$(head -1 "$OUT/sdl2/$name")
        printf '  %-20s DIFFERS  (%s)\n' "${name%.raw}" "$first"
        [ "$hdr1" = "$hdr2" ] || printf '     header: %s / %s\n' "$hdr1" "$hdr2"
        FAIL=1
    fi
done

if [ "$FAIL" -ne 0 ]; then
    echo "result: DIFFERENCES (see above)"
    exit 1
fi
echo "result: all $(ls "$OUT"/sdl1/*.raw | wc -l) cases byte-identical to SDL_gfx"
