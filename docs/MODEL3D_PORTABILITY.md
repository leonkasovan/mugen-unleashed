# MUGEN_MODEL3D portability research and implementation plan

Research date: 2026-11-12. Source baseline: `sdl2` at
`f97752f327ca61486ca389f47ceaa5d1d78f5a8a`.

Current implementation progress: [MODEL3D_IMPLEMENTATION.md](MODEL3D_IMPLEMENTATION.md).
The source-state observations below describe the research baseline.

Status: research and proposed work; no mobile/Vita backend has been enabled by
this document. Source references and line numbers below describe that baseline.

## Decision

Proceed in stages. Linux desktop OpenGL already has model support. Port the
renderer to GLES 3.0 once for Android and Linux GLES, then implement and qualify
a smaller VitaGL profile. Reuse the existing glTF loader, animation semantics
and stage/background integration. Do not just remove the Makefile guard or
define MUGEN_MODEL3D in the two platform CMake files.

This feature draws glTF stages and motif/storyboard backgrounds. It does not
convert SFF characters into 3D fighters, replace the fighting system, or make
System/OpenGLScreen render 3D. SDL2 handles platform services; it does not
translate desktop GLSL or implement these model shaders.

| Target | Current state | Recommended next step | Main risk |
|---|---|---|---|
| Linux x86_64, desktop GL | MUGEN_MODEL3D defaults ON | Qualify representative assets and preserve as reference | Current probe covers a simple triangle, not full PBR/shadow/IBL parity |
| Linux ARM64, desktop GL override | Same implementation is selectable when dependencies/context support desktop GL | Build/run on an actual desktop-GL-capable ARM device | ARM64 does not imply a usable desktop GL driver |
| Linux ARM64, GLES 3.0 default | Models OFF; explicitly enabling them is rejected | Shared GLES model backend | Desktop API/dialect and resource lifecycle |
| Linux x86_64, GLES override | Models OFF | Host qualification target for the same GLES backend | Host Mesa success is not handheld GPU proof |
| Android armeabi-v7a/arm64-v8a | Models OFF; System default, GLES opt-in | Enable through qualified GLES path | Device limits, context loss, memory, packaged assets |
| PS Vita, native SDL2 + VitaGL | Models OFF; existing 2D GL path retained | Dedicated constrained shader profile over VitaGL | Uniform/varying budgets, compiler behavior, memory and frame time |

Keep root GNU Make for Windows/Linux and platform CMake for Android/Vita.
Do not restore root CMake/MSVC support or any SDL 1.2 compatibility layer.

## What is already reusable

`src/model.c` parses glTF/GLB with vendored cgltf 1.14, decodes textures through
libpng/stb_image, evaluates transforms/animations/skins/morphs, and submits GPU
work. `src/stage3d.c` implements stage placement and BGCtrl3D;
`src/bg3d.c` implements motif/storyboard scene backgrounds. Preserve their
animation timing, layer ordering, camera, PalFX and transparency semantics.
Original engine structs retain their ABI; these additions use separate state.

Root Makefile lines 636-648 select the real model implementation on desktop GL
and reject GLES+MODEL3D. Android's
`port/android/app/src/main/jni/src/CMakeLists.txt:60` exposes MUGEN_GLES, and
`port/android/build.sh --gles` wires it through Gradle. It already includes the
GLES draw/renderer files, despite stale comments describing only a probe.
Vita uses `port/psvita/CMakeLists.txt`, the VitaSDK or optional vendored VitaGL,
and the native display adapter. Neither platform CMake currently supplies the
model vendor objects, generated model shader headers or feature definition.
`src/model_stub.c:4` supplies the disabled APIs and reports model requests.

Existing `[Video] EnableModelShadow` is read at `src/config.c:417`.
It controls shadows, not feature availability or shader compilation. Model
requests come from stage/motif/storyboard definitions; MUGEN_MODEL3D is a build
switch, not an existing mugen.cfg key.

