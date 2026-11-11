# mugen-unleashed

A C reconstruction of M.U.G.E.N 1.1 Beta 1, with portable SDL2 backends and
optional upstream glTF stages. Engine behavior and original address/layout
comments remain the reference; platform fixes are documented in source.

**SDL2 is the only supported SDL version.** Root MSVC/CMake support has been
retired. Android and PS Vita retain their own CMake projects.

| Platform | Entry point | Dependencies/rendering |
|---|---|---|
| Windows 32/64-bit | GNU Make + matching MinGW-w64 kit | Static SDL2, SDL2_mixer, SDL2_ttf; System/desktop GL |
| Linux x86-64 | GNU Make | SDL2 development packages; System/desktop GL or GLES |
| Linux ARM64 | `port/arm64/build.sh` | SDL2 target sysroot; System/GLES |
| Android | `port/android/build.sh` | Gradle/NDK CMake, native SDL2; System/GLES |
| PS Vita | `port/psvita/build.sh` | VitaSDK CMake, native SDL2; System/native vitaGL |

## Windows

```bat
setup.bat /build32
setup.bat /build64
```

Or put the desired kit on PATH and run `make check-toolchain` then `make -j8`.
The compiler triple selects the architecture. Outputs keep their existing
names: `build/mingw-sdl2/mugen-mingw-win32-sdl2.exe` and
`build/mingw64-sdl2/mugen-mingw-win64-sdl2.exe`. `make install` copies a stripped
binary to `install/`. See [the MinGW guide](docs/BUILD-MINGW.md) and
[the Win64 notes](docs/BUILD-MINGW64.md).

## Linux

```sh
sudo apt-get install build-essential pkg-config libsdl2-dev libsdl2-mixer-dev \
    libsdl2-ttf-dev libpng-dev zlib1g-dev libglu1-mesa-dev
make -j8
```

The x86-64 output is `build/linux-x64-sdl2/mugen-linux-x64`. ARM64 defaults to
GLES; `MUGEN_GLES=0|1` selects the desktop/GLES renderer. See
[the Linux guide](docs/BUILD-LINUX.md) and [ARM64 setup](port/arm64/README.md).

## Android and PS Vita

Use [Android instructions](port/android/README.md) or
[PS Vita instructions](port/psvita/README.md). The root Makefile does not build
these ports. VitaGL retains exclusive ownership of the Vita GL display; SDL2
provides input/audio and the System presenter. Desktop model shaders are not
enabled on Vita/GLES.

## Game data and settings

The engine requires a compatible game tree in `install/`; existing assets and
`mugen.cfg` settings are retained. `tools/fetch_game_data.sh` can populate an
empty installation. Keyboard/gamepad binding numbers and resolution settings
are unchanged by the SDL2-only transition.

## Validation and migration record

[SDL2_ONLY_PLAN.md](docs/SDL2_ONLY_PLAN.md) records the implementation order,
acceptance criteria, validation results and remaining hardware checks. The old
SDL1-capable build remains retrievable at commit `6753f6b`; active builds and
tests do not require its libraries or headers.

```sh
python3 tools/check_sdl2_only.py
sh tools/test_zoomdiff.sh
sh tools/test_render_baseline.sh
sh tools/test_sdl2helpers.sh
sh tools/test_sdl2input.sh
SDL_AUDIODRIVER=dummy sh tools/test_sdl2audio.sh
```

The scaler/render suites compare against committed synthetic baseline hashes.
They never regenerate expected values from the candidate implementation.
Other rendering, config, gamepad and platform tests remain under `tools/` and
the platform test directories. Missing runtime capabilities report SKIP.

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
`MUGEN_GLES=0`); `MUGEN_MODEL3D=0` disables it. This renderer uses desktop GLSL 1.20, with
GLSL 1.30/OpenGL 3.0 for environment maps. VitaGL and GLES/Android retain their
existing 2D renderers; desktop model shaders are not enabled on those targets.
Unsupported model requests are logged.

See [the merge and validation record](docs/UPSTREAM_MERGE.md) for the exact
upstream revision, compatibility choices and remaining qualification limits.

## Legal

M.U.G.E.N is © Elecbyte. Third-party libraries retain their license notices;
see [third_party/README.md](third_party/README.md). Game assets belong to their
respective authors and are not part of this source change.
