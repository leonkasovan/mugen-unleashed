# mugen-unleashed

A source-level reconstruction of **M.U.G.E.N 1.1.0 Beta 1 P1** (Elecbyte, built
2013-08-11): the original 32-bit Windows binary, rewritten as C and rebuilt from
those sources.

The project aims for *behavioural* fidelity with the original — gameplay, timing,
file formats, log output — not merely a similar-looking game. Where the
reconstruction knowingly deviates, the deviation is marked in the source as a
port change and recorded in the notes under `docs/`.

## What you get

Two Windows builds are supported, and they differ in how they ship:

| Build | Executable | Runtime requirements |
|---|---|---|
| **GNU Make + MinGW (static)** | `mugen-mingw-win32-sdl2.exe` | Nothing but Windows system DLLs |
| **MSVC + CMake** | `mugen-msvc-win32-sdl1.exe` | The game's `Elecbyte.MUGEN.libs` folder (SDL, SDL_mixer, SDL_ttf, SDL_gfx, zlib, libresample, pthreads) |

The static build is self-contained: SDL2, SDL2_mixer, SDL2_ttf, zlib, pthreads,
Lua 5.1.4, libpng, glad, minizip and libresample are all linked in. Because
SDL2_mixer carries mpg123 and vorbisfile internally, WAV, MP3 and Ogg Vorbis
play without any external decoder DLL — including without Elecbyte's
`libmpg123-0.dll`, `vorbisplug.dll` or `waveplug.dll`.

The engine also builds on Linux from the same `Makefile` (x86, x64, arm64), and
there are Android and PS Vita ports under `port/`.

## Building

### Static Windows build (recommended)

Requires `i686-w64-mingw32` GCC, GNU Make, and the static SDL2 stack discoverable
through `pkg-config` (`sdl2`, `SDL2_mixer`, `SDL2_ttf`, `zlib`, `libpng16`).

```sh
make check-toolchain     # verifies the compiler and the static archives
make -j8                 # build/mingw-sdl2/mugen-mingw-win32-sdl2.exe
make install             # strip it and copy it into install/
```

`install/` is a M.U.G.E.N 1.1b1 tree; drop the executable in and run it. No
`Elecbyte.MUGEN.libs` folder is needed.

The SDL2 backend is the default. `make SDL_BACKEND=1` builds the older SDL 1.2
backend instead (`build/mingw-sdl1/mugen-mingw-win32-sdl1.exe`), which exists for the
differential comparisons against the original binary, not as the normal target.

See **[docs/BUILD-MINGW.md](docs/BUILD-MINGW.md)** for the dependency list,
toolchain versions and the variables `make` accepts.

### MSVC 2022

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

Produces `build/x86-release/mugen-msvc-win32-sdl1.exe`. This build links the game's
original DLLs rather than static copies. See
**[docs/BUILD-MSVC.md](docs/BUILD-MSVC.md)**.

### Linux

```sh
make -j8
```

The target (`linux-x64`, `linux-x86`, `linux-arm64`) is chosen from the host and
the compiler's own triple. See **[docs/BUILD-LINUX.md](docs/BUILD-LINUX.md)**.

### MinGW Win64 (experimental)

With the 64-bit kit first on `PATH` there is nothing to ask for — the Makefile
reads the compiler's triple and builds the 64-bit target, printing a note that
it did:

```sh
export PATH=/c/w64devkit/bin:$PATH
export PKG_CONFIG_PATH=/c/w64devkit/lib/pkgconfig
make -j8
```

Produces `build/mingw64-sdl2/mugen-mingw-win64-sdl2.exe`. `make
MUGEN_TARGET=win64 BUILD=build/win64-sdl2 -j8` pins the target and the
directory explicitly. See **[docs/BUILD-MINGW64.md](docs/BUILD-MINGW64.md)**.

### Other platforms

- **Android** — `port/android/build.sh` (native SDL2, arm64-v8a).
- **PS Vita** — `port/psvita/build.sh` (VitaSDK, SDL 1.2 over vitaGL). It
  downloads and installs VitaSDK to `C:\vitasdk` if it is not there, sets up the
  environment, and builds `build/psvita/mugen-psvita.vpk`. See
  [port/psvita/README.md](port/psvita/README.md).
