# Android SDL 1.2 semantics to SDL2: migration and qualification plan

Date: 2026-11-02 (Asia/Jakarta).
Native-source baseline: `d14b4e3b05bad838a1a44c3691b48edf35ab69f6`, branch `sdl2`.
Related documents: [README.md](README.md), [REVIEW.md](REVIEW.md),
[desktop migration record](../../SDL2_PLAN.md).

## 1. Starting point and definition of completion

**Android already links SDL2.** Its native CMake imports `libSDL2.so`,
`libSDL2_mixer.so` and `libSDL2_ttf.so`. A force-included local adapter emulates
SDL1 APIs for the reconstructed engine. This plan finishes the migration of
those semantics and consolidates validated portable code; it does not pretend
there is a currently supported Android SDL1 build.

The current change adds toolchain download/check orchestration and its host
tests. It does not implement the native fixes below. A host fixture APK is not
an Android build, and a buildable APK is not a qualified port.

Completion means:

- No SDL1 header/library ABI enters the Android executable or packaged native
  dependency closure.
- Legacy engine keys, button codes, surface semantics, timing and observable
  behavior are preserved through an explicit, tested SDL2 boundary.
- The System renderer presents complete frames at the configured game size,
  with independent output dimensions and exactly the intended presentation rate.
- Input, text, fonts, audio, streams, data extraction, lifecycle and cleanup
  meet their named acceptance gates on actual Android devices.
- Packaging, signing identity and dependency/toolchain provenance can be
  reproduced from a manifest, with limitations stated explicitly.
- A compatibility wrapper may retain SDL1-era function names where preserving
  reconstruction fidelity requires them. Correct semantics and ownership,
  not renaming every call site, define the migration.

### 1.1 Build ownership and constraints

| Entry point | Role | Migration rule |
|---|---|---|
| Root `Makefile` | Existing Windows/Linux builds | No Android or Vita target/toolchain logic added |
| Root `CMakeLists.txt` | Existing desktop/MSVC configuration | Preserve behavior; run applicable regressions after shared-source changes |
| `port/android/build.sh` | Host preflight/bootstrap, staging and Gradle invocation | Android host workflow lives here and in scoped helpers |
| `port/android/app/build.gradle` | Android packaging, ABI and SDK/NDK/CMake selection | One explicit set of compatible pins |
| `port/android/app/src/main/jni/src/CMakeLists.txt` | Native engine shared library | Explicit SDL2 dependencies, sources and platform definitions |
| `port/psvita/*` | Independent Vita CMake port | Not the Android build path; do not inherit its device policy |

Preserve reconstructed structs/address comments and floating-point expression
order. Do not add window/renderer/controller fields to original engine structs
at assumed SDL1 offsets. New backend metadata belongs to the platform layer.

**Do not change game resolution to make the port appear faster.** Preserve
`GameWidth/GameHeight`, including a selected 640x480 configuration. Physical
display size, logical framebuffer size, orientation and letterbox viewport are
different values. The Android build/bootstrap does not authorize editing the
pristine reference installation or silently rewriting user configuration.

## 2. Verified source inventory and blockers

| Area | Current behavior | Required outcome |
|---|---|---|
| SDL headers/ABI | SDL2 imported, local SDL1 aliases force-included | One matching SDL2 ABI with no conflicting adapters |
| Macro selection | `ANDROID_PORT` + `MUGEN_VITA_GL`; only `MUGEN_SDL2_API` inferred | Separate Android identity, SDL major and renderer capability |
| Keys | Normal key translation/text handler require `MUGEN_SDL2`, absent here | Common forward/inverse legacy-key translation |
| Controller policy | Vita bridge and fixed-topology config overrides can compile on Android | Android device/slot/topology mapping, persistent remaps |
| Software output | Android SDL texture path exists | Correct geometry, allocation, pitch and frame policy |
| Dimensions | Shared Vita guard requests 960x544 on Android | No Vita panel dimensions in Android output policy |
| Mode selection | GL source excluded but cfg can select GL stubs | Explicit supported-mode enforcement before function-table selection |
| Texture resizing | Format checked, size change not covered | Recreate on format or dimensions with safe rollback |
| Present batching | Each public request can re-arm and immediately present | Separate dirty marking from one explicit frame-end flush |
| Scaling | Android local truncation/blit wrapper | Proven SDL_gfx-compatible sampling/state preservation |
| Audio | SDL2 mixer linked; its codec profile and engine BGM routing disagree | Named, tested engine routes rather than codec assumptions |
| RWops | Shared SDL2-signature code exists | Correct size/offset behavior and decoder-specific lifetime tests |
| Assets | Final files written directly and existing files skipped | Atomic, verified extraction/update reconciliation |
| Shutdown | Android objects destroyed after `System_Deinit -> SDL_Quit` | One valid destruction order before subsystem teardown |
| Touch/UI | No on-screen control implementation | Explicit optional feature gate, never implied by SDL2 linkage |

The findings are described in REVIEW.md as A01–A09. The existing documentation's
claim that all statically detectable defects were fixed is not an acceptance
baseline. Preserve historical observations as history, not current proof.

