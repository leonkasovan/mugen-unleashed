# SDL2-only implementation plan

Base: `6753f6b898b8c9067f900720c6043418429351eb` (`sdl2`).
Requested scope: retire MSVC/root CMake and remove SDL 1.2 support. Android,
Vita and their CMake projects remain supported. This is an engine platform
migration, not a change to gameplay, config key numbers or screen resolution.

## Supported build contract

| Platform | Build | SDL and rendering |
|---|---|---|
| Windows x86 / x86-64 | GNU Make + MinGW-w64, existing dependency kits | SDL2, SDL2_mixer, SDL2_ttf; desktop OpenGL/System |
| Linux x86-64 / ARM64 | GNU Make, native or cross toolchain | SDL2; desktop GL or GLES/System as configured |
| Android | Existing Gradle + NDK CMake | Native SDL2; existing System/GLES paths |
| PS Vita | `port/psvita/build.sh` + VitaSDK CMake | Native SDL2; native vitaGL owns GL display |

Root MSVC presets/import-library generation are removed. Platform CMake files,
Vita package/configuration modules and Android test CMake are retained. A
user's `install/` tree, original executables and unrelated changes are untouched.

## Implementation order and acceptance checks

### 1. Build and distribution

- Remove root `CMakeLists.txt`, `CMakePresets.json`, root MSVC-only `cmake/`
  helpers, `tools/msvc_env.bat` and `docs/BUILD-MSVC.md`.
- Remove the SDL1 backend selection and dependency queries from Make. Keep
  existing `-sdl2` output names and configuration stamping. Reject an explicit
  obsolete `SDL_BACKEND=1` rather than silently building something else.
- Remove tracked SDL1 SDK headers and SDL-related `.def` imports. Keep
  independently used pthread/zlib headers until their consumers no longer need
  them; this task does not require a threading ABI rewrite.
- Change CI to GNU Make and SDL2 dependencies. Cover branch `sdl2` and PRs;
  package the actual SDL2 outputs. Do not publish an SDL1/MSVC artifact.
- Check dependency metadata and executable imports/NEEDED entries for SDL1.

### 2. SDL2 engine operations

- Replace `sdl2compat` with an explicitly named SDL2 video backend. Keep its
  transactional surface/texture allocation, renderer reset, one-present-per-
  frame logic, fullscreen behavior and Vita display ownership.
- Replace legacy SDL function aliases at their call sites with engine/backend
  operations. SDL2 functions must retain their real signatures and meaning.
- Replace old SDL video flag spellings with engine flags. The numeric values
  remain internal and are never passed directly to SDL2 window APIs.
- Give swap interval its own operation, separate from SDL_GLattr values.
- Remove fake SDLKey types, key-repeat/Unicode no-ops, SDL_putenv and the
  borrowed SDL1 entry-point implementation. Preserve command-line argument
  handling, exit status and Windows log behavior in an SDL2 entry point.
- Always require SDL_MAJOR_VERSION == 2. Remove SDL1 branches in event,
  keyboard, surface, configuration, RWops and mixer ownership handling.
- Keep engine key IDs from `engine_keys.h`: existing numeric mugen.cfg
  bindings, console commands and gamepad persistence must not be renumbered.

### 3. Fonts, scaling and sound

- SDL2_ttf is the only font library. Keep palette/color-key/alpha semantics.
- Call `Sdl2_ScaleSurface` directly. Remove the SDL_gfx dependency and the
  `zoomSurface` facade; do not introduce SDL2_gfx for this operation.
- SDL2_mixer is the only mixer. Keep two-argument Mix_LoadMUS_RW ownership
  handling on both success and failure and use SDL2 RWops size/seek/read types.
- Give the engine mixer-spec/chunk-size query an engine name rather than
  pretending stock SDL2_mixer exports Elecbyte's Mix_QuerySpecEx extension.
- Preserve existing BGM mutex/thread backends and pointer-width fixes.

### 4. Tests, documentation and safeguards

- Move all active test builds to SDL2 development headers and libraries.
  Tests must not depend on deleted SDL1 SDK headers or a hardcoded Windows
  `-m32` link line when running on native Linux.
- Preserve scaling and render fixture coverage using recorded synthetic
  reference outputs; SDL1 libraries are not required to run the final suite.
  Record fixture provenance. Never create expected values from the candidate
  implementation during a test run.
- Adapt GL mocks to SDL2 types/events and retain failure-path assertions.
- Add an automated SDL2-only contract check for obsolete build artifacts,
  headers/imports, source aliases and accidental SDL1 dependencies.
- Update current README/build/port instructions. Old analysis and historical
  migration records may describe SDL1 but must be marked as historical where
  they otherwise look like current build instructions.

### 5. Qualification

Run appropriate available checks after integration:

1. Full Linux x86-64 GL and GLES builds, plus a stable incremental rebuild.
2. SDL2 scaler/key/input tests; config merge and gamepad persistence for
   desktop/Android/Vita host variants.
3. Audio callback, tail silence, muted callback and RWops ownership/size tests
   using the dummy audio driver; decoding/effects tests where fixtures exist.
4. Render/GL regression tests and real offscreen GL model pixels/context
   recreation. Missing runtime capabilities are SKIP, never PASS.
5. Android host/build-contract tests and native VitaSDK ARM build/SELF/VPK if
   the SDK is available. MinGW 32/64 builds if compatible toolchains/dependency
   kits are available.
6. Physical-device checks remain explicit: audible output, controllers,
   System/OpenGL screenshots, suspend/resume and renderer performance.

Each unavailable build or hardware test is reported as unverified. Do not
claim Windows/Vita runtime correctness from Linux mocks or cross-linking.

