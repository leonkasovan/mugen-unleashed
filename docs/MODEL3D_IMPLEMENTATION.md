# Portable model renderer: implementation and qualification

Baseline: `sdl2` commit `f97752f`. Work branch: `feature/model3d-portability`.
This implements the first portable paths from [the research plan](MODEL3D_PORTABILITY.md).
Android/Vita remain opt-in. Hardware qualification is still open; a host test
or a successful link does not certify a phone, handheld or Vita GPU.

## Windows reference supplied by the user

The uploaded `mugen(20261112-010902).log` records the Windows 64-bit SDL2/OpenGL
build successfully loading `Stage0_3D_BLACK.glb` twice and `PokemonCenter.glb`
once. The match summaries report 421 / 656 / 863 frames at 58.31 / 60.07 /
60.06 FPS, all with zero skipped frames, followed by successful termination.
The log proves loading and execution; it does not prove visual equivalence,
which material features those models exercised, or correct skin/shadow/IBL output.
The log's BGM file-open failures and absent gamepad enumeration are independent
of the model renderer. The private log itself is not committed.

## What changed

| Area | Implementation |
|---|---|
| CPU assets | `Model_LoadAsset` parses/evaluates asset data without GL. `model_asset.c` decodes images and validates source graphs/counts/joint indices. Decoded RGBA and base vertex/index buffers survive context destruction. |
| GPU residency | `model_gpu.c` owns buffer/texture creation, an explicit live-model registry, upload failure state and context generations. Losing a context drops names without deleting them against a new context. |
| Restore | Draws lazily upload the existing asset. Animation/controller state remains intact. Morph caches are invalidated because restored VBOs start from base geometry. Desktop environment maps retain their source path and are regenerated on restore. |
| State boundary | `model_pass.c` restores program/cache, buffers/attributes, touched texture units, framebuffer, viewport/scissor, blend/depth/cull/color state and supported pixel-unpack state. GLES uses an owned VAO and preserves separate read/draw FBOs. |
| GLES | GLSL ES 3.00 variants reuse the desktop shader algorithms. Shadow declarations/varyings are compiled out. No desktop attribute stack, alpha test, 1D textures or EXT-only framebuffer calls reach this path. |
| Vita basic | A small translated-GLSL profile uses base texture/color, alpha, texture transform and PalFX. Node animations and CPU morphs remain shared. Skinned models are explicitly rejected. Lighting, outlines, shadows and IBL are omitted and identified in the profile log. |
| Builds | Linux Make accepts opt-in GLES models. Android/Vita CMake share static cgltf/stb dependency wiring and byte-preserving shader embedding. No new runtime dependency, SDL1 shim or root MSVC/CMake route is introduced. |

The parser/evaluator and renderer still share `model.c`; this is a lifetime and
ownership separation, not a rewrite of all model math into a new framework.
Only new model structs grow. Original game struct layouts and configuration
resolution values are unchanged.

Two defects exposed by the fixtures are intentionally corrected in the desktop
reference as well:

1. **Skinning across scene roots.** All roots must receive world transforms
   before any skin matrices are calculated. Otherwise a mesh referencing a
   joint in a later root uses an uninitialized transform on its first frame.
2. **Zero-slope self-shadowing.** Directional/spot shadow bias could become zero
   for a perpendicular surface; strict depth comparisons then shadowed the
   surface itself. The minimum bias is now applied after slope scaling. The
   synthetic directional-light plane rendered black before this correction.

## Profiles and activation

| Feature | Desktop GL | GLES 3.0 | Vita basic |
|---|---|---|---|
| glTF/GLB stages and screen backgrounds | Implemented reference | Implemented, experimental | Implemented, experimental |
| Rigid meshes, base textures, alpha, PalFX | Yes | Yes | Yes |
| Node animation and CPU morphs | Yes | Yes | Yes |
| GPU skinning, up to 64 joints per skin | Yes | Yes | Rejected |
| Punctual lighting/PBR and outlines | Yes | Implemented | Omitted |
| Model shadows | Existing desktop path, bias fix | Unsupported | Unsupported |
| Runtime HDR environment preprocessing | Existing desktop path, reload source retained | Unsupported | Unsupported |
| Device validation of these changes | Windows user log predates changes; Linux host tests | Mesa host tests only | Host basic shader/API checks only |

