#!/bin/sh
# No GPU or module-loading claim: exercise the production preflight on a host.
set -eu
cd "$(dirname "$0")/.."
out="$PWD/build/tmp/vita-shader-tests"
mkdir -p "$out"
trap 'rm -f "$out/probe" "$out/compiler-fixture.bin"; rmdir "$out" 2>/dev/null || true' EXIT
export TMPDIR="$out"
${CC:-cc} ${CFLAGS:-} -UNDEBUG -DSDL_MAIN_HANDLED -std=c11 -Wall -Wextra -Werror -O1 -Isrc \
    tools/test_vita_shader.c -o "$out/probe"
for scenario in success bundled bundled-invalid fallback load-failure raw-elf short-header open-denied no-context; do
    (cd "$out"; ./probe "$scenario")
done