### 2.1 File-level inventory

| Existing file | Responsibility to audit |
|---|---|
| `android_main.h` | SDL1 aliases/constants, GL no-ops, SDL key types and forced include order |
| `android_video.c` | Initialization, window/renderer/texture/surface ownership, logical size, present and quit |
| `android_main.c` | SDL entry, writable root, extraction/version stamp, argv and calls into the engine |
| `android_gfx.c` | Scaling dimensions/sampling, palette/key/alpha state and ownership |
| `android_glstub.c` | Which symbols are intentionally unavailable and whether calls fail visibly |
| `src/mugen.h` | SDL-major conditions versus Android lifecycle/GL-loader conditions |
| `src/events.c` / `keyboard.c` | Key conversion, snapshots, text events, synthetic input and focus state |
| `src/pads.c` / `config.c` | Instance/slot mapping, button code domain, controller settings and remap precedence |
| `src/video.c` / `gfx.c` / `frame.c` | Render policy, target sizes, submission and skip-draw behavior |
| `src/main.c` / `system.c` / `error.c` | Platform init, normal/error cleanup order and diagnostic paths |
| `src/font.c` / `stream.c` / `bgmplugins.c` | SDL2 fonts/RWops/music signatures and lifetime contracts |
| Gradle/manifest/SDL Java files | Library load order, matching Java/native SDL release, ABI, package and lifecycle |

## 3. Architecture decisions

### 3.1 Share SDL2 semantics, retain Android ownership

Extract reusable key and surface/scaling helpers from the desktop SDL2 bridge
into small modules such as `src/sdl2keys.c/.h` and
`src/sdl2surface.c/.h` (proposed names). Compile these for SDL2 consumers
without also pulling in a desktop window/GL implementation.

Keep Android entry/activity, writable-storage selection and mobile lifecycle
in `port/android`. Retain a thin Android video-policy adapter. Decide whether
to share more presentation infrastructure only after its policy parameters
are explicit and both platforms' tests pass.

Do not simply add `-DMUGEN_SDL2` to the current Android CMake: this can enable
`src/sdl2compat.c`'s independent window/present implementation and duplicate
macros/symbols alongside `android_main.h`. Extract or split the modules first.

### 3.2 Make platform and renderer guards truthful

- `ANDROID_PORT` identifies Android entry/lifecycle/storage behavior.
- `MUGEN_SDL2_API` identifies SDL2 API/ABI semantics.
- A dedicated software-only capability condition selects unavailable desktop
  GL declarations/entry points, rather than pretending Android is a Vita.
- A future GLES renderer needs its own capability/profile selection and tests.
  Neither `MUGEN_VITA_GL` nor SDL2's internal renderer backend implies it exists.

Audit every affected guard across sources and headers before removing the
Android definition of `MUGEN_VITA_GL`. That definition currently also changes
symbol declarations and GL capability aliases; deleting only the CMake flag
can leave unrelated compile/link failures. Preserve Windows/Linux/Vita branches.

### 3.3 One presentation and lifecycle owner

The selected SDL2 video adapter owns window, renderer, texture and output
surface; the engine borrows the documented drawing target. SDLActivity/SDL2
own the Android surface/JNI lifecycle. Avoid a second native EGL/window owner.

Destroy owned resources while their SDL subsystem is alive, reset pointers and
initialization state, and call final SDL shutdown once. Handle partial init,
normal exit, error exit, background/foreground and recreation using the same
ownership contract. Do not rely on process death to mask double destruction.

### 3.4 Keep initial renderer scope explicit

The required migration target is System software rasterization with SDL2
presentation. Before selecting renderer function tables, unsupported
`OpenGL/OpenGLScreen` must either fail clearly or fall back explicitly with
requested/actual mode diagnostics. No silent GL stub rendering.

A real GPU sprite renderer for Android is a separate extension (section 8).
The VitaGL implementation planned for Vita is not an Android library/back end.

## 4. Milestones and acceptance gates

All native milestones below are pending. B0's host bootstrap implementation
is present in this change; its real-distribution/native-build gate is pending.

### B0 — Reproducible host setup and baseline evidence

Already implemented in this change:

- `build.sh --setup / --download-tools / --check-tools / --offline`.
- Repository-local default SDK/JDK/caches, HTTPS vendor downloads, archive
  checksums and staged extraction.
- JDK 17 validation, exact SDK/NDK/CMake pins, an explicit external NDK property
  consumed by Gradle and matching companion-builder prefixes.
- Correct requested APK variant selection and fresh asset staging.
- Offline fixture tests for these contracts; see tests/test_build.py.

Remaining qualification:

- [ ] Fetch real Google/Adoptium metadata and archives on supported hosts;
  verify expected command-line-tool layout and SDK manager execution.
- [ ] Complete actual SDK license/install flow without automatic acceptance.
- [ ] Confirm Gradle 8.1.1 / AGP 8.1.1 / JDK 17, SDK 34, build-tools 33.0.1,
  NDK 27.1.12297006 and CMake 3.22.1 selections in real build output.
- [ ] Validate native Windows/MSYS path conversion, macOS execution/Rosetta
  requirements and Linux host compatibility.
