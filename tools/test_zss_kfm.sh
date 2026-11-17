#!/bin/sh
# Native Linux-only unchanged-KFM qualification. Other targets remain OPEN.
# Pass the normal make flags as arguments (CC=..., PKG_CFLAGS=..., PKG_LIBS=...).
set -eu
cd "$(dirname "$0")/.."
case "$(uname -s)" in Linux) ;; *) echo 'SKIP: KFM runtime gate requires native Linux' >&2; exit 77;; esac
OUT="${OUT:-$PWD/build/tmp/zss-kfm-runtime}"
case "$OUT" in "$PWD"/build/*) ;; *) echo 'OUT must be inside repository/build' >&2; exit 2;; esac
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp" PYTHONDONTWRITEBYTECODE=1
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -DSDL_MAIN_HANDLED -DMUGEN_MEMPROF_NO_WRAP=1 \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src \
    -Ithird_party/libresample/include $(${PKG_CONFIG:-pkg-config} --cflags sdl2) \
    -c tools/test_zss_kfm_runtime.c -o "$OUT/runtime.o"
# Force relink if only the adapter changed; no production source is patched.
rm -f "$OUT/engine/mugen-linux-x64"
make "$@" MUGEN_TARGET=linux-x64 MUGEN_MODEL3D=0 BUILD="$OUT/engine" \
    PLAT_LDFLAGS="-Wl,--gc-sections -Wl,--wrap=Round_Tick $OUT/runtime.o"
python3 tools/test_zss_kfm.py --binary "$OUT/engine/mugen-linux-x64" \
    --game-dir "${GAME_DIR:-install}" --out "$OUT/results"
