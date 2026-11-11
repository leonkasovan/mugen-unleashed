# zxmarcos main integration

> Historical record. For the current SDL2-only build and removal of root
> MSVC/CMake, see [docs/SDL2_ONLY_PLAN.md](SDL2_ONLY_PLAN.md).

## Revisions and history

This merge integrates `zxmarcos/mugen-unleashed` `main` at
`cc94cfe53d98951c1c0f87610616169984db1217` into the SDL2 branch based on
`7af368e99ba64738c4e047d15c9622d3b4e8913f`.

The histories have no shared Git ancestor. They are independent imports of
nearly the same reconstructed engine. The upstream initial release
`eea065966ab4c6557f811d707ec05221ba0f7ae9` is the **explicit content merge
base**, not a claim of shared ancestry. Applying the nine subsequent upstream
feature commits against that snapshot preserves our platform and fidelity
work. The merge commit retains the original SDL2 head as first parent and
upstream's head as second parent, so both histories remain reachable.

The content merge was calculated with:

```sh
git merge-tree --write-tree \
  --merge-base=eea065966ab4c6557f811d707ec05221ba0f7ae9 \
  7af368e99ba64738c4e047d15c9622d3b4e8913f \
  cc94cfe53d98951c1c0f87610616169984db1217
```

This produced textual conflicts in `CMakeLists.txt` and `README.md`.

## What was integrated

Upstream adds glTF stages and screen/storyboard backgrounds, node animation,
skinning, morphs, outlines, BGCtrl3D, punctual lights, PBR, shadow maps,
environment maps, JPEG/DDS textures and animation pointers. These are engine
extensions rather than reconstructed behavior. Existing structures retain
their layout; stage/background model state is stored separately.

The imported cgltf 1.14 and stb_image 2.30 sources remain vendored with their
license notices. They are compiled statically for desktop model support;
there are no additional runtime DLL requirements from those libraries.

## Conflict and portability decisions

- Keep our root CMake source list, glad loader and existing build settings;
  add the model modules and shader generation. Do not restore upstream GLEW.
- Keep our README and add upstream's 3D usage documentation.
- Keep all Windows/Linux/Android/Vita ports, tests and build scripts. No
  upstream snapshot deletions are applied to those directories.
- Root Make builds enable `MUGEN_MODEL3D=1` for desktop GL; setting it to `0`
  selects the disabled implementation. Root MSVC CMake exposes the matching
  option. The configuration stamp includes the option, so changing it
  rebuilds architecture/configuration-sensitive objects.
- GLES builds default to `MUGEN_MODEL3D=0`; explicitly requesting both is a
  Make error. Android and PS Vita retain their existing 2D renderers and
  native SDL2. Their source globs compile `model_stub.c`, with no desktop
  shaders or cgltf/stb dependency. Actual model requests log an unsupported
  message; model-only backgrounds cannot silently become empty 2D scenes.
- Desktop models require a live OpenGL renderer. System rendering rejects a
  model request before file loading or GL texture work.
- Base model shaders use GLSL 1.20. Image-based lighting needs desktop GL 3.0
  and GLSL 1.30. Existing glad GL 2.1/EXT FBO entry points cover the calls;
  higher feature detection uses the context version and exact extension
  tokens rather than GLEW globals.
- Both Make and CMake embed shader strings without losing quotes, brackets,
  semicolons or backslashes. Stable Make output keeps its modification time.
- The new PNG memory decoder supports old/new libpng gray-expansion APIs and
  rejects null arguments and RGBA/row allocation sizes that cannot fit the
  address space on 32-bit or 64-bit targets.

## Integration defects corrected

The real GL fixture exposed an upstream draw failure when a model had no
environment map: cube samplers retained unit 0, aliasing a 2D sampler. On a
GL 3+ context this generated `GL_INVALID_OPERATION` and drew no model.
Environment sampler units are now set even when the environment is null.
The fixture failed before this fix and renders afterward.

Compiled/attached shaders are released with their programs, including failed
compile/link paths. Shared model/shadow program and FBO caches are released
before the engine destroys its GL context. A fresh context can rebuild those
caches, and repeated cache teardown is safe. This does not establish recovery
of a live model's VBOs/textures after unplanned GPU context loss.

The existing mocked Vita error-dialog test now models SDL2's message-box
boundary. The storyboard regression harness links the disabled model backend
so the added upstream hooks remain exercised by the existing test.

## Validation on Linux x86-64

All results below were obtained during this integration, using ignored build
and SDK directories. The host is Linux x86-64, GCC and Mesa 25.0.7, with SDL2;
it is not Windows or physical Vita hardware.

| Check | Result |
|---|---|
| Full native Linux desktop GL build | Pass |
| Desktop `MUGEN_MODEL3D=0`, then `=1`, same output tree | Pass; option changes rebuild objects |
| Repeating the unchanged desktop build | Pass; no compilation/link step |
| Full Linux GLES build, models disabled | Pass |
| Native VitaSDK ARM engine, ELF/SELF/VPK generation | Pass; SDL2/VitaGL 2D configuration |
| `tools/test_model.sh`, Mesa offscreen GL 4.5 compatibility | Pass |
| Same model probe with GL 2.1 / GLSL 1.20 overrides | Pass |
| Make/CMake shader generator escape/byte comparison | Pass |
| `tools/test_vita_build.py` | 17 tests pass |
| `tools/test_vita_video.sh` | Both presenter contracts pass, including injected failures |
| `tools/test_vita_sdl2input.sh` | Pass; virtual controllers |
| `tools/test_gamepad_config.sh`, desktop/Android/Vita variants | All pass |
| `tools/test_configmerge.sh` | 42 cases pass |
| `tools/test_gl_errors.sh` | Desktop/GLES/Linux-GLES/Vita mocks and refused-context probe pass |
| `tools/test_build_warnings.sh` | Pass |

The model probe uses production model, file, PNG and GL code. It generates a
small glTF triangle with an embedded RGBA PNG, checks loaded geometry and GPU
texture/buffer handles, checks colored center and black corner pixels, checks
GL errors, frees it, and repeats after destroying/recreating the GL context.
It also checks rejection without a desktop GL context and the disabled port
backend. Missing video/GL capability returns 77 (SKIP), not a false pass.

To repeat on a host with SDL2/libpng/zlib development packages:

```sh
SDL_VIDEODRIVER=offscreen sh tools/test_model.sh
MESA_GL_VERSION_OVERRIDE=2.1 MESA_GLSL_VERSION_OVERRIDE=120 \
  SDL_VIDEODRIVER=offscreen sh tools/test_model.sh
```

`CC`, `CFLAGS`, `LDFLAGS`, `SDL_CFLAGS`, `SDL_LIBS` and `OUT` can select another
host setup. The native Vita build used an empty
`MUGEN_VITA_SHADER_COMPILER` for source/build verification; its generated VPK
was not a replacement for the user's fully bundled deployment.

## Still open

- Windows 32/64-bit MinGW and MSVC builds were not run in this environment.
- An Android NDK/APK build and physical Android/Vita execution were not run.
- Desktop 3D import is present, but complex stages, PBR lighting, shadow maps,
  HDR filtering, morph/skin animation and storyboard integration still need
  representative asset and gameplay testing. The triangle probe is not a
  claim that every imported feature is qualified.
- Hardware presentation, audio, controller behavior and performance need a
  device smoke test after installing a normal package built from this merge.
- Vita/GLES 3D rendering remains a separate porting task. This integration
  preserves their 2D paths; it does not translate desktop GLSL to VitaGL/GLES.