- **Linux arm64 cross build** — `port/arm64/build.sh`.

## Requirements on the machine

The engine is a faithful reconstruction of a 2013 binary, so it inherits its
platform expectations:

- **32-bit is the reference.** The struct layouts, offsets and assertions in
  `src/` describe the original 32-bit binary. Win64 builds link and run (see
  `docs/BUILD-MINGW64.md`): a Win64-vs-Win32 log comparison on the same
  scenario is identical after numeric normalisation. Linux ARM64/x64 link
  but are not yet qualified the same way.
- **x87 float semantics on x86.** The original ran with `/arch:IA32` at 53-bit
  precision; the MinGW builds pass `-mfpmath=387 -fexcess-precision=standard`
  so float rounding matches. Changing this alters gameplay-visible arithmetic.
- **A game directory.** The engine reads `mugen.cfg`, `data/system.def`, fonts,
  stages and characters from the working directory.

## Documentation

| Document | Contents |
|---|---|
| [docs/ANALYSIS.md](docs/ANALYSIS.md) | The original binary: module map, addresses, calling conventions |
| [docs/BUILD-MINGW.md](docs/BUILD-MINGW.md) | The static Windows build in detail |
| [docs/BUILD-MSVC.md](docs/BUILD-MSVC.md) | The MSVC/CMake build in detail |
| [docs/BUILD-LINUX.md](docs/BUILD-LINUX.md) | The Linux build |
| [port/psvita/README.md](port/psvita/README.md) | The PS Vita build (`port/psvita/build.sh`), packaging and on-console diagnosis |
| [docs/ANDROID.md](docs/ANDROID.md) | The Android port |
| [docs/LUA.md](docs/LUA.md) | The embedded Lua 5.1.4 layer |
| [SDL2_PLAN.md](SDL2_PLAN.md) | The SDL 1.2 → SDL2 migration |
| [PLANS.md](PLANS.md) | Working plan and open items |
| [AGENTS.md](AGENTS.md) | Conventions for working on the code |
| [third_party/README.md](third_party/README.md) | Third-party code, versions and licences |

## Testing

```sh
make check-toolchain         # toolchain and static archives present
bash tools/test_zoomdiff.sh  # SDL2 scaler vs SDL_gfx, byte for byte
```

`tools/test_zoomdiff.sh` builds the same 21 scaling cases against the SDL 1.2
SDL_gfx and the SDL2 replacement and requires every result to be identical. It is
the guard against silent divergence in software rendering.

## Legal

M.U.G.E.N is © Elecbyte. This is an unofficial, non-commercial reconstruction
made for study and preservation. It ships no copyrighted game assets: the
characters, stages, fonts and sound files under `install/` belong to their
respective authors. Third-party libraries keep their own licences, listed in
[third_party/README.md](third_party/README.md).

## 3D stages

The merged upstream code adds 3D stages to the engine, compatible with the ones made for
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

Screens and storyboards can have a 3D background too, as in Ikemen GO. The motif (`[Files] model`)
or the storyboard (`[SceneDef] model`) names the model. Each background group then picks a scene of
it and places a fixed camera:

```ini
[TitleBGdef]
scenenumber = 0          ; the glTF scene to draw (none without)
fov = 40
near = 0.1
far = 1000
modeloffset = 0, -1.5, -8
modelrotate = 10, 20, 0  ; degrees
modelscale = 0.015, 0.015, 0.015
```

The code is in `src/model.c` (glTF models), `src/stage3d.c` (stages) and `src/bg3d.c` (screen and
storyboard backgrounds). Where the original code calls into it, a `mugen-unleashed:` comment marks
the change.


Desktop Makefile builds enable this with `MUGEN_MODEL3D=1` (default when
`MUGEN_GLES=0`); `MUGEN_MODEL3D=0` disables it. The MSVC CMake build has the
matching `MUGEN_MODEL3D` option. This renderer uses desktop GLSL 1.20, with
GLSL 1.30/OpenGL 3.0 for environment maps. VitaGL and GLES/Android retain their
existing 2D renderers; desktop model shaders are not enabled on those targets.
Unsupported model requests are logged.

See [the merge and validation record](docs/UPSTREAM_MERGE.md) for the exact
upstream revision, compatibility choices and remaining qualification limits.