## Concrete porting blockers and solutions

### 1. Desktop shader dialect and API assumptions

Evidence: `src/shaders/model.vert:1` uses GLSL 1.20;
`src/model.c:2140` selects fragment GLSL 1.20/1.30. Draw/shadow/environment
paths use glPushAttrib/glPopAttrib, GL_ALPHA_TEST, GL_TEXTURE_1D and EXT FBO
entry points (`model.c:2798`, `3128`, `3341`). GLES has no desktop attribute
stack or those legacy enables. `Model_GL3` at line 51 parses a desktop version
prefix, so an `OpenGL ES 3.x` string fails its test.

Solution: separate renderer capabilities from desktop version parsing. Use
GLES 3.0 core FBO operations and GLSL ES `#version 300 es`, with precision
qualifiers, explicit fragment outputs, in/out interfaces and texture calls.
Retain desktop shaders as the reference. Add Vita-compatible GLSL variants
following the existing translator conventions rather than assuming a desktop
version directive works. In particular, model.frag's `mul` needs the same
`uPalMul` alias already used by Vita sprite shaders. Query each linked
attribute/uniform; Vita locations are opaque handles, not arithmetic indices.
Do not change the active GL loader: mugen.h must remain first and each target
must use exactly one of glad desktop, glad ES, or vitaGL.h.

### 2. Shader resource budgets need real variants

Evidence: `model.vert:13` declares 192 vec4 joint rows for 64 bones. Four main
mat4 uniforms and four light matrices add 32 more vec4s before scalar/control
uniforms. Its varyings contain 34 scalar components when all are active.
`model.frag:26-46` and `model.c:74-155` assign five material samplers, eight
shadow samplers and three environment samplers: units 0-15 with full IBL.

The committed VitaGL source (`source/get_info.c:675-704`, `shared.h:32-39`)
reports 128 vertex uniform vectors, 16 fragment uniform vectors, eight varying
vectors, 16 attributes and 16 fragment texture units. Thus an assertion that
Vita has only eight texture units would be wrong for this source. However,
the full model shader exceeds its reported vertex budget even before matrices;
34 varying components also exceed eight vec4s. Its fragment uniforms need a
separate budget check. These advertised constants do not prove actual shader
compiler limits or the limits of a different prebuilt SDK library. Log the
linked VitaGL revision/configuration and compile/link representative shaders.

GLES 3.0 guarantees at least 256 vertex uniform vectors, 224 fragment uniform
vectors, 16 attributes and 16 fragment texture units [S1]. This makes the
existing 64-bone approach plausible there, not automatically qualified.

Solution: compile variants with genuinely omitted code/declarations for
skinning, shadows, IBL, material maps and light counts. Runtime booleans alone
do not remove active resources. Start Vita with unlit textured rigid meshes,
no shadow varyings and no environment samplers. A candidate 32-bone variant
uses 96 joint rows; only select it if the remaining uniforms and actual link
fit. Remap joints per primitive, partition meshes, or implement a measured CPU
skinning fallback. Never silently truncate a skin. Existing `Model_LoadSkin`
at line 1557 merely warns for more than 64 joints; validate both palette size
and vertex joint indices during this work. Cache programs by variant and
context generation; initialize all active sampler units without type aliasing.

### 3. Shared GL state must survive mixed 2D and 3D draws

Evidence: `Model_Draw` at line 3341 modifies program, buffers, vertex arrays,
texture units, blend/depth/culling and scissor state, then resets some to zero.
Desktop push/pop does not restore all of these states; blindly removing it
would also lose the states it currently protects. GLES sprite submission has
its own buffers/program state. Existing g_glCurrentProgram and
g_glBoundFramebuffer are software caches, not independent truth.