- [ ] Record exact JDK patch/archive hashes, source commits and package revisions.
  “JDK 17” and a cached latest-metadata response alone are not a full lockfile.
- [ ] Harden SDL2/companion source archive provenance and separate ABI/API build
  directories. The existing companion downloader is not covered by the bootstrap's
  verified archive path.
- [ ] Capture an actual baseline APK, unstripped native symbols, packed cfg,
  asset/library hashes, device model/API/page size and SDL renderer information.
- [ ] Inspect actual `DT_NEEDED` closure, SONAMEs, architecture and minimum API
  of every packaged library. Include any required FreeType/C++/other shared
  dependencies; do not assume four named .so files establish closure.
- [ ] Run same-binary control scenarios to characterize timing/RNG variation
  before interpreting cross-build log differences.

Gate: a fresh documented host can produce a real APK without another private
repository; pins and dependency closure are known; artifacts can be reproduced
from the recorded inputs. No device behavior is inferred from this gate.

### B1 — Isolate target policy and reject unavailable renderers

Primary files: native CMake, `android_main.h`, `mugen.h` and the guarded engine
modules listed in the inventory.

- [ ] Replace Android's reuse of Vita identity with explicit capability/platform
  conditions. Keep common software declarations where needed.
- [ ] Remove Vita stdout/stderr paths and logs, Vita controller overrides, bridge
  callbacks and fixed 960x544 surface policy from Android.
- [ ] Preserve Android lifecycle/error reporting and the shared main helpers.
- [ ] Establish actual mode validation before `Gfx_SelectRenderer`. Emit requested
  and actual renderer, fallback reason, logical size and output size.
- [ ] Keep game configuration unchanged outside an explicit, generated platform
  override. Never edit reference `install/` files in place.
- [ ] Ensure stubbed GL functions cannot be reached by a successfully accepted
  unsupported cfg value. Add diagnostic failures for accidental entry.
- [ ] Verify compile definitions/symbol maps for Android, desktop and Vita;
  do not claim isolation from the presence of a single gfx guard.

Gate: an Android preprocessed/source audit contains no Vita panel/control/path
policy, and all three render-mode cfg values have deterministic, documented
outcomes before drawing begins.

### B2 — Unify keys, text and controller input

Primary files: proposed common SDL2 key helper, `events.c`, `keyboard.c`,
`pads.c`, `config.c`, built-in input tables and the Android adapter.

#### Keys and text

- [ ] Extract/reuse `Sdl2_EngineKey` and its exact inverse for every SDL2
  consumer without enabling a second video implementation.
- [ ] Translate incoming SDL2 keycodes before any legacy array or queue access.
  Reject unmapped/out-of-range values. Preserve the engine's release-bit scheme.
- [ ] Fix focus snapshots by mapping engine key -> SDL2 key -> SDL2 scancode.
  Check both the source keyboard-state length and destination bounds.
- [ ] Normalize built-in menu/input tables as needed: constants compiled from
  SDL2 headers and legacy integers saved in cfg must meet in the same engine
  domain. Do not blindly convert an already-normalized saved binding twice.
- [ ] Apply the same policy to synthetic/touch/controller-to-key events,
  debug hotkeys, pause, Ctrl/Shift/Alt, function keys, arrows and keypad keys.
- [ ] Handle `SDL_TEXTINPUT` separately from physical key press/release.
  Process all valid UTF-8 codepoints, define unsupported-codepoint handling,
  and avoid delivering printable input twice to the console.
- [ ] Define key repeat behavior for editing separately from held gameplay
  state. Do not claim that removing `SDL_EnableKeyRepeat` proves equivalence.
- [ ] Clear/reconcile pressed states on focus/background/controller removal
  and ensure releases cannot be lost across a paused frame.

Required key fixtures include ASCII A, Up (SDL2 `0x40000052` -> legacy 273),
Pause, F1/F2, left/right Ctrl, keypad keys, unknown keys, malformed text and
multi-codepoint text events. Verify press, release, snapshot and saved-binding
lookup all agree, including actual menu navigation.

#### Controllers

- [ ] Retain and test the existing SDL2 instance-ID-to-slot handling.
  Enumeration index is not an SDL_JoystickID.
- [ ] Initially preserve the engine's raw joystick API with runtime axes/hats/
  buttons, rather than silently switching persisted bindings to a different
  GameController numbering scheme.
- [ ] Derive engine button codes from that device topology and slot convention:
  `2*axes + 4*hats + 1 + raw_button` is the current slot-relative button rule.
  Never put a raw zero into a map where zero means unassigned.
- [ ] Remove the unconditional Vita-specific P1/P2 defaults. Initialize sensible
  Android defaults only when settings are missing/invalid; keep explicit remaps.
- [ ] Verify keyboard and pad channels separately on each menu/fight screen.
  Confirm/cancel/Start must not simultaneously become attack and pause actions.
- [ ] Record GUID/topology/mapping version with persisted settings where
  necessary. Define the migration/reset policy for old incompatible cfg maps.
