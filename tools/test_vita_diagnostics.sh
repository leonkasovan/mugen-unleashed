#!/bin/sh
# Production diagnostic module, mocked device APIs; no native GPU claim.
set -eu
cd "$(dirname "$0")/.."
out="$PWD/build/tmp/vita-diagnostics-tests"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
${CC:-cc} ${CFLAGS:-} -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Isrc -Iport/psvita/src tools/test_vita_diagnostics.c -o "$out/probe"
for scenario in success native-fail shader-fail pixel-fail; do
  mkdir -p "$out/$scenario"
  (cd "$out/$scenario"; ../probe "$scenario")
done