`MUGEN_MODEL3D` is a build option. The runtime rendering route remains
`RenderMode = OpenGL`. System/OpenGLScreen cannot draw model content; a 2D
fallback must be authored separately. `EnableModelShadow` does not enable 3D
and cannot enable effects absent from the selected profile.

```sh
# Desktop reference, unchanged default model selection
make

# Linux GLES; remains explicitly opt-in
make MUGEN_GLES=1 MUGEN_MODEL3D=1

# Android: compiles model support and implies --gles
port/android/build.sh --model3d

# Vita: compiles the basic profile, which the build turns on by default
port/psvita/build.sh --model3d
port/psvita/build.sh --no-model3d    # compile it out again
```

On Vita this profile is restricted (skinning rejected, lighting/outlines/shadow
/IBL omitted) and its hardware qualification is still open, so enabling it by
default compiles support in rather than certifying it -- see the status table in
this document before relying on it on a device.

Both platform CMake entry points accept `-DMUGEN_MODEL3D=ON`. Android additionally
requires `-DMUGEN_GLES=ON`; incompatible direct configurations fail clearly.
Shaders are embedded into the executable/library; no new model shader files
need copying into the game installation. Model assets and external textures
must still be present in the packaged game data. The existing Vita shader
compiler loading/bundling mechanism is unchanged.

The retained geometry/index/decoded-texture budget defaults to 16 MiB per model
on Vita and 256 MiB elsewhere, with a compile-time `MODEL_CPU_BUDGET` override.
This is not a complete process-memory cap: cgltf storage, model metadata, morph
targets, temporary decoding and GPU copies also consume memory. Large desktop
assets may need an authored handheld version. Images are checked before large
PNG/JPEG/DDS allocations, source graphs are bounded, and out-of-palette joint
indices are rejected before shader access. These checks are not a complete
security audit of every glTF extension/decoder.

Loading now requires cgltf validation and successful decoding of referenced
textures. Malformed assets tolerated by the previous permissive loader may
need correction. The retained backing budget is also a new compatibility
constraint; verify the user's real stage assets before treating the desktop
reference as fully qualified.

## Repeatable host validation

```sh
OUT="$PWD/build/tmp/model-desktop" SDL_VIDEODRIVER=offscreen sh tools/test_model.sh
OUT="$PWD/build/tmp/model-gles" MODEL_TEST_GLES=1 SDL_VIDEODRIVER=offscreen sh tools/test_model.sh
OUT="$PWD/build/tmp/model-basic" MODEL_TEST_BASIC=1 SDL_VIDEODRIVER=offscreen sh tools/test_model.sh
python3 tools/compare_model_frames.py build/tmp/model-desktop build/tmp/model-gles
python3 tools/compare_model_frames.py --basic build/tmp/model-desktop build/tmp/model-basic
python3 tools/test_model_build.py
sh tools/test_model_vita_api.sh
python3 tools/test_vita_build.py
python3 -m unittest discover -s port/android/tests -p test_build.py
```

Use distinct `OUT` directories when running model variants concurrently.
The generated fixtures cover rigid geometry, node animation, GPU skinning,
morphs, combined skin+morph, alpha, a punctual light, binary GLB and invalid
oversized/cyclic/out-of-palette assets and malformed DDS rows. Each successful fixture is drawn before
and after destruction of the context while preserving the same model object.
Whole RGBA frames must agree across restoration, and caller GL state must be
restored. The desktop lighting case requests a real shadow FBO when supported. The
basic profile's skin rejection is tested explicitly.

`test_model_vita_api.sh` compiles model sources against the committed VitaGL
declarations, using opaque SDK type stubs and host SDL headers. It is an API
syntax check, not a Vita ABI, native link, shader-compiler or device test.
`test_model_build.py` configures the real shared CMake shader target and checks
the compiled embedded bytes against every source shader. The Android build
tests mock the SDK/Gradle and check option propagation; they do not produce a
real APK. Missing GL capability exits 77 rather than passing silently.

