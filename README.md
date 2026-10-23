# mugen-unleashed

A C reconstruction of the M.U.G.E.N 1.1 Beta 1 engine (`mugen.exe` 1.1.0 Beta 1 P1,
built on 2013-08-11 by Elecbyte), recovered from the original executable. It builds into a
native `mugen.exe` that runs the game the way the original does.

This repository contains **no game data and none of Elecbyte's files**. To run what it builds,
you need your own copy of M.U.G.E.N 1.1 Beta 1.

## Status

- **The whole engine is reconstructed.** Every function of the game code is in C. The
  libraries the original linked statically (Lua 5.1.4, libpng 1.2.38, GLEW 1.5.4 and minizip
  0.15) are built from their original sources, in the same versions.
- **It behaves like the original.** Each function was checked against the original binary with
  differential tests. The rebuilt executable writes the same `mugen.log` as the original on:
  - the title screen;
  - a quick VS match and AI vs AI matches;
  - the character select demo;
  - the options screen and its menus;
  - storyboards;
  - `-nosound` and `-nomusic`;
  - the error dialog of a missing character.

  This holds for both the Debug and the optimised builds.
- **It reproduces the original's quirks and bugs.** Fixes and new features are left for later
  and should be marked as such in the code.

## Building

GitHub Actions builds the release executable on every push. To get it without building:
- download the `mugen-unleashed-x86` artifact of the latest run (the Actions tab);
- or download the zip attached to a GitHub release. Releases are tagged after the engine
  version they reconstruct, starting with `v1.1b1`.

To build it yourself, you need:

- Windows with Visual Studio 2022, including the x86 C++ tools. The CMake and Ninja that come
  with it are enough.
- Optional: Python 3 with `pefile` (`pip install pefile`). The build uses it to take the window
  icon from your original `mugen.exe`.

From a regular command prompt:

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

The `x86-debug` preset builds an unoptimised executable instead. The executable ends up in
`build\<preset>\mugen.exe`.

To embed the original window icon, point the configure step at your M.U.G.E.N directory:

```bat
tools\msvc_env.bat cmake --preset x86-release -DMUGEN_GAME_DIR=C:/path/to/mugen-1.1b1
```

## Running

1. Copy `mugen.exe` into your M.U.G.E.N 1.1 Beta 1 directory, under another name if you want
   to keep the original (for example `mugen_unleashed.exe`).
2. Run it from there.

It needs the `Elecbyte.MUGEN.libs` folder of the game, which holds SDL, SDL_mixer, SDL_ttf,
SDL_gfx, zlib, libresample and pthreads. The executable's manifest finds those DLLs there, as
the original's does. Like the original, it writes `stdout.txt` and `stderr.txt` next to itself
and deletes them when they stay empty.

## 3D stages

This branch adds 3D stages to the engine, compatible with the ones made for
[Ikemen GO](https://github.com/ikemen-engine/Ikemen-GO). This is new code, not part of the
original. A stage gets a glTF 2.0 model (`.glb` or `.gltf`) through its `.def`:

```ini
[Camera]
fov = 30                 ; vertical field of view in degrees (default 40)

[BGdef]
model = mystage.glb      ; relative to the stage's directory
spr = mystage.sff        ; still used for the stage portrait

[Model]
offset = 0, -0.16, -0.7  ; x, y, z of the model in the view; z is the depth of the ground
scale = 0.0575, 0.0575, 0.0575
```

As in Ikemen GO:
- the `[Model] offset` y moves the stage's `zoffset` onto the ground of the model;
- a stage with a model needs no `[BG]` element;
- `[Camera] near`, `far` and `yshift` are read as well.

The model follows the camera. It is drawn under the `[BG]` elements of the back layer. Nodes
with a `layerNumber` extra of 1 are drawn in front of the players, together with the model's
second scene.

What is supported so far:
- node hierarchies;
- base colour textures (PNG, JPEG and uncompressed RGBA DDS, the formats Ikemen GO reads) and
  vertex colours;
- `alphaMode` and `doubleSided`;
- `KHR_texture_transform`;
- node animations (linear, step and cubic spline), skins and morph targets (with weight
  animations);
- `KHR_animation_pointer` on the targets Ikemen GO animates: node transforms and weights, mesh
  weights, material factors, alpha cutoff, occlusion strength and texture transforms, light
  colour, intensity, range and cone angles;
- punctual lights (`KHR_lights_punctual`: directional, point and spot, up to 4 per scene);
- PBR materials (metallic-roughness, normal, occlusion and emission maps,
  `KHR_materials_unlit`);
- environment maps (image based lighting) from an equirectangular Radiance HDR panorama:
  `[Model] environment = sky.hdr` and `environmentintensity` (1);
- shadow maps, with the light and node extras `shadowMapNear`, `shadowMapFar`,
  `shadowMapLeft`, `shadowMapRight`, `shadowMapBottom`, `shadowMapTop` and `shadowMapBias`.
  `[Video] EnableModelShadow = 0` in `mugen.cfg` turns them off;
- the Ikemen GO node extras `trans`, `disableZTest`, `disableZWrite`, `castShadow`,
  `layerNumber`, `id` and `meshOutline` (with the `_OUTLINE_ATTRIBUTE` vertex attribute);
- the animation extras `id`, `loopCount` and `enabled`;
- `[BGCtrl3D]` groups of type `Anim` and `Visible`, aimed at the `id` extras of animations and
  nodes by `ctrlid`, on Ikemen GO's timing.

A model without lights or an environment map is drawn unlit, as Ikemen GO does. Like Ikemen GO,
the engine ignores a second set of joints (`JOINTS_1` / `WEIGHTS_1`). 3D stages need
the OpenGL renderer. Shadows need float textures (OpenGL 3.0 or `ARB_texture_rg` /
`ARB_texture_float`), environment maps OpenGL 3.0. Problems with models are written to
`mugen.log`.

The code is in `src/model.c` (glTF models) and `src/stage3d.c` (the stage side). Where the
original code calls into it, a `mugen-unleashed:` comment marks the change.

## Layout

```
src/            the engine, one .c file per module of the original (see docs/ANALYSIS.md)
  mugen.h       common header; the type headers it includes describe the original's structs
  symbols.h     prototypes and globals of the whole program
  globals.c     the globals, with their initial values from the original's .data
  sdlmain.c     SDL 1.2's SDLmain (WinMain), rebuilt from the original
  lookup3.c     Bob Jenkins' hashlittle, as specialised by the original's linker
third_party/    Lua, libpng, GLEW, minizip, headers and import definitions of the game's DLLs
cmake/          build helpers
tools/          msvc_env.bat (Visual Studio environment), extract_icon.py
docs/           notes on the original binary and how the code reproduces it
```

Each function carries a comment with the address of its counterpart in the original
executable, so you can compare the two in a disassembler.

## Legal

M.U.G.E.N is a trademark of Elecbyte. The original engine is © 1999-2013 Elecbyte. The code
in `src/` was reconstructed from their binary for preservation and study. It is not affiliated
with or endorsed by Elecbyte, and no license is granted for it beyond what applicable law
allows.

The libraries in `third_party/` are distributed under their own licenses (see
`third_party/README.md`).