- [ ] Test two devices, instance IDs different from indexes, reconnect and
  removal while held. If reconnect requires restart, document and test that
  limit instead of claiming hot-plug support.

Gate: no native or synthetic SDL2 key enters a legacy array unconverted;
menu, fight and resync actions agree; controller remaps persist and no Vita
button/panel convention is active on Android.

### B3 — Correct System surfaces and presentation

Primary files: `android_video.c`, `src/video.c`, `gfx.c` and `frame.c`.

#### Geometry, allocation and ownership

- [ ] CPU framebuffer dimensions come from the configured game, not display
  resolution. Record window size, renderer output size and logical viewport
  independently, especially for fullscreen/high-DPI/landscape devices.
- [ ] Support RGB565 and the required 32-bit path with verified SDL masks and
  texture formats. Either reject 24bpp explicitly or perform a reusable,
  correct 24-to-32 conversion before upload. Do not mismatch byte widths.
- [ ] Keep display 8bpp rejection separate from support for indexed offscreen
  surfaces/fonts/palettes used by scaling and sprite code.
- [ ] Recreate streaming textures whenever width, height or format changes.
  Allocate new objects first; commit the new state only after success so OOM
  does not leave stale dimensions, invalid pointers or half-updated resources.
- [ ] Check video init, logical-size, texture-update and rendering return values.
  Log the failing stage and backend, not only “video mode failed”.
- [ ] Honor actual surface pitch. Upload completed frames opaquely; do not
  apply a second alpha blend to the already-composited framebuffer.
- [ ] Define the software surface's borrowed/owned lifetime. On mode changes,
  invalidate old references safely and avoid freeing it through both engine
  and adapter cleanup paths.
- [ ] Handle SDL renderer/device reset events by recreating texture state from
  retained CPU pixels. Reinitialize all cached fields during a valid restart.

#### Exactly one intended frame presentation

Choose an explicit deferred policy:

1. Drawing/SDL_UpdateRect/SDL_Flip requests mark the logical framebuffer dirty.
2. One identified frame-end boundary uploads and presents the final pixels.
3. Repeated requests within that frame do not upload again.
4. Skip-draw frames do not manufacture a new presentation.
5. Initialization, error display and any non-frame immediate paint use a
   separate, documented flush operation.

Do not use a dirty flag that each caller immediately re-arms and flushes.
The current source probe's three requests produce three uploads/presents.

- [ ] Instrument draw requests, dirty marks, uploads and actual presents.
- [ ] Assert one UpdateRect + one Flip + one frame-end request results in one
  intended present; two drawn frames give two; a skipped unchanged frame gives
  zero extra uploads.
- [ ] Test mutations that reintroduce immediate flushes or omit the frame-end
  flush. Tests must detect both duplication and frozen output.
- [ ] Verify present counts in initialization/menu/fight/error paths, not only
  a minimal SDL loop.
- [ ] Respect the engine's timing/skip behavior. Do not add arbitrary sleep or
  change logical game speed to conceal presentation cost.

#### Scaling to the device

Use the renderer for output scaling; keep nearest filtering where the current
pixel-art policy requires it and honor configured aspect/letterbox behavior.
Touch/mouse coordinates must be converted through the real viewport. Record
the actual SDL renderer/acceleration instead of assuming the requested
accelerated backend always succeeded.

Gate: complete frames at unchanged configured game dimensions, correct
same-format resize/format transitions and no invalid upload pitch; counted
presentation matches the frame contract on device.

### B4 — Qualify surface conversion, SDL_gfx and SDL_ttf

Primary files: `android_gfx.c`, proposed common surface/scaler module,
`font.c` and the SDL2 dependency integration.

#### Scaler selection

Preferred initial choice: extract/reuse the shared SDL2 scaler whose desktop
record includes 19 differential cases. Android's present adapter does not
need to own a separate approximation of the same function. The result must
be tested for the selected Android ABI/dependencies; desktop results do not
automatically establish ARM equivalence.

Alternative: use upstream SDL2_gfx's surface rotozoom implementation, with
a pinned source/build and the same tests. There is no need to adopt its
SDL_Renderer drawing primitives to replace this engine's rasterizer.
Do not link both an upstream and local `zoomSurface` symbol.

Required fixtures:

| Fixture family | Assertions |
|---|---|
| 8-bit indexed / partial palettes | Pixel indexes, palette size/colors/alpha, padded entries and ownership |
| Color-key cases | No key, zero key and nonzero key values, not only key presence |
| RGB565 / RGB555 / 32-bit | Actual channel-mask decoding and documented conversion semantics |
| Fractional ratios / odd sizes | Output dimensions and sampler coordinates, including 16.16 RGBA behavior |
| Padded pitches / tiny images | Every row and edge; no overread/write |
| Real semitransparency | Pixel alpha, alpha/color modulation and blend mode preserved |
| Error cases | NULL, unsupported scale/smoothing and allocation failure handled without leaks |

Use `SDL_GetRGBA` and queried state in comparisons, not hardcoded shifts or
constant alpha. A palette-presence/shape-only check is insufficient.
Require nonconstant expected images so an all-black scaler cannot pass.

