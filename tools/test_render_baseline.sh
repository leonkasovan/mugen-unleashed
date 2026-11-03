#!/bin/sh
# M0 baseline harness: records build/dependency/driver metadata and runs the
# shared fixture generator under SDL1 and SDL2.
#
# Usage: tools/test_render_baseline.sh [--sdl1-only | --sdl2-only]
# Artefacts go under build/render-baseline/<target>/ per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-baseline
SDLKIT=C:/x86devkit
CC="${CC:-gcc}"
MODE="${1:-all}"

mkdir -p "$OUT/sdl1" "$OUT/sdl2"

baseline_json() {
    # $1 = target dir, $2 = label
    dir="$1"; label="$2"
    sha="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
    triple="$($CC -dumpmachine 2>/dev/null || echo unknown)"
    flags="$($CC -dumpversion 2>/dev/null || echo unknown)"
    if command -v pkg-config >/dev/null 2>&1; then
        sdl2ver="$(pkg-config --modversion sdl2 2>/dev/null || echo unknown)"
    else
        sdl2ver="unknown"
    fi
    {
        printf '{\n'
        printf '  "label": "%s",\n' "$label"
        printf '  "code_sha": "%s",\n' "$sha"
        printf '  "compiler": "%s %s",\n' "$CC" "$flags"
        printf '  "triple": "%s",\n' "$triple"
        printf '  "sdl2_pkg": "%s",\n' "$sdl2ver"
        printf '  "os": "%s",\n' "$(uname -s 2>/dev/null || echo Windows)"
        printf '  "game_size": "640x480",\n'
        printf '  "cfg": "RenderMode=system, sdl1 + sdl2 defaults"\n'
        printf '}\n'
    } > "$dir/baseline.json"
    echo "wrote $dir/baseline.json"
}

if [ "$MODE" = "all" ] || [ "$MODE" = "--sdl1-only" ]; then
    echo "== SDL 1.2 + SDL_gfx =="
    $CC -m32 -std=gnu11 -O1 -g -mconsole \
        -I"$SDLKIT/include/SDL" \
        tools/test_render_baseline.c \
        -o "$OUT/sdl1/test_render_baseline.exe" \
        "$SDLKIT/lib/libSDL.a" "$SDLKIT/lib/libSDL_gfx.a" \
        -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lshell32
    baseline_json "$OUT/sdl1" "sdl1-reference"
    "$OUT/sdl1/test_render_baseline.exe" "$OUT/sdl1"
fi

if [ "$MODE" = "all" ] || [ "$MODE" = "--sdl2-only" ]; then
    echo "== SDL2 + shim =="
    # shellcheck disable=SC2086
    $CC -m32 -std=gnu11 -O1 -g -mconsole \
        -DMUGEN_SDL2 -DSDL_MAIN_HANDLED \
        -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
        -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
        $(pkg-config --cflags sdl2 | sed 's/-Dmain=SDL_main//') \
        tools/test_render_baseline.c src/sdl2surface.c src/sdl2keys.c \
        -o "$OUT/sdl2/test_render_baseline.exe" \
        $(pkg-config --libs --static sdl2 | sed 's/-lSDL2main//;s/-mwindows//') \
        -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lm
    baseline_json "$OUT/sdl2" "sdl2-shim"
    "$OUT/sdl2/test_render_baseline.exe" "$OUT/sdl2"
fi

if [ "$MODE" = "all" ]; then
    echo "== compare =="
    FAIL=0
    for f in F-RAMP565.raw F-IDX8.raw F-PADX.raw; do
        if ! cmp -s "$OUT/sdl1/$f" "$OUT/sdl2/$f"; then
            echo "MISMATCH: $f"
            FAIL=1
        fi
    done
    if [ "$FAIL" = "0" ]; then
        echo "baseline fixtures: identical"
    else
        echo "baseline fixtures: DIFFER (see above)" >&2
        exit 1
    fi
fi
