# Android port: validated review

Reviewed: 2026-11-02 (Asia/Jakarta).
Source baseline: `d14b4e3b05bad838a1a44c3691b48edf35ab69f6` on `sdl2`.
Scope: Android Gradle/CMake, native adapter, relevant shared engine paths,
library builder and build orchestration. See [PLAN.md](PLAN.md) for remediation.

**Verdict: SDL2 is already the Android runtime dependency, but the migration
of SDL1 engine semantics is incomplete. Multiple source defects remain.
The previous “every statically identifiable defect is fixed” conclusion is
withdrawn. This update fixes toolchain/bootstrap orchestration, not the native
runtime defects listed OPEN below.**

The current work ran source checks, a small host presentation probe, Bash
syntax checks and 23 isolated bootstrap/build-orchestration tests. It did not
build a real APK, download/execute a real SDK/JDK distribution, run Android
hardware/emulation, or rebuild desktop/Vita binaries. Historical APK sizes,
symbol listings and multi-platform build claims from the previous review
are not newly verified evidence.

## 1. What the source establishes

| Area | Current fact |
|---|---|
| Runtime SDL | Native CMake links `libSDL2.so` plus SDL2_mixer/SDL2_ttf, not SDL1 libraries |
| Engine compatibility | Force-included `android_main.h` supplies removed SDL1 names; `android_video.c` implements a software surface / streaming texture path |
| Renderer | `glrender.c` and `gldetect.c` are excluded; this does not itself prevent config from selecting their stubbed entry points |
| Entry | The Android entry includes `SDL_main.h`; shared `src/main.c` is renamed `Mugen_Main` under `ANDROID_PORT` and retained for its helpers |
| API guards | `ANDROID_PORT` enables `MUGEN_SDL2_API`, but not `MUGEN_SDL2`; important key/text adapters still require the latter |
| Platform guards | Android CMake also defines `MUGEN_VITA_GL`; several genuinely Vita-specific blocks remain active on Android |
| ABI | Gradle packages `arm64-v8a` only, min API 21, compile API 34, target API 29 |
| Tool pins | AGP 8.1.1, wrapper Gradle 8.1.1, NDK 27.1.12297006, CMake 3.22.1, JDK 17; this change pins build-tools 33.0.1 explicitly |
| Libraries | SDL2 itself is supplied separately; companion builder selects SDL2_ttf 2.20.2 and SDL2_mixer 2.6.3 |
| Runtime evidence | No new device, audible-audio or frame-rate result is available in this review |

SDL2 retaining software surfaces is compatible with the engine's CPU renderer.
A GPU-presented software frame is not an implemented MUGEN OpenGL renderer.

## 2. Open findings

### A01 — P1: SDL2 special keys still reach legacy arrays unconverted

Sources: [events.c](../../src/events.c), [keyboard.c](../../src/keyboard.c),
[native CMake](app/src/main/jni/src/CMakeLists.txt),
[compatibility header](app/src/main/jni/src/android_main.h).

The `SDL_KEYDOWN/UP` path translates through `Sdl2_EngineKey` only when
`MUGEN_SDL2` is defined. Android does not define it, so large SDL2 special-key
values are passed directly to `Keyboard_HandleKey`, which writes
`g_keyState[key]` without checking the upper bound. SDL2 Up is
`0x40000052`; the engine's legacy Up binding is 273.

The Android focus-resync branch also calls `SDL_GetScancodeFromKey(i)` with
legacy engine identifiers. That works for ASCII, but legacy 273 is not SDL2's
Up keycode. The earlier H1 fix therefore did not solve the whole key domain.
The text-input event path is also still gated on `MUGEN_SDL2`.

Required: one reusable forward/inverse translation for all SDL2 platforms,
including event, resync, saved bindings and synthetic-input paths. Do not
merely define `MUGEN_SDL2` on Android: that also enables a competing video
adapter and conflicts with existing aliases unless ownership is separated.

Status: **OPEN**. Source-confirmed bounds defect; no Android crash trace was
generated for this review.

### A02 — P1: Vita controls and panel policy still leak into Android

Sources: [events.c](../../src/events.c), [config.c](../../src/config.c),
[video.c](../../src/video.c), [main.c](../../src/main.c).