#### TTF and surface API details

- [ ] Record SDL2_ttf, FreeType, optional HarfBuzz and font-file hashes.
- [ ] Verify open/close/init/quit order, metrics, ascent, baseline, multiline
  layout, UTF-8 and the sizes/styles used by the screen pack.
- [ ] Test both solid indexed and blended RGBA text before/after scaling and
  after conversion to engine surfaces.
- [ ] Preserve source alpha and color-key behavior without blending during
  a transformation that should copy pixels.
- [ ] Distinguish dependency-driven glyph raster differences from wrong metrics,
  opaque backgrounds, lost masks or placement errors.
- [ ] Keep font optimizations such as atlases outside the initial correctness
  change unless they solve a measured problem and carry independent tests.

Gate: pixel/state/metric tests pass their declared comparisons on ARM; no broad
exemption for a format or font family. A successfully linked font/scaler symbol
does not satisfy this gate.

### B5 — SDL2_mixer, RWops and actual BGM routing

Primary files: `sound.c`, `sdlmixercompat.c`, `bgmthread.c`, `bgmplugins.c`,
`stream.c` and the Android companion builder/config.

#### Device and callback contract

- [ ] Compile/link the SDL2 mixer headers and local `Mix_QuerySpecEx` adapter;
  never import Elecbyte's SDL1 mixer extension ABI.
- [ ] Record obtained frequency, sample format and channels, plus separately
  labeled requested chunk frames and measured callback bytes.
- [ ] Use `bytes = frames * channels * (SDL_AUDIO_BITSIZE(format)/8)`.
  1024 stereo S16 frames normally give 4096 callback bytes; this is not a 4x
  buffer defect.
- [ ] Validate every byte for complete/partial blocks, underrun silence,
  muted/paused state and guard regions beyond callback len. Use the correct
  silence pattern for supported unsigned and signed formats.
- [ ] Establish callback/main-thread ownership and synchronization. Stop or
  quiesce callbacks before releasing buffers/music/effects. Do not allocate,
  do file I/O or join a thread in a real-time callback.
- [ ] Verify real SND effects, simultaneous BGM, volume/pan, resampling,
  looping, stop/replace and background/resume audibly on Android.

#### RWops and music lifetime

- [ ] Exercise size/seek/read/write with SDL2's widths and engine bounds.
  Size queries must not change cursor position; test EOF/error/archive paths.
- [ ] Trace underlying Stream, RWops and Mix_Music ownership for success,
  failed loads, streaming, seeking, replacement and shutdown.
- [ ] Preserve `freesrc=0` only with a correct caller-owned lifetime. Do not
  close/free a RWops immediately merely because a historical tracker decoder
  used to read everything synchronously.
- [ ] Verify each selected decoder's actual lifetime behavior with real files;
  the quick-VS custom-hook test does not exercise every music decoder.

#### Choose a usable engine music route

The current mismatch is explicit: `Sdlmix_Open` accepts tracker/MIDI and
rejects WAV/OGG/MP3, while the Android companion profile disables tracker/MIDI.
The mpg123 subsystem expects a Windows-named external library not supplied
by this APK. Merely enabling a codec cannot fix the route.

Choose one reviewable first implementation:

1. **Recommended for common Android music assets:** add an Android native
   SDL2_mixer subsystem/adapter registered for the intended WAV/OGG/MP3 formats.
   Preserve plugin-host result/mode, loop points, pause/mute/volume, callbacks
   and lifetime. Verify ownership of the SDL2_mixer music channel versus the
   existing custom BGM hook; never assume both can drive it simultaneously.
2. Enable the existing tracker/MIDI route and its actual decoders/assets when
   compatibility with those formats is required. MIDI patch/config data must
   be packaged or reported unavailable.
3. If retaining native mpg123, supply a matching Android library and a scoped
   native binding/name map. Do not rename a missing DLL string and call it done.

Keep desktop plugin routing unchanged. Document supported formats by engine
route and tested decoder, and fail clearly for unavailable configured plugins.
Do not silently drop every BGM plugin just to make initialization look clean.

Gate: actual effects and each claimed music route play correctly and audibly,
with tested looping/seeking/teardown, independent from callback unit tests.

### B6 — Make data extraction and upgrades recoverable

Primary files: `android_main.c`, Gradle's asset-manifest task and a proposed
`port/android/tests` extraction harness.

- [ ] Resolve a writable app-owned root before opening output/log/temp files.
  Verify directory creation and storage availability; never assume
  `/data/local/tmp` is writable by the app.
- [ ] Use a validated manifest with normalized relative paths, expected size
  and content hash. Reject escaping/absolute paths and report invalid entries.
- [ ] Write each file to a temporary sibling, validate read/write/close and
  size/hash, then rename atomically to its final location.
- [ ] Remove or safely resume incomplete temporaries. Existing destination files
  must be distinguished from complete verified files; `stat` alone is not proof.
- [ ] Key extraction state to an actual manifest/content generation. Only write
  the completion record after every required entry is verified.
- [ ] Define user-edit reconciliation: preserve modified cfg/characters/assets,
  update unchanged packaged files, report conflicts and do not delete user
  content because an APK changed. Keep backup/recovery behavior reviewable.
