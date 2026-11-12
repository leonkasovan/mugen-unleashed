#!/usr/bin/env python3
"""Small synthetic glTF assets: no copyrighted game/model data required."""
import base64
import copy
import json
from pathlib import Path
import struct
import sys
import zlib

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 1, 1, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(b'\0\xff\xff\xff\xff')) + chunk(b'IEND', b''))
model = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[0])], nodes=[dict(mesh=0)],
             bufferViews=[], accessors=[], images=[dict(uri='data:image/png;base64,' + base64.b64encode(png).decode())],
             textures=[dict(source=0)], materials=[dict(pbrMetallicRoughness=dict(
                 baseColorFactor=[.1, .8, .2, 1], baseColorTexture=dict(index=0)))])
data = bytearray()


def accessor(fmt, values, component_type, kind, count, **metadata):
    while len(data) % 4:
        data.append(0)
    encoded = struct.pack('<' + fmt, *values)
    view = len(model['bufferViews'])
    model['bufferViews'].append(dict(buffer=0, byteOffset=len(data), byteLength=len(encoded)))
    data.extend(encoded)
    result = len(model['accessors'])
    model['accessors'].append(dict(bufferView=view, componentType=component_type, type=kind, count=count, **metadata))
    return result


pos = accessor('9f', [-.8, -.8, 0, .8, -.8, 0, 0, .8, 0], 5126, 'VEC3', 3,
               min=[-.8, -.8, 0], max=[.8, .8, 0])
idx = accessor('3H', [0, 1, 2], 5123, 'SCALAR', 3)
uv = accessor('6f', [0, 0, 1, 0, .5, 1], 5126, 'VEC2', 3)
normal = accessor('9f', [0, 0, 1] * 3, 5126, 'VEC3', 3)
joints = accessor('12H', [0] * 12, 5123, 'VEC4', 3)
weights = accessor('12f', [1, 0, 0, 0] * 3, 5126, 'VEC4', 3)
morph = accessor('9f', [.12, 0, 0] * 3, 5126, 'VEC3', 3, min=[.12, 0, 0], max=[.12, 0, 0])
times = accessor('2f', [0, 1], 5126, 'SCALAR', 2, min=[0], max=[1])
movement = accessor('6f', [0, 0, 0, .1, .1, 0], 5126, 'VEC3', 2)
model['buffers'] = [dict(byteLength=len(data), uri='data:application/octet-stream;base64,' + base64.b64encode(data).decode())]
model['meshes'] = [dict(primitives=[dict(attributes=dict(POSITION=pos, TEXCOORD_0=uv), indices=idx, material=0)])]


def write(name, asset):
    (out / (name + '.gltf')).write_text(json.dumps(asset))


write('triangle', model)
animated = copy.deepcopy(model)
animated['animations'] = [dict(samplers=[dict(input=times, output=movement)],
                               channels=[dict(sampler=0, target=dict(node=0, path='translation'))])]
write('animated', animated)
skin = copy.deepcopy(animated)
skin['nodes'] = [dict(mesh=0, skin=0), dict()]
skin['scenes'][0]['nodes'] = [0, 1]
skin['skins'] = [dict(joints=[1])]
skin['animations'][0]['channels'][0]['target']['node'] = 1
skin['meshes'][0]['primitives'][0]['attributes'].update(JOINTS_0=joints, WEIGHTS_0=weights, NORMAL=normal)
write('skin', skin)
combined = copy.deepcopy(skin)
combined['meshes'][0].update(weights=[.5])
combined['meshes'][0]['primitives'][0]['targets'] = [dict(POSITION=morph)]
write('skin-morph', combined)
morphed = copy.deepcopy(model)
morphed['meshes'][0].update(weights=[.5])
morphed['meshes'][0]['primitives'][0]['targets'] = [dict(POSITION=morph)]
write('morph', morphed)
lit = copy.deepcopy(model)
lit['extensionsUsed'] = ['KHR_lights_punctual']
lit['extensions'] = dict(KHR_lights_punctual=dict(lights=[dict(type='directional', color=[1, 1, 1], intensity=2)]))
lit['nodes'].append(dict(extensions=dict(KHR_lights_punctual=dict(light=0))))
lit['scenes'][0]['nodes'].append(1)
lit['meshes'][0]['primitives'][0]['attributes']['NORMAL'] = normal
lit['materials'][0]['pbrMetallicRoughness'].update(metallicFactor=0, roughnessFactor=.8)
write('light', lit)
alpha = copy.deepcopy(model)
alpha['materials'][0].update(alphaMode='BLEND', doubleSided=True)
alpha['materials'][0]['pbrMetallicRoughness']['baseColorFactor'][3] = .6
write('alpha', alpha)
bad = copy.deepcopy(model)
bad['buffers'][0]['byteLength'] = 2**32
write('oversized', bad)
bad = copy.deepcopy(model)
bad['nodes'][0]['children'] = [0]
write('cycle', bad)

# A GLB with the same geometry; no dependency on external test files.
glb = copy.deepcopy(model)
glb['buffers'][0].pop('uri')
json_bytes = json.dumps(glb).encode()
json_bytes += b' ' * (-len(json_bytes) % 4)
bin_bytes = bytes(data) + b'\0' * (-len(data) % 4)
encoded = (struct.pack('<III', 0x46546c67, 2, 12 + 8 + len(json_bytes) + 8 + len(bin_bytes))
           + struct.pack('<II', len(json_bytes), 0x4e4f534a) + json_bytes
           + struct.pack('<II', len(bin_bytes), 0x004e4942) + bin_bytes)
(out / 'triangle.glb').write_bytes(encoded)
bad = copy.deepcopy(skin)
bad_data = bytearray(data)
joint_offset = model['bufferViews'][model['accessors'][joints]['bufferView']]['byteOffset']
struct.pack_into('<H', bad_data, joint_offset, 64)
bad['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(bad_data).decode()
write('bad-joint', bad)

(out / 'environment.hdr').write_bytes(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n' + bytes([128, 128, 128, 129]) * 4)

# Non-byte-aligned DDS rows used to let the final pixel read beyond its row.
dds = bytearray(128 + 31)
dds[:4] = b'DDS '
for offset, value in [(12, 1), (16, 8), (80, 0x41), (88, 31), (92, 0xff), (96, 0xff00), (100, 0xff0000), (104, 0xff000000)]:
    struct.pack_into('<I', dds, offset, value)
bad = copy.deepcopy(model)
bad['images'][0]['uri'] = 'data:image/vnd-ms.dds;base64,' + base64.b64encode(dds).decode()
write('bad-dds', bad)