Solution: introduce a small explicit model-pass state contract shared with the
sprite backend. Flush queued sprites before entering; restore or re-establish
every touched state afterward, updating caches consistently. Include VAO/VBO/
EBO ownership, enabled and constant attributes, framebuffer, viewport/scissor,
front face, depth test/function/write mask, color mask, blend factors/equations,
program, active texture, touched bindings and pixel-store state. All error exits
must honor the contract. Use an owned VAO where supported. Preserve the depth
buffer/layer clearing behavior and framebuffer Y flip; prove it with sprites
both before and after the model. Do not add another swap/present.

### 4. Live models cannot currently recover after context loss

Evidence: `Model_Upload` at line 1578 frees vertex/index CPU buffers after
upload. Textures are also represented by context-owned names. The teardown at
`model.c:3408` releases shared programs/shadow caches; it does not rebuild live
model geometry/material resources. `gles_render.c:631` and its lost/restore
hooks manage sprite resources, with no model lifecycle integration.

Solution: split CPU asset state and GPU residency. Give the renderer a context
generation and models a residency generation. Retain bounded reload sources
or decoded data sufficient to rebuild buffers, textures and environment data;
preserve current animation/controller state rather than reloading game state.
Distinguish orderly live-context destruction from loss: invalidate dead names
without deleting them against a replacement context. Restore lazily or in a
bounded queue, then resume drawing. Route stage/motif/storyboard models through
one registry with explicit ownership. Test failures during partial restore.
Android documents that context loss deletes associated GL resources [S3]; use
the project's SDL/EGL lifecycle, not a new GLSurfaceView integration.

### 5. Shadow/HDR paths require optional capabilities and memory limits

Evidence: `model.c:2646` creates float shadow targets, and line 3034 uses
RGBA32F cube maps. Environment generation uses a 1024-square RGBA32F LUT and
hundreds/thousands of samples per filter pixel. GL version/float texture upload
support does not by itself establish renderability or filtering. GLES float
color targets need the applicable format/extension guarantees and an actual
complete FBO; EXT_color_buffer_float documents these additional formats [S4].

Solution: keep shadows and runtime HDR preprocessing out of Vita v1. Use a
small optional shadow tier on GLES only after format, depth and FBO probes.
Treat float filtering separately from color renderability; no desktop
GL_LUMINANCE32F fallback on GLES. Add configurable caps, not fixed 1024 maps
for every supported device. A single 1024-square R32F cube already needs
24 MiB for color faces alone; the RGBA32F LUT alone needs 16 MiB, excluding
depth, temporaries, alignment and driver overhead. Shadow textures are lazy,
so do not describe the theoretical maximum as startup allocation.
Prefer optional offline-generated environment maps for handhelds; that needs
a documented format/loader and matching orientation/roughness/BRDF convention.
Do not claim existing code already consumes prefiltered assets.

### 6. 32-bit ingestion and allocation safety

Evidence: model.h stores counts/offsets in int/unsigned int; model.c casts
cgltf counts, doubles capacities and calculates byte offsets, including
`index_count * 4` at line 1599. Skin allocation at line 1562 and DDS/HDR sizes
also need checked arithmetic. Pointer width does not change GPU index width:
current draws use GL_UNSIGNED_INT at lines 2464/2764.

Solution: validate cgltf data and all buffer/accessor ranges before conversion;
perform checked size_t multiplication/addition and range-check every narrowing
to int, GLuint-sized identifiers, GLsizei and GLsizeiptr. Bound decoded image
sizes, joints, nodes, hierarchy depth, morphs, animation keys and total CPU/GPU
asset bytes. Reject cycles/invalid indices and unsupported material extensions
with useful diagnostics. Do not cast host pointers to int or serialize structs
containing pointers. Test ARMv7/Vita 32-bit and ARM64/x64, including allocation
failure and truncated/oversized assets.

