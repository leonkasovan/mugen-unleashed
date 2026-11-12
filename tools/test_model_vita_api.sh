#!/bin/sh
# Host syntax check against the committed VitaGL API, NOT a VitaSDK/device test.
# Only opaque SDK types are stubbed; every GL declaration is vitaGL's own.
set -eu
cd "$(dirname "$0")/.."
out="${OUT:-$PWD/build/tmp/model-vita-api}"
mkdir -p "$out/shaders"
cat > "$out/vitashark.h" <<'EOF'
#include <stdint.h>
#include <stddef.h>
typedef struct SceGxmContext SceGxmContext;
typedef struct SceGxmShaderPatcher SceGxmShaderPatcher;
typedef struct SceGxmTexture SceGxmTexture;
typedef int SceGxmMultisampleMode;
typedef int SceGxmTextureFormat;
typedef int SceGxmTextureType;
typedef int shark_opt;
EOF
for shader in src/shaders/*.vert src/shaders/*.frag; do
    name=${shader##*/}
    variable=$(printf '%s' "$name" | tr . _)
    sh tools/embed_shader.sh "$shader" "$out/shaders/$name.h" "g_shader_$variable"
done
# Linux SDL headers stand in for SDL types only. This does not link or assert
# that the real Vita SDL library is configured correctly.
"${CC:-cc}" ${CFLAGS:-} -std=gnu11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
    -Werror=implicit-function-declaration -DSDL_MAIN_HANDLED -DMUGEN_MODEL3D -DMUGEN_VITA_GL \
    -DSDL_VIDEO_RENDER_VITA_GXM=1 -Isrc -Iport/psvita/src -I"$out" -I"$out/shaders" \
    -Ithird_party/vitagl/src/source -Ithird_party/cgltf-1.14 -Ithird_party/stb \
    -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
    $(pkg-config --cflags sdl2 libpng) \
    src/model.c src/model_asset.c src/model_gpu.c src/model_pass.c
echo 'ok: model sources match VitaGL declarations (host syntax only; device OPEN)'
