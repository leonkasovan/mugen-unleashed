#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="${OUT:-$PWD/build/tmp/model-test}"
mkdir -p "$out/tmp" "$out/shaders"
export TMPDIR="$out/tmp"
cc="${CC:-cc}"
sdl_cflags="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 libpng zlib)}"
sdl_libs="${SDL_LIBS:-$(pkg-config --libs sdl2 libpng zlib)}"
flags="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Wall -Wextra -Wno-unused-parameter"
flags="$flags -DSDL_MAIN_HANDLED -ffunction-sections -fdata-sections"
loader=third_party/glad-2.1
if [ "${MODEL_TEST_GLES:-0}" = 1 ]; then
    loader=third_party/glad-es-3.0
    flags="$flags -DMUGEN_GLES"
fi
if [ "${MODEL_TEST_BASIC:-0}" = 1 ]; then flags="$flags -DMUGEN_MODEL_BASIC"; fi
flags="$flags -Isrc -I$loader -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src"
flags="$flags -Ithird_party/libresample/include -Ithird_party/cgltf-1.14 -Ithird_party/stb -I$out/shaders $sdl_cflags"
for shader in src/shaders/*.vert src/shaders/*.frag; do
    name=${shader##*/}
    variable=$(printf '%s' "$name" | tr . _)
    sh tools/embed_shader.sh "$shader" "$out/shaders/$name.h" "g_shader_$variable"
done
python3 tools/model_fixtures.py "$out"
# Vendor warnings stay out of the engine build; no extra runtime dependency.
$cc $flags -w -c third_party/cgltf-1.14/cgltf.c -o "$out/cgltf.o"
$cc $flags -w -c third_party/stb/stb_image.c -o "$out/stb.o"
$cc $flags -c "$loader/glad.c" -o "$out/glad.o"
$cc $flags -DMUGEN_MODEL3D tools/test_model.c src/model.c src/model_asset.c src/model_gpu.c src/model_pass.c src/pngio.c src/file.c \
    src/util.c src/compat_linux.c "$out/cgltf.o" "$out/stb.o" "$out/glad.o" \
    -Wl,--gc-sections ${LDFLAGS:-} $sdl_libs -ldl -lm -o "$out/desktop"
$cc $flags tools/test_model.c src/model_stub.c -Wl,--gc-sections \
    ${LDFLAGS:-} $sdl_libs -lm -o "$out/unsupported"
"$out/unsupported" "$out/triangle.gltf"
for fixture in triangle animated morph alpha light; do
    echo "fixture: $fixture"
    MODEL_TEST_CAPTURE="$out/$fixture.rgba" "$out/desktop" "$out/$fixture.gltf"
done
for fixture in skin skin-morph; do
    if [ "${MODEL_TEST_BASIC:-0}" = 1 ]; then
        MODEL_TEST_REJECT_SKIN=1 "$out/desktop" "$out/$fixture.gltf"
    else
        MODEL_TEST_CAPTURE="$out/$fixture.rgba" "$out/desktop" "$out/$fixture.gltf"
    fi
done
"$out/desktop" "$out/triangle.glb"
for fixture in oversized cycle bad-joint bad-dds; do
    MODEL_TEST_BAD_ASSET=1 "$out/desktop" "$out/$fixture.gltf"
done