For Vita, optionally remap/chunk meshes to uint16 indices. Preserve large-mesh
geometry and choose the draw type from stored index format. The current
renderer needs 32-bit-index testing before changing vitaGL flags: upstream
explicitly documents potential glitches with INDICES_SPEEDHACK [S5]. Do not
turn off correctness checks or enable unsafe hacks to get a passing demo.

### 7. Build and runtime capability contracts

Add one reusable shader-header generation step to root Make and both platform
CMake builds, compiling existing cgltf/stb sources statically. Keep disabled
stubs and enabled implementations mutually exclusive. Include backend/profile/
shader options in configuration stamps or CMake dependencies. Android packages
must stage any non-embedded model resources and record them in asset manifests.
Vita must package the required shader variants through its existing mechanism.
No new shader compiler dependency is introduced beyond the existing VitaGL path.

Android currently advertises ES 2.0 while its optional engine GL backend needs
ES 3.0 (`AndroidManifest.xml:19`). Keep the broader installation/fallback policy
only if runtime ES3 probing and System fallback remain supported; an exclusively
ES3 APK must declare ES3. Android API level alone does not establish GPU ES3
support [S2]. Do not raise to ES3.2 unnecessarily.

Use MUGEN_MODEL3D for compiled availability, with a runtime capability record
such as base/skinning/light/shadow/IBL support and actual resource limits.
Any additional profile/config names are proposed, not implemented settings.
Keep `RenderMode = OpenGL` as the model rendering route. System and
OpenGLScreen must report unsupported model content; switching to System cannot
make a model-only stage playable. Offer an authored 2D fallback if available,
otherwise reject the stage clearly. Do not silently discard requested features.

## Proposed implementation sequence and acceptance gates

| Step | Files/work | Required evidence before advancing |
|---|---|---|
| A: reference fixtures | tools/test_model.*, new licensed/synthetic multi-feature assets | Desktop GL2.1 and GL3+ image/log baselines; ordinary 2D stage unchanged; unsupported assets fail explicitly |
| B: separate CPU/GPU state | model.c/h; proposed model_asset.c, model_backend.h, model_gl.c; preserve Stage3D/Bg3D APIs | Desktop parity, no added original-struct layout changes, checked allocation/error cleanup, fresh context and live-model restore tests |
| C: GLES base | proposed model_gles.c; GLSL ES shaders; gles_render.c lifecycle; Make/Android CMake | Linux GLES host triangle + textures/alpha + sprite/model/sprite sequence; no desktop GL imports; same fixtures on ARM64 Linux and Android |
| D: GLES semantics | skins, morphs, lights/PBR, outlines, texture transforms, BGCtrl3D | Reference animation states, image comparisons with justified GPU tolerances, uniform/sampler limits, Android suspend/rotate/resume and failed restore |
| E: Vita base | proposed model_vita.c and Vita shader variants; platform CMake and diagnostics | Real Vita textured mesh in a stage, menu/background integration, 2D overlays and return-to-menu; logged shader interfaces, GPU errors, memory, presentation |
| F: Vita optional skin/light | bounded variants, palette remapping or measured fallback | Actual compiler/link success, moving skinned mesh, no discarded joints; representative performance and no memory growth over repeated stage changes |
| G: expensive optional effects | GLES shadow/IBL tiers first; Vita only if separately justified | Per-format FBO probes, bounded peak memory, image correctness and measured timing; unsupported effects remain declared limitations |
| H: opt-in release | docs, defaults, build matrix | ABI/build checks plus hardware qualification; only then relax the current guards for supported backend/profile combinations |

A full rendering abstraction for every engine API is unnecessary. A small model
backend boundary should cover capabilities, program variants, upload/release,
context invalidation/restoration, begin/end model pass and draw submission.
Share asset evaluation/math and as much GL-family submission as practical;
keep dialects/capability-specific operations explicit. Avoid three copies of
the parser/animation system and a single lowest-quality shader on all targets.

## Test and performance matrix