Defining `MUGEN_VITA_GL` also enables:

- The Vita joystick-to-keyboard bridge, including direct calls to
  `Keyboard_HandleKey` with SDL2 arrow constants and Vita raw-button assumptions.
- The Vita config override that replaces both players' joystick maps with
  fixed four-axis/no-hat Vita engine codes, discarding actual Android bindings
  and assuming a controller topology/mapping that Android need not have.
- A System surface request forced to 960x544 in `Video_SetMode`. The Android
  presentation branch does not perform the Vita CPU upscale, so this is not
  harmless target naming or a faithful use of configured game dimensions.
- Vita `ux0:data/...` stdout/stderr redirection in the shared entry point.

The previous M3 fix excluded some `gfx.c` paths only. It did not isolate all
Vita-specific behavior. Android controllers have their own topology/mapping,
and legacy button numbers 9..24 are engine codes, not proof of a 24-button pad.

The intervening Vita fix converted its raw button indexes to engine codes
(raw + 9 for that specific topology). That correction is present in this
review's updated baseline; it does not resolve the Android platform guard or
controller-policy leakage. The old raw-zero mismatch is not reported as a
current Vita defect here.

Required: separate platform identity from GL header/renderer capability;
retain Android lifecycle behavior while removing Vita policy from Android.
Preserve the configured game resolution, including 640x480 where selected.

Status: **OPEN**.

### A03 — P1: “System is forced on Android” is not implemented

Sources: [config.c](../../src/config.c),
[video.c](../../src/video.c),
[GL stubs](app/src/main/jni/src/android_glstub.c).

The parser still accepts `OpenGL` and `OpenGLScreen` without an Android
override. `Video_SetMode` skips `Gl_Init` under `ANDROID_PORT` and can return
success, while GL drawing entries are no-ops/failure stubs. The native
adapter/README comments saying config forces System are false.

Required: reject unsupported modes before selecting GL function tables, or
perform an explicit logged fallback to System. A desktop cfg requesting GL
must not produce an apparently initialized blank renderer.

Current workaround: supply `RenderMode=System` in the game cfg actually used
on the device. The build script does not rewrite it, and existing extracted
cfg files may survive an APK upgrade.

Status: **OPEN**.

### A04 — P1: asset retries can preserve a truncated file and stamp success

Source: [android_main.c](app/src/main/jni/src/android_main.c),
`extract_asset` / `extract_assets`.

Extraction writes directly to the final file. After an interrupted/short
write, that partial file can remain. The next launch skips any existing
destination using only `stat`, returns success for it, and can write the
completion stamp. Therefore “stamp only after success” does not establish
crash-safe extraction.

The stamp name is also a constant generation marker, not an automatic
manifest/content hash. A new APK payload does not invalidate it by itself.
User-modified files and packaged updates need an explicit reconciliation rule.

Required: verify relative paths, copy to a temporary file in the same writable
directory, check read/write/close and size/hash, then rename atomically.
Stamp only the validated manifest generation; define how modified user files
are preserved. Handle full storage and unavailable external/internal paths.

Status: **OPEN**. Also test Android's temporary-file support instead of assuming
`tmpfile()` succeeds in the app sandbox.

### A05 — P1: successful-exit video cleanup occurs after SDL_Quit

Sources: [main.c](../../src/main.c), [system.c](../../src/system.c),
[android_video.c](app/src/main/jni/src/android_video.c).

`Mugen_Main` calls `System_Deinit`, which ends with `SDL_Quit`, and only later
calls `Android_VideoQuit`. That helper still holds and destroys its texture,
renderer and window pointers after SDL video shutdown may have invalidated
them. It also does not reset every initialization/dimension/dirty state field.

Required: establish one owner and destroy Android presentation objects before
SDL subsystem shutdown; make partial cleanup and repeated init/quit safe.
Simply proving `Android_VideoQuit` is called does not qualify its placement.

Status: **OPEN**. Native shutdown-order hazard; no device teardown test here.

### A06 — P2: resizing updates the surface but retains the old-size texture

Source: [android_video.c](app/src/main/jni/src/android_video.c),
`Android_SetVideoMode`.