- [ ] Handle no-assets builds with already-provisioned data explicitly. An empty
  manifest must not permanently suppress extraction of a later real payload.
- [ ] Fail startup clearly when required game data is missing rather than
  entering the engine with a misleading successful extraction stamp.
- [ ] Avoid an unqualified `tmpfile()` dependency; stream the manifest or use an
  explicit app-writable temporary path.
- [ ] Preserve the source reference tree and record packaged/extracted hashes.

Required interruption fixtures: stop before/after a partial write, before/after
rename, before/after completion record, full storage, missing asset, malformed
manifest, upgrade with changed packaged files and user-edited cfg. Repeated
launches must recover without silently validating a truncated file.

Gate: an interrupted first run and an APK payload update produce a consistent,
validated tree while retaining user modifications according to the documented
policy.

### B7 — Qualify Android lifecycle, errors and JNI/library integration

- [ ] Keep SDL2 Java/native versions matched and confirm packaged `SDL_main` /
  `Mugen_Main` symbols from the actual ELF. Java source alone does not prove
  the native library loads.
- [ ] Inspect transitive native libraries in the actual APK and confirm loader
  resolution on minimum/supported API levels. Avoid packaging host/other-ABI
  binaries under the arm64 directory.
- [ ] Check SDL initialization failures before creating window/texture state.
  Retain clear logcat diagnostics and a usable app-file log even before the
  engine log is available.
- [ ] Move Android video object destruction ahead of final `SDL_Quit` and reset
  all cached initialization, size/format, dirty and input state.
- [ ] Test partial init cleanup at each allocation stage and repeated helper
  init/quit. Do not assume the entire reconstructed engine is reentrant merely
  because one video helper can restart.
- [ ] Handle foreground/background, focus, surface/context loss, orientation
  and Activity/process recreation through SDL's Android ownership model.
  Do not add a competing EGL lifecycle or invoke desktop resize policy blindly.
- [ ] Release held keys/controllers/touches on suspend/removal. Respect configured
  audio background behavior and prevent a resume burst of stale queued input.
- [ ] Preserve valid engine state across a surface reset; rebuild only GPU
  presentation objects when appropriate, with CPU data still valid.
- [ ] Check JNI-supplied arguments, bounded file arguments, empty argv and errors.
  Document the current whitespace-only syntax or implement explicit quoting
  with tests rather than claiming shell semantics.
- [ ] Verify normal exit, Back navigation and error exit without double free,
  callbacks after free, stale window references or a second SDL shutdown owner.

Gate: real-device background/foreground, surface recreation and clean exit
work repeatedly, with no invalid native object lifetime or stuck input/audio.

### B8 — Measure, qualify devices and prepare release

- [ ] Capture logs, framebuffer screenshots, input traces, audio evidence and
  frame-time/resource measurements for the scenarios in section 6.
- [ ] Record CPU software draw, upload and present/wait time separately.
  Capture median/p95/p99 frame duration, tick/draw/present counters, texture
  upload bytes and memory across scene changes.
- [ ] Keep the same game resolution/assets/settings while comparing versions.
  Do not turn down resolution, skip extra frames or alter game speed to
  manufacture a performance result.
- [ ] Validate actual renderer backend/output viewport on at least the intended
  physical Android device class. Emulator success is separate evidence.
- [ ] Test minimum API compatibility, modern storage/lifecycle behavior, and
  4 KiB / 16 KiB page-size native-library packaging where that support is claimed.
  Inspect every native dependency, not just libmain.so. Toolchain/AGP upgrades
  needed for broader device support are separate, explicit changes.
- [ ] Run shared-source host SDL2 tests and the required MSVC/MinGW/Linux/Vita
  regressions when those files change; isolate any expected ARM numeric drift.
- [ ] Produce a manifest of toolchains, SDL/companion sources, Java integration,
  fonts/assets, signing-certificate fingerprint and final APK/native hashes.
  Do not record private keys/passwords.
- [ ] Rewrite README/REVIEW status only from the collected evidence. Retain
  unsupported formats, restart requirements and failed scenarios as open items.
- [ ] Keep a known APK, configuration/data backup and recorded dependency prefix
  for rollback; use compatible signing identity for install-over tests.

Gate: all required scenarios pass on supported devices, deviations are scoped
and documented, and the previous installation/data can be restored.

## 5. Optional feature work after the core migration

### 5.1 Touch controls and data import

Touch support is not currently implemented. A phone-only usability claim
requires its own completion gate; core migration can otherwise qualify only
the declared keyboard/gamepad input profile.

- Design a landscape overlay with d-pad, six attack buttons, Start and menu
  controls, independently configurable from game resolution.
- Route actions through the tested engine/SDL2 input boundary, with explicit
  per-screen confirm/cancel behavior rather than arbitrary keyboard constants.
- Support multitouch, held combinations, sliding between buttons and release
  on cancellation/focus loss. Do not synthesize presses without matching releases.
- Convert physical touch coordinates using the actual viewport/letterbox and
  density; the overlay must not change game logical dimensions.