Desktop IBL lifecycle smoke tests can use small compile-time dimensions/sample
counts (`MODEL_ENV_SIZE`, `MODEL_ENV_LOWEST_MIP`, `MODEL_ENV_LUT_SIZE`, and
`MODEL_ENV_*_SAMPLES`) to keep software rendering practical. Set
`MODEL_TEST_ENV` only on the generated `light.gltf` probe invocation, pointing
at its generated `environment.hdr`. This tests real float FBO/shader/resource
recreation code with reduced work; it is not production-size IBL quality or
performance qualification. Production defaults remain unchanged.

## Still required before enabling mobile/Vita defaults

1. Build with the real Android NDK and VitaSDK, including native link and
   APK/VPK packaging; verify shader compiler logs on Vita. Host syntax/build
   orchestration checks do not close these gates.
2. Run licensed representative stages on Windows 32/64, native Linux ARM64,
   Android and Vita. Compare camera, layer ordering, characters/UI, translucent
   meshes, outlines, skins/morphs and texture orientation, with screenshots.
3. Exercise the complete app lifecycle: background/resume, rotation/resize,
   context recreation during an animated stage, unload/return to menu and
   restore/allocation failure. The isolated model probe does not replace this.
4. Measure drawn/skipped frames, CPU/GPU costs, peak memory and repeated-stage
   memory growth at unchanged resolution. State capture adds work and retained
   assets add memory; no FPS improvement or fixed frame-rate claim is made.
5. Qualify a bounded Vita skinning/lighting profile independently. Do not lift
   the current skin rejection or feed the desktop shader into VitaGL merely
   because the basic profile loads a model. Optional GLES/Vita shadows/IBL need
   format/filtering probes and a separate memory/performance budget.

Test execution details for this workspace are recorded below as checks finish;
device/ABI rows remain OPEN until actual evidence exists.


## Execution record: 2026-11-12

Host: Linux x86_64, GCC 14.2.0, Mesa 25.0.7. Dependencies were unpacked into
this project's ignored build directory; no system-wide package installation
or game configuration changes were made. Full engine links used explicit
SDL2/mixer/ttf/png/zlib library/include overrides for that local dependency tree.

| Check | Result |
|---|---|
| Full Linux desktop engine build, model support ON | PASS |
| Full Linux GLES engine build, model support ON | PASS |
| Desktop GL 4.5 fixture suite, including shadow path and live-model restore | PASS |
| Desktop GL 2.1 / GLSL 1.20: seven named rendering fixtures | PASS |
| GLES 3.0 fixture suite and live-model restore | PASS |
| Basic profile fixture suite and explicit skin/skin+morph rejection | PASS on host; not Vita hardware |
| Seven desktop/GLES full-frame comparisons | Byte-identical, maximum channel delta 0 |
| Four shared unlit desktop/basic full-frame comparisons | Byte-identical |
| ASan + UBSan model suite | PASS; leak detection disabled for the graphics-driver run |
| Reduced-work desktop HDR/IBL load, draw, state restoration and live-context rebuild | PASS; 16-pixel targets, eight-sample smoke configuration |
| Shared CMake shader target and exact embedded bytes | PASS |
| VitaGL declaration compatibility | PASS, host syntax only with opaque SDK type stubs |
| Existing Vita build-orchestration tests | PASS, 17 tests |
| Android build-orchestration tests, including --model3d implies GLES | PASS, 27 tests |
| Existing desktop/GLES/Vita GL-error regression suite | PASS |
| GLES model objects audited for legacy alpha-test/attribute-stack/EXT FBO imports | None found |
| CPU image/asset object audited for GL imports | None found |
| Native Windows 32/64 rebuild/retest of these changes | OPEN |
| Native Linux ARM64 build and GPU run | OPEN |
| Real NDK build/APK and Android GPU/lifecycle run | OPEN |
| Real VitaSDK build/VPK and Vita shader/GPU/lifecycle run | OPEN |

The pre-existing pngio.c longjmp-clobber warning appears in focused test builds.
The final recompilation of the changed model objects produced no compiler warnings. Model fixture
results do not establish end-to-end game performance, leak freedom of the
entire engine, or equivalence for the user's unprovided stage assets.
