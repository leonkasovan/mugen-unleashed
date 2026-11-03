#!/bin/sh
# Host gate for the Vita/GLES capability resolver (RENDERING_PLAN.md V2).
#
# Compiles port/psvita/src/vita_caps.c on the host -- it has no GL dependency,
# by design -- and runs the assertions in tools/test_vita_caps.c. No Vita
# hardware needed.
#
# Usage: tools/test_vita_caps.sh
# Artefacts go under build/render-vita per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-vita
CC="${CC:-gcc}"

mkdir -p "$OUT"

"$CC" -m32 -std=gnu11 -O1 -g -Wall \
    -Iport/psvita/src \
    tools/test_vita_caps.c port/psvita/src/vita_caps.c \
    -o "$OUT/test_vita_caps"

echo "built $OUT/test_vita_caps"
"$OUT/test_vita_caps"

# V3: the Vita shader dialect (ES-shaped: attributes/varyings, 2D palette,
# EB_DEFINES marker) must stay compatible. Same structural rules as GLES but
# with vitaGL's own naming.
"$CC" -m32 -std=gnu11 -O1 -g -Wall \
    tools/test_gles_shader.c \
    -o "$OUT/test_vita_shader"
"$OUT/test_vita_shader" port/psvita/shaders vita