## Completion record — 12 November 2026

All implementation steps above are complete. This records implementation
completion, not qualification of hardware that this host cannot access.

- Removed root MSVC/CMake files, helper scripts, original game-DLL manifest,
  all generated-import definitions and the vendored SDL1 SDK headers.
- Make requests only SDL2/SDL2_mixer/SDL2_ttf. Its configuration stamp changes
  from the old backend ID so an existing output directory rebuilds safely.
  `SDL_BACKEND=1` now fails with an explicit error; a stable SDL2 build does
  no shader generation, compilation or linking.
- `sdl2compat.c/.h` is replaced by `sdl2video.c/.h`, an engine display backend.
  Removed legacy SDL function aliases, fake SDLKey/GL attributes, old event
  structures and SDL1 RWops/mixer ownership branches. Engine display request
  bits are named DISPLAY_*, and are explicitly translated to SDL2 window flags.
- Added explicit Video_SwapBuffers/Video_SetSwapInterval dispatch so native
  VitaGL never swaps an SDL window. Deleted the old SDL1 Vita double-buffer
  fallback; System presentation uses the SDL2 frame boundary.
- Shared scaler and saved-key translation remain in sdl2surface/sdl2keys.
  All scale callers use Sdl2_ScaleSurface directly. Font/input configuration
  IDs and existing resolution settings are preserved.
- Sound_QueryMixerSpec is an engine operation over stock SDL2_mixer, shared
  by every build. SDL2_mixer owns music RWops through freesrc=1; the SDL1
  wrapper ownership table is gone. The original BGM threading ABIs remain.
- Windows startup keeps command-line/log handling in platform_main_win32.c;
  it calls SDL_SetMainReady and the engine main, without SDL1 module-handle
  setup or the DDRAW preload.
- Tests use SDL2 headers. The pure key/surface suite is now named
  test_sdl2helpers. Frozen synthetic scaler/render manifests under
  tools/fixtures identify their independent pre-change commit, runtime,
  byte counts and hashes. They record the previous SDL2 implementation;
  this task did not manufacture or claim a fresh SDL1 comparison.
- CI builds MinGW win32/win64 and Linux SDL2, audits executable imports,
  and packages SDL2 executables. Tags retain standalone release ZIP uploads.
  Android/Vita platform CMake and SDK checks remain in place.

### Actual verification

Environment: Linux x86-64, GCC, SDL2 2.32.4 and Mesa 25.0.7 offscreen software
rendering; native VitaSDK ARM cross-toolchain with ordinary SDL2 packages.

| Check | Result |
|---|---|
| Full Linux desktop GL build | Pass |
| Full Linux GLES build | Pass |
| Repeated unchanged desktop build | No shader/compile/link steps |
| Native VitaSDK engine, ELF metadata, SELF and VPK | Pass; build-only package, no bundled compiler/game assets |
| Android native host engine library and CTest | Pass, 1/1; keyboard/text, controller hotplug, TTF/surface, presentation and WAV decode/playback paths |
| Shared module syntax gates, desktop/Android/GLES | All pass |
| Linux qualification runner | PASS=24, FAIL=0, SKIP=1 |
| Dual-ABI layout comparison | SKIP: 64-bit layout/syntax recorded; 32-bit Windows compiler unavailable |
| Scaler and render baselines | 21 + 3 fixtures byte-identical to 6753f6b |
| Frozen baseline mutation | Zero-filled output is rejected |
| SDL2 input/key/surface helpers and raw-joystick fallback | Pass |
| Fullscreen, resize transaction/reset and present-count tests | Pass on host SDL2 |
| Gamepad mapping/config persistence, desktop/Android/Vita variants | All pass |
| Audio hook, tail silence, mute, mixer query and RWops size | Pass with dummy audio device |
| Real textured glTF rendering, fresh GL contexts | Pass on Mesa GL 4.5 compatibility |
| Real SDL2 GLES loader failure/retry | Pass on Mesa GLES 3.2 |
| CNS and build-warning regression suites | Pass |
| Config merge | 42 cases pass |
| ASan/UBSan config, CNS and desktop gamepad persistence | Pass; leak detection disabled for host SDL dependencies |
| Vita build/config bootstrap tests | 17 pass |
| Audio target-selector tests | 4 pass |
| ARM tool/bootstrap/sysroot tests | 9 pass |
| Android bootstrap/build-orchestration tests | 26 pass |
| Source/dependency contract and Linux ELF audit | Pass; no legacy build inputs or SDL1 dependencies |
| CI YAML/source configuration | Validated locally; remote jobs not counted as local build evidence |

Two stale test fixtures were corrected while moving the harnesses to SDL2:
the Android host controller test now sends controller events to its mapped
slot, while separate raw-device tests retain raw-event coverage. ARM bootstrap
tests now initialize their required scratch directory and exercise the current
release-sysroot installer instead of invoking its retired generator. Assertions
remain active; unavailable ABI comparisons return 77 and are recorded as SKIP.

### Still unverified

- Windows MinGW 32/64-bit builds and Windows runtime; the updated CI jobs are
  configured to perform builds but were not run on this Linux host.
- Android NDK/APK rebuild, physical Android execution and Vita installation/run.
- Audible sound, physical controllers, suspend/resume and the full game
  screenshot/performance matrix. Dummy audio proves callbacks/playback APIs,
  not what a speaker sounds like.
- The qualifier's manual E2E, GPU-reference and device rows remain OPEN.
  The imported desktop 3D features retain their previous asset-coverage gaps.

The historical SDL1-capable baseline remains retrievable at the base commit.
Active builds and tests do not need SDL1 development/runtime libraries.
