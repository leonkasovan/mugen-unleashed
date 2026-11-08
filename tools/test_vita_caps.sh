#!/bin/sh
# Host gate for the Vita/GLES capability resolver (RENDERING_PLAN.md V2) and
# the Vita shader dialect (V3).
#
# Compiles port/psvita/src/vita_caps.c on the host -- it has no GL dependency,
# by design -- and runs the assertions in tools/test_vita_caps.c. No Vita
# hardware needed.
#
# Usage: tools/test_vita_caps.sh
#   MUGEN_TEST_TARGET=win32|win64 selects compiler, kit and pointer width.
# Artefacts go under build/tmp/render-vita per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/tmp/render-vita
. tools/lib_target.sh

mkdir -p "$OUT"

echo "target $TARGET: $CC $ARCHFLAGS ($("$CC" -dumpmachine 2>/dev/null || echo unknown))"

"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    -Iport/psvita/src \
    tools/test_vita_caps.c port/psvita/src/vita_caps.c \
    -o "$OUT/test_vita_caps"

echo "built $OUT/test_vita_caps"
run_harness "$OUT/test_vita_caps"

# V3: the Vita shader dialect (ES-shaped: attributes/varyings, 2D palette,
# EB_DEFINES marker) must stay compatible. Same structural rules as GLES but
# with vitaGL's own naming.
"$CC" $ARCHFLAGS -std=gnu11 -O1 -g -Wall \
    tools/test_gles_shader.c \
    -o "$OUT/test_vita_shader"
run_harness "$OUT/test_vita_shader" res/shaders/vita vita