Run native desktop GL x64, Linux GLES x64, Linux ARM64 GLES, Android ARMv7 and
ARM64, and Vita ARM32. Keep Windows 32/64 desktop model builds as regression
checks. Build-only success is separate from device render qualification.

Fixtures must cover glTF and GLB, embedded/external buffers and PNG/JPEG/DDS,
multiple scenes/layers, rigid and matrix nodes, animation interpolation,
mirroring/nonuniform scale, OPAQUE/MASK/BLEND, double-sided surfaces, PalFX,
texture transforms, joints at/over the palette limit, morph+skin combination,
all supported primitive/index types, outline/light variants, and unsupported
extensions. Check sprites/characters before and after models, stage unload,
storyboard teardown, hot reload/mode changes, allocation failure and repeated
context loss. FBO orientation, culling and alpha errors require pixel checks;
matching logs alone do not establish correctness.

Record shader compile/link logs, actual GL/VitaGL identity, source commit and
ABI. Missing device/context/toolchain is OPEN/SKIP, never PASS. Use structural
CPU-data comparisons across ABIs and tolerant reference image comparisons
across GPUs; do not promise bit-identical shader output.

Measure a fixed scenario with identical assets/resolution: no model, rigid
model, skin/morph, lights, then optional effects. Record model CPU update,
upload and draw submission time separately from frame/present waiting; collect
GPU timing only where reliable. Track real drawn frames, skipped frames,
p50/p95 frame time, cold/warm load time and peak/free memory. Reuse morph
scratch storage and avoid redundant skin/world-transform evaluation only after
profiling (current morph upload allocates a temporary at model.c:2322).
No automatic resolution/config changes and no promised 60 FPS from a triangle.

## Evidence and honest limits of this research

This session inspected the source baseline and the committed/pinned VitaGL
source, and checked the primary specifications below. It did not execute a
model on Vita or Android, compile an APK/VPK, or measure model performance.

Attempted `SDL_VIDEODRIVER=offscreen sh tools/test_model.sh` in this session:
blocked before compilation because pkg-config cannot find SDL2/libpng development
packages in the current environment. It is not a model test pass or a renderer
failure. Earlier desktop triangle results are recorded in
`docs/UPSTREAM_MERGE.md:100-125`; those are historical evidence, not reruns here.
The existing test creates a fresh context and reloads a model; that is different
from restoring a still-live animated model after unplanned context loss.

Documentation cleanup belongs in implementation: Android CMake still describes
the opt-in renderer as probe-only, UPSTREAM_MERGE mentions the now-retired root
MSVC CMake path, and BUILD-LINUX's package example still lists SDL1 packages.
Use current build rules, not those stale descriptions, as source of truth.

## Primary external sources

- [S1: Khronos OpenGL ES 3.0.6 specification](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf), resource limits tables 6.31-6.33 and shader/FBO sections.
- [S2: Android OpenGL ES support and runtime version checking](https://developer.android.com/develop/ui/views/graphics/opengl/about-opengl).
- [S3: Android EGL context loss and resource recreation](https://developer.android.com/reference/android/opengl/GLSurfaceView.Renderer#onSurfaceCreated(javax.microedition.khronos.opengles.GL10,%20javax.microedition.khronos.egl.EGLConfig)). This describes resource semantics; this engine continues using SDL/EGL.
- [S4: Khronos EXT_color_buffer_float](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_color_buffer_float.txt).
- [S5: VitaGL build/compatibility options](https://github.com/Rinnegatamante/vitaGL/blob/master/README.md). Current upstream guidance; not proof the packaged VitaSDK library has those exact options.
- [Pinned VitaGL source](https://github.com/Rinnegatamante/vitaGL/tree/9bf3e3f4e55efd1f4a191b179144f7f9a98cd925), matched by tools/psvita/fetch_vitagl.sh; inspect get_info.c/shared.h/custom_shaders.c locally and qualify the actually linked library.
