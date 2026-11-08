#!/bin/sh
# Host contract tests; no claim about GXM or actual shader compilation.
set -eu
cd "$(dirname "$0")/.."
out=build/tmp/vita-video-tests
mkdir -p "$out"
trap 'rm -f "$out/probe"; rmdir "$out" 2>/dev/null || true' EXIT
${CC:-gcc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -O1 -Isrc \
    tools/test_vita_video.c src/gl_mvp.c -lm -o "$out/probe"
"$out/probe"
"$out/probe" alloc-fail
"$out/probe" invalid-viewport
"$out/probe" resolution-fallback
"$out/probe" sdl-owned
