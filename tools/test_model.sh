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
flags="$flags -Isrc -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/lua-5.1.4/src"
flags="$flags -Ithird_party/libresample/include -Ithird_party/cgltf-1.14 -Ithird_party/stb -I$out/shaders $sdl_cflags"
for shader in src/shaders/*.vert src/shaders/*.frag; do
    name=${shader##*/}
    variable=$(printf '%s' "$name" | tr . _)
    sh tools/embed_shader.sh "$shader" "$out/shaders/$name.h" "g_shader_$variable"
done
python3 - "$out/triangle.gltf" <<'PY'
import base64, json, struct, sys, zlib
data = struct.pack('<9f3H', -.8,-.8,0, .8,-.8,0, 0,.8,0, 0,1,2)
def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind+payload))
png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 1,1,8,6,0,0,0))
       + chunk(b'IDAT', zlib.compress(b'\0\xff\xff\xff\xff')) + chunk(b'IEND', b''))
model = dict(asset=dict(version="2.0"), scene=0, scenes=[dict(nodes=[0])],
    nodes=[dict(mesh=0)], buffers=[dict(byteLength=len(data),
        uri='data:application/octet-stream;base64,'+base64.b64encode(data).decode())],
    bufferViews=[dict(buffer=0, byteOffset=0, byteLength=36),
                 dict(buffer=0, byteOffset=36, byteLength=6)],
    accessors=[dict(bufferView=0, componentType=5126, count=3, type="VEC3",
                    min=[-.8,-.8,0], max=[.8,.8,0]),
               dict(bufferView=1, componentType=5123, count=3, type="SCALAR")],
    images=[dict(uri='data:image/png;base64,'+base64.b64encode(png).decode())],
    textures=[dict(source=0)],
    materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=[.1,.8,.2,1], baseColorTexture=dict(index=0)))],
    meshes=[dict(primitives=[dict(attributes=dict(POSITION=0), indices=1, material=0)])])
open(sys.argv[1], 'w').write(json.dumps(model))
PY
# Vendor warnings stay out of the engine build; no extra runtime dependency.
$cc $flags -w -c third_party/cgltf-1.14/cgltf.c -o "$out/cgltf.o"
$cc $flags -w -c third_party/stb/stb_image.c -o "$out/stb.o"
$cc $flags -c third_party/glad-2.1/glad.c -o "$out/glad.o"
$cc $flags -DMUGEN_MODEL3D tools/test_model.c src/model.c src/pngio.c src/file.c \
    src/util.c src/compat_linux.c "$out/cgltf.o" "$out/stb.o" "$out/glad.o" \
    -Wl,--gc-sections ${LDFLAGS:-} $sdl_libs -ldl -lm -o "$out/desktop"
$cc $flags tools/test_model.c src/model_stub.c -Wl,--gc-sections \
    ${LDFLAGS:-} $sdl_libs -lm -o "$out/unsupported"
"$out/unsupported" "$out/triangle.gltf"
"$out/desktop" "$out/triangle.gltf"