- Define whether touches outside the game viewport are overlay controls or
  ignored, and prevent duplicate SDL touch-to-mouse/gamepad actions.
- Add app-owned/SAF import and export workflows for game data without assuming
  all file managers can edit `Android/data` or requesting broad storage access
  as a substitute for a tested import flow.
- Test import cancellation, insufficient space and modification-preserving
  updates using the same extraction/reconciliation contract.

### 5.2 Additional ABIs

The current Gradle/build script supports arm64-v8a only. Adding armeabi-v7a
or x86_64 requires matching engine/vendor builds, Java/native packaging,
minimum-API/page-size checks, structure/float behavior tests and per-ABI output
naming. Changing `ANDROID_ABI` alone is insufficient.

## 6. Verification matrix

| ID | Scenario | Required evidence |
|---|---|---|
| T01 | Missing/existing JDK and SDK | Correct selection, explicit download opt-in, checksum/failed-download behavior |
| T02 | Clean real build on supported hosts | Exact package/ABI selections, no private sibling-repo dependency |
| T03 | Wrong SDL1/host/other-ABI library | Explicit rejection or diagnosed link/closure failure |
| T04 | Activity/native library entry | Actual load and symbol resolution, usable early logs |
| T05 | System startup/menu/quick VS | Visible correct frames, requested/actual renderer and dimensions |
| T06 | Unsupported GL cfg | Clear failure/logged fallback before selecting a stub draw path |
| T07 | Key press/release/resync | Same legacy actions for ASCII/special/keypad/modifiers; bounds instrumentation |
| T08 | Gamepad topology/instance/remaps | Correct raw-to-slot-to-action mapping; persistent settings and removal releases |
| T09 | Same-format size change, format change, OOM | Correct texture size/pitch, safe rollback and no stale pointers |
| T10 | Repeated present requests and skip-draw | Intended number of uploads/presents, no duplicate or frozen frames |
| T11 | Display/orientation/aspect | Configured game coordinates unchanged; correct viewport and input conversion |
| T12 | Scaling and TTF | Exact named pixel/state/metric comparisons, including real alpha and nonzero key |
| T13 | Callback/RWops | Whole buffers/guard regions, actual callback length and streaming lifetime |
| T14 | SND effects plus supported BGM | Audible mixing, loops/seek/volume/mute/pause and clean replacement |
| T15 | First-run interruption and data update | Atomic recovery, correct generation and preserved user modifications |
| T16 | Background/resume and surface recreation | Valid render resources, no stuck input or stale audio |
| T17 | Partial init, normal exit and errors | Destruction before subsystem shutdown, no double owner/use-after-free |
| T18 | Sustained fixed scenario | Frame-time distribution and stable memory/resource counts |
| T19 | Touch/import profile if advertised | Multitouch/release/viewport and storage recovery tests |
| T20 | APK/signing/modern native compatibility | Correct certificate, dependency closure and claimed API/page-size support |

### 6.1 Comparisons and meaningful negative tests

There is no Android SDL1 executable to use as a fictitious reference.
Compare against the preserved Android baseline for existing platform behavior,
desktop SDL1/SDL2 for reconstructed contracts, and independent synthetic
expected images/inputs/audio buffers for correctness.

Normalized logs do not prove graphics, audio or input. Use identical assets,
game cfg and durations; run same-binary controls to separate RNG/timing effects.
Do not demand exact AI/attract log ordering for uncontrolled nondeterministic
runs or declare mismatches resolved simply by increasing a normalizer.

Tests must fail when:

- SDL2 Up is sent directly to a legacy array, inverse resync is wrong, or a raw
  controller index is stored as a derived engine button.
- Vita panel defaults/control callbacks are enabled in the Android profile.
- A resize retains the old texture, an upload ignores pitch, or the 24bpp
  adapter falsely claims a 32-bit pixel layout.
- Immediate present is reintroduced, the final flush is removed, or a skipped
  frame causes an unintended upload.
- Scaling loses alpha/key/palette state or returns a constant black image.
- Callback silence is shortened, writes exceed len, RWops size changes the
  cursor, or an active decoder's stream is closed early.
- Extraction stamps a partial file or an old generation suppresses new data.
- A renderer identity test accepts GL stubs or an ownership test destroys
  graphics objects after final SDL shutdown.

Preserve full expected pixels/buffers and resource invariants. Any accepted
floating-point/glyph difference must identify the fixture, location/channels,
dependency and reason; no whole-format exemption or arbitrary visual tolerance.

### 6.2 Evidence storage

Keep generated files inside the repository, for example:

```text
build/android-validation/<engine-sha>/<device-or-host>/
  manifest.json
  setup-and-build.log
  native-link-and-dependencies.txt
  packed-config-and-shader-hashes.txt
  logcat.txt
  mugen.log
  frames/
  audio/
  frame-times.csv
  memory.csv
  results.md
```

Identify source inspection, simulated host tests, real native build, emulator
and physical-device results separately. Retain matching unstripped symbols
for native crash analysis. Remove disposable scratch output after the work;
keep evidence required to reproduce a review.

