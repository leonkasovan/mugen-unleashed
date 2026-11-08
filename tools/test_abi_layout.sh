#!/bin/sh
# M4 layout report (RENDERING_PLAN.md §8 steps 5+7).
#
# Prints struct layouts under the 32-bit reference compiler and the 64-bit
# compiler. Every 64-bit divergence from the reference offset is a site that
# must use member access (see the camera.c/ai.c/char.c fixes).
#
# Usage: tools/test_abi_layout.sh       (from the repository root)
# Artefacts go under build/tmp/render-abi per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-abi
# The 64-bit kit's gcc.exe is a wrapper that needs its own bin dir on PATH
# to find cc1 (same reason the Makefile demands the kit first on PATH).
case "$PATH" in
*/w64devkit/bin*) ;;
*) export PATH="/c/w64devkit/bin:$PATH";;
esac
CC32="${CC32:-/c/x86devkit/bin/gcc}"
CC64="${CC64:-gcc}"

mkdir -p "$OUT"

# shellcheck disable=SC2086
# SDL's headers are not in the devkit: a clean w64devkit ships no SDL, so
# they come from the dependency tree (build/sysroots/<target>-sysroot) or
# from whatever pkg-config resolves. This test only prints sizeof's, so the
# include path is all it needs -- no library is linked.
SDL_CFLAGS="$(pkg-config --cflags sdl2 2>/dev/null || true)"
if [ -z "$SDL_CFLAGS" ]; then
    for d in build/sysroots/win32-sysroot build/sysroots/win64-sysroot; do
        [ -d "$d/usr/include/SDL2" ] && SDL_CFLAGS="-I$d/usr/include -I$d/usr/include/SDL2" && break
    done
fi

$CC32 -m32 -std=gnu11 -O1 -g -Wall $SDL_CFLAGS \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/sdk/include \
    tools/test_abi_layout.c -o "$OUT/layout32.exe" -lwinmm
"$OUT/layout32.exe" | tee "$OUT/layout32.txt"

# Only now put the 64-bit kit first. "PATH already contains a w64devkit entry"
# is not the same as "the tools in use are the 64-bit ones": this shell can have
# both kits on PATH, and whichever comes first wins. The 32-bit step above still
# needs the 32-bit `as`, so the reordering happens after it, not at the top.
# Seen as "as: unrecognized option `--64'" and "unimplemented: 64-bit mode not
# compiled in" -- which read as a 64-bit build failure rather than a PATH
# problem, and made MUT R04 report a broken layout gate.
case "$PATH" in
/c/w64devkit/bin:*) ;;
*) PATH="/c/w64devkit/bin:$PATH"; export PATH;;
esac
if ! "$CC64" -dumpmachine 2>/dev/null | grep -q '^x86_64'; then
    echo "CC64=$CC64 is not a 64-bit compiler ($("$CC64" -dumpmachine 2>/dev/null))" >&2
    echo "Put /c/w64devkit/bin first on PATH, or set CC64 to that gcc." >&2
    exit 1
fi

$CC64 -m64 -std=gnu11 -O1 -g -Wall \
    -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 \
    -Ithird_party/lua-5.1.4/src -Ithird_party/libpng-1.2.38 \
    $SDL_CFLAGS \
    tools/test_abi_layout.c -o "$OUT/layout64.exe" -lwinmm
"$OUT/layout64.exe" | tee "$OUT/layout64.txt"

echo "== divergence (reference vs 64-bit) =="
diff "$OUT/layout32.txt" "$OUT/layout64.txt" || true
cat <<'NOTE'
Expected divergences (each fixed by member access, never a hardcoded offset):
  Bg.legacy 0x64 -> 0x7c, Projectile.afterimage 0x504 -> 0x548
Coincidentally stable (documented at the use site, still member access):
  BgElem.y 0x88, InputBuf.keys 0x78, GlTex 0x18/0x1c (pointer-free)
NOTE

echo "== 64-bit syntax check of the fixed units (§8.8) =="
# Real target compiler (LLP64 Windows), not -m64 on the host kit: proves the
# member-access fixes introduce no new type errors where it matters.
# shellcheck disable=SC2086
/c/w64devkit/bin/gcc -m64 -std=gnu11 -fsyntax-only \
    -Isrc -Ithird_party/lua-5.1.4/src -Ithird_party/glad-2.1 \
    -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38 \
    $SDL_CFLAGS \
    src/camera.c src/ai.c src/char.c src/bgmstream.c

echo "layout + 64-bit syntax: OK"