The function changes logical/window size and recreates the software surface,
but destroys the texture only when its pixel format changes. A same-format
size change therefore leaves an old-size streaming texture paired with a
new-size source surface/pitch. The result can be an error, cropped frame or
invalid memory access during upload, depending on the dimensions/backend.

The 24bpp branch also pairs a 24bpp source surface with a 32-bit RGB888 texture.
That requires an explicit conversion or rejection, not a comment that 24 is
“stored as 32-bit”.

Required: recreate textures on dimensions or format change, validate allocation
results transactionally and test actual bytes-per-pixel/pitch for each exposed
mode. Keep unsupported display bpp separate from indexed offscreen surfaces.

Status: **OPEN**. The old M1 fix is partial.

### A07 — P2: dirty state does not batch presentation once per frame

Sources: [android_video.c](app/src/main/jni/src/android_video.c) and
[frame.c](../../src/frame.c).

`Android_UpdateRectX`, `Android_Flip` and `Android_PresentFrame` each set
`s_dirty=1` and immediately call `present`. Clearing it inside `present` cannot
collapse the next call, because that call sets it again.

A host probe using those extracted source functions with counted SDL stubs ran
one UpdateRect, one Flip and one frame-end request: **3 uploads, 3 presents**.
This establishes the adapter behavior; it is not an Android performance test.

Required: distinguish invalidation from the one frame-end flush, account for
skip-draw and explicit non-frame initialization/error presentation, and assert
upload/present counts. The old M2 “FIXED” conclusion is withdrawn.

Status: **OPEN**.

### A08 — P2: Android zoomSurface is not established equivalent to SDL_gfx

Source: [android_gfx.c](app/src/main/jni/src/android_gfx.c).

The wrapper truncates output dimensions, calls `SDL_BlitScaled` and does not
restore the source alpha/color modulation and blend mode as a pure scaling
operation. SDL_gfx uses distinct indexed/RGBA sampling/conversion rules.
The more thoroughly tested shared SDL2 scaler is not enabled in this Android
profile. A handwritten signature and a successful link prove neither state
preservation nor pixel equivalence.

The comment that no SDL2_gfx port exists is wrong. Upstream SDL2_gfx provides
a surface rotozoom API; the correct choice is a validated implementation,
not dependency avoidance justified by nonexistence.

Required: reuse/extract the shared validated scaler or prove an alternative
against indexed, 16-bit, 32-bit, fractional-size, nonzero-key and real-alpha
fixtures. Compare actual masks/state with `SDL_GetRGBA`.

Status: **OPEN**.

### A09 — P2: advertised BGM formats do not match the engine route

Sources: [companion builder](tools/build_sdl2_companions.sh),
[bgmplugins.c](../../src/bgmplugins.c), [compat_linux.c](../../src/compat_linux.c).

The companion builder enables WAV/OGG/MP3-capable SDL2_mixer code while disabling
MOD and MIDI. The engine's `Sdlmix_Open` rejects WAV/OGG/MP3 and accepts
tracker/MIDI paths. The mpg123 subsystem separately tries to load
`libmpg123-0.dll`, which this Android package does not supply.

Thus codec capability alone does not make the documented music formats work.
A configured plugin init failure can also stop the registry before another
usable subsystem is reached. SND effect playback is a different path.

Required: choose and test an Android music adapter/extension route, or enable
the actual tracker/MIDI engine route with its dependencies/assets. Qualify
loop/seek/volume and callback/RWops lifetimes before claiming support.

Status: **OPEN**.

## 3. Build fixes implemented in this update