## 7. Change map, sequence and rollback

| Area | Planned change |
|---|---|
| `build.sh` and bootstrap helpers | Current implementation plus live-host qualification, stronger source lock/provenance and dependency validation |
| App Gradle/CMake/manifest | Coherent pins, explicit dependency imports/closure, renderer capability and lifecycle configuration |
| `src/sdl2keys.*` / `sdl2surface.*` (proposed) | Portable key translation and proven surface/scaling helpers |
| `android_main.h` | Thin Android policy/API boundary; no competing aliases or Vita identity |
| `android_video.c` | Correct dimensions, allocation, frame policy, reset and lifetime |
| `android_main.c` | Writable-root validation, atomic generation-aware assets and entry cleanup |
| `android_gfx.c` | Replace approximation with qualified common/upstream scaling |
| `android_glstub.c` | Unavailable-renderer contract; no successful silent draw path |
| `src/events.c` / `keyboard.c` / `pads.c` / `config.c` | Unified SDL2 key/control domains and persistent mapping |
| `src/video.c` / `gfx.c` / `frame.c` | Platform isolation and one presentation boundary |
| `src/main.c` / `system.c` / `error.c` | Correct ownership order and platform-specific diagnostics |
| Audio/font/stream modules | Scoped API/lifetime/route fixes proven by fixtures |
| Android tests/diagnostic APK | Input, render, asset, audio and lifecycle evidence |
| README/REVIEW/PLAN | Measured status and current instructions |
| Root Makefile | No Android build logic added |

Suggested reviewable sequence:

1. Record B0's real baseline and build provenance; qualify the new bootstrap.
2. Land B1 platform isolation and unsupported-renderer enforcement.
3. Land B2 key/controller fixes with bounds/action tests.
4. Land B3 video/frame/cleanup fixes with counted presentation and resize tests.
5. Land B4 scaler/TTF qualification without unrelated font optimizations.
6. Land B5 music-route/stream/audio fixes with actual decoder fixtures.
7. Land B6 atomic extraction and user-data reconciliation.
8. Land B7 lifecycle/JNI/error validation and B8 device/performance evidence.
9. Add optional touch/import, ABIs or GPU rendering as separate features.

B4/B5/B6 can be developed after platform ownership is established, but their
gates still block claims about supported fonts/audio/data updates. Keep
shared-source changes small and comment deliberate platform behavior changes
as required by AGENTS.

Rollback uses the recorded previous Android APK and compatible signing key,
plus a backed-up data/config generation. Keep source/dependency prefixes and
build output isolated. Do not invent a root `make SDL_BACKEND=1` Android
rollback command or delete user data to conceal a failed migration.

## 8. Optional Android GPU sprite renderer

This is separate from SDL2 migration and from VitaGL. If later requested:

- Use an actual supported OpenGL ES profile and explicit SDL GL context on
  Android; retain System as a diagnostic/recovery mode.
- Port indexed/RGBA shaders to the selected GLES dialect, with explicit
  attributes/varyings and a 2D palette sampler. Preserve PalFX/premultiplication.
- Replace immediate-mode and fixed-function matrix submission with a tested
  vertex/MVP path. Preserve projective UV Q, transforms and transparent draw order.
- Cover calibration, primitives, FBOs, blending, font uploads, capture and final
  present, not only the two main sprite draw functions.
- Do not mix SDL_Renderer and direct engine GL on one active window.
- Require shader compile/link status, actual GPU draw counters and independent
  pixel tests before accepting `RenderMode=OpenGL`.
- Qualify context loss/resume and resource recreation on Android hardware.
  Neither packaging `libGLESv2` nor a stubbed GL function proves this renderer.

## 9. Current commands and primary references

Current setup/check/test workflow is documented in README.md:

```sh
bash port/android/build.sh --setup
source build/android/toolchains/env.sh
bash port/android/build.sh --check-tools
python3 port/android/tests/test_build.py
```

Build SDL2 and companions separately, supply a System-mode game tree, then run
the Android build script. These commands do not implement B1–B8 by themselves.

Primary technical references:

- [SDL2 migration guide](https://wiki.libsdl.org/SDL2/MigrationGuide)
- [SDL2 Android integration](https://wiki.libsdl.org/SDL2/Android)
- [SDL2 source / matching Java integration](https://github.com/libsdl-org/SDL/tree/release-2.32.10/android-project)
- [AGP 8.1 compatibility](https://developer.android.com/build/releases/agp-8-1-0-release-notes)
- [Android SDK manager](https://developer.android.com/tools/sdkmanager)
- [NDK version/path configuration](https://developer.android.com/studio/projects/configure-agp-ndk)
- [Android native page-size compatibility](https://developer.android.com/guide/practices/page-sizes)
- [SDL2_mixer music/RWops contract](https://wiki.libsdl.org/SDL2_mixer/Mix_LoadMUS_RW)
- [SDL2 shutdown](https://wiki.libsdl.org/SDL2/SDL_Quit)

Re-read the current source and selected dependency versions before each
implementation milestone. No unrun gate becomes complete merely because this
plan exists or an earlier document marked a similarly named fix “done”.
