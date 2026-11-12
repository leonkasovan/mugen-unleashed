#!/usr/bin/env python3
"""Assert the 3D review regressions in the captures test_model.sh produced.

Each finding produced a visibly wrong frame. These checks encode the symptom, so
the fix cannot silently regress:

  shared-skin     two mesh nodes sharing one skin, the first translated: the
                  joint palette includes the inverse of its own mesh node, so
                  skinning must cancel that translation and both instances must
                  land exactly where the untranslated triangle does.
                  Before the fix one instance was displaced (604 pixels).
  skin-normalmap  a normal-mapped skinned surface must not be black. The
                  skinned branch left the bitangent at zero, and the fragment
                  shader normalized it.
Normals are deliberately not covered here: a cancelling normal such as
(-1,0,1) was treated as absent by the old x+y+z presence test, but on this
driver the capture is the same either way, so a pixel regression would not be
able to tell the fixed shader from the buggy one. tools/test_model_vita_api.sh
compiles the shaders; the presence test itself has no runtime cover.
"""
import sys
from pathlib import Path

import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('captures', type=Path)
options = parser.parse_args()

captures = options.captures
SIZE = 64 * 64 * 4


def load(name):
    path = captures / (name + '.rgba')
    if not path.is_file():
        raise SystemExit(f'{name}: missing capture {path}')
    data = path.read_bytes()
    if len(data) != SIZE:
        raise SystemExit(f'{name}: {len(data)} bytes, expected {SIZE}')
    return data


def differing(a, b):
    return sum(1 for i in range(0, SIZE, 4) if a[i:i + 4] != b[i:i + 4])


def check(name, ok, detail):
    print(f'{name}: {detail}')
    if not ok:
        raise SystemExit(f'{name}: FAILED')


checks = 0

# 1. Shared skin: the translated instance must be skinned back onto the other.
triangle = load('triangle')
shared = load('shared-skin')
moved = differing(triangle, shared)
check('shared-skin', moved == 0,
      f'{moved} pixels differ from the untranslated triangle (expected 0)')
checks += 1

# 2. An identity skin must not change a normal-mapped surface: the skinned twin
#    has to render exactly like the unskinned one. It used to render black.
plain = load('normalmap')
skinned = load('skin-normalmap')
lit_pixels = sum(1 for i in range(0, SIZE, 4) if plain[i] + plain[i + 1] + plain[i + 2] > 24)
changed = differing(plain, skinned)
check('skin-normalmap', lit_pixels > SIZE // 4 // 8 and changed == 0,
      f'{lit_pixels} lit pixels unskinned, {changed} pixels differ under an identity skin '
      f'(expected lit and 0 differences)')
checks += 1

print(f'ok: {checks} review regressions')