| Change | Evidence / limit |
|---|---|
| Explicit opt-in missing-tool downloads | `--setup` and `--download-tools`; default build/check does not call the bootstrap downloader |
| Consistent SDK roots and pinned versions | SDK root priority is explicit; NDK revision comes from `source.properties`, not a directory label |
| External NDK selection | `build.sh` passes `mugenNdkPath`; Gradle consumes it while retaining its version check |
| Reproducible SDK package selection | SDK 34, NDK 27.1.12297006, CMake 3.22.1 and build-tools 33.0.1 read from project settings |
| JDK selection | Require runnable JDK 17, including javac; reject an invalid explicit override |
| Downloads and extraction | HTTPS, vendor checksum verification, staged extraction, retryable partial-download handling and archive-path checks |
| Location of generated files | Default SDK/JDK/download/Gradle/temp state stays under `build/android/`; no profile edits or sudo |
| SDK licenses | SDK manager retains its own prompts; no automatic “yes” or fabricated acceptance |
| Companion prefix mismatch | Corrected its repository-root calculation and aligned SDL2/NDK/prefix defaults with `build.sh` |
| Stale assets / wrong APK variant | Regenerate staging even for `--no-assets`; collect only the requested debug/release variant |
| ABI labeling | Reject non-arm64 requests until Gradle and all native libraries actually support them |
| Machine-specific tracked property | Stop rewriting `gradle.properties.override`; use exported Java home / explicit Gradle arguments |

The companion source downloader itself is still the existing implementation:
its source archive provenance/checksum hardening and full dependency closure
are follow-up work in PLAN.md. The bootstrap does not automatically rebuild
SDL2 or its companion libraries.

## 4. Revalidation of previous finding IDs

| Previous ID / claim | Updated assessment |
|---|---|
| H1 keycode/scancode fixed | Incomplete; A01 and A02 reopen the relevant paths |
| H2 device execution absent | This review adds no device/emulator evidence |
| H3 release signing fixed | Conditional release signing exists; missing/nonexistent keystore still selects debug signing |
| M1 resize fixed | Logical/window size updates exist; texture resizing is incomplete (A06) |
| M2 present batching fixed | False for current code; counted host probe shows three presents (A07) |
| M3 Vita leakage fixed | Some gfx guards changed; video/input/config/stdio leakage remains (A02) |
| M4 fake JNI export removed | Removal is visible in source; no new ELF symbol scan performed |
| M5 argv preserved | Java argv is retained and file tokens appended; quoting is not supported and counts/line length are bounded |
| L8 video cleanup fixed | Called, but after SDL_Quit; ordering remains wrong (A05) |
| L9 sibling-repo dependency | Build/companion defaults now local; actual SDL2 libraries must still be built/supplied |
| L10 touch controls | Still unimplemented; a gamepad/keyboard is required, and current input defects still need fixing |

Do not treat a symbol appearing in disassembly as proof of the correct
translation, ownership, texture size, frame count or audible output.

## 5. Tests and remaining qualification

Run from the repository root:

```sh
bash -n port/android/build.sh
bash -n port/android/tools/bootstrap_toolchains.sh
bash -n port/android/tools/build_sdl2_companions.sh
python3 port/android/tests/test_build.py
```

The 23 isolated tests use fake curl, JDK, SDK manager, Gradle and native
libraries. They exercise missing-tool behavior, exact package requests,
checksums, archive traversal, valid/invalid symlinks, failed-download retry,
idempotence, version mismatch, custom paths with spaces, offline gating,
APK variant selection and asset staging without modifying the reference tree.

The live Google repository XML was also fetched and parsed successfully for
command-line tools 19.0 on Linux, Windows and macOS (archive build 13114758).
Actual SDK archive/JDK API access timed out through the current proxy, so real
archive installation and SDK manager execution remain unverified.

These are meaningful bootstrap/orchestration tests, **not a native build**.
Still required: live Google/Adoptium downloads, real SDK-manager/license flow,
real Gradle/APK build on the supported hosts, native ELF dependency/symbol
checks and actual device startup/input/render/audio/lifecycle tests.
macOS and Windows native tool execution were not tested by the Linux fixtures.

Primary references:

- [AGP 8.1 compatibility](https://developer.android.com/build/releases/agp-8-1-0-release-notes)
- [SDK manager](https://developer.android.com/tools/sdkmanager)
- [NDK version/path selection](https://developer.android.com/studio/projects/configure-agp-ndk)
- [Google SDK manager launcher](https://android.googlesource.com/platform/prebuilts/cmdline-tools/+/db049c98617976c1d4d337c12034ab6e515911cb/tools/bin/sdkmanager)
- [Adoptium API installation examples](https://adoptium.net/installation/ci-scripts/)
- [SDL2 shutdown](https://wiki.libsdl.org/SDL2/SDL_Quit)
