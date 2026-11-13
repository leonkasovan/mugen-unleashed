#!/bin/sh
# Differential gate for the extracted TrueType line-break search.
#
# tools/../src/textwrap.c holds the search that Font_WrapLengthTTF used to
# perform inline, rewritten as a binary search over the same measurements. This
# compiles it with a synthetic font and compares it against a verbatim copy of
# the original loop (tools/test_textwrap.c) on hundreds of inputs. No SDL_ttf,
# no font file, no GPU -- src/textwrap.c deliberately has no engine dependency.
#
# Usage: sh tools/test_textwrap.sh
set -eu
cd "$(dirname "$0")/.."
OUT="${OUT:-$PWD/build/tmp/test-textwrap}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

. tools/lib_target.sh

# shellcheck disable=SC2086
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -Isrc \
    tools/test_textwrap.c src/textwrap.c \
    -o "$OUT/test_textwrap"

run_harness "$OUT/test_textwrap"
