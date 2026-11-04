# Android port (native SDL2)

**Branch:** `sdl2`  
**Status:** native SDL2 — no SDL1 API shim or compatibility layer. The desktop `src/sdl2compat.c/.h` is excluded from Android.

## Architecture decisions

- **SDL2 only** — `ANDROID_PORT` is set; `MUGEN_VITA_GL` and desktop `MUGEN_SDL2` are not enabled. `sdl2compat.c` and `sdlmixercompat.c` are **not** compiled.
- **Renderer** — System (software) rasterization is the supported path. Desktop `OpenGL`/`OpenGLScreen` bodies are excluded before table selection; a fallback diagnostic is emitted if an unsupported cfg is accepted.
- **Keys** — OS SDL2 keycodes are translated to engine config IDs at the boundary; the inverse mapping is used for focus snapshots and names. Stable `mugen.cfg` key IDs are an engine data schema, independently named in `engine_keys.h`.
- **Controllers** — Native SDL2 joystick instance IDs and topology; no Vita bridge or fixed 960×544 panel override.
- **Text/fonts** — `SDL_TEXTINPUT`, `SDL2_ttf`, native surface/palette/alpha APIs; `zoomSurface` and SDL_gfx are not exported.
- **Music/effects** — Native `SDL2_mixer` route for WAV/OGG/MP3. No Windows plugin dependency; `mpg123`/`vorbisplug` are silently skipped if their DLLs are absent.
- **RWops / streams** — `SDL_RWFromFP`/`SDL_RWFromMem` with native size/seek semantics; ownership transferred to the mixer.

## Build entry points

| Path | Purpose |
|---|---|
| `port/android/build.sh` | Host preflight, tool bootstrap, Gradle invocation, APK staging |
| `port/android/app/build.gradle` | ABI, SDK/NDK/CMake pins, signing |
| `port/android/app/src/main/jni/src/CMakeLists.txt` | Native `libmain.so` build |
| `port/android/tests/test_build.py` | Offline bootstrap/orchestration fixtures |
| `port/android/native_tests/` (if present) | Host-compiled Android engine test binary |

See `port/android/README.md` for full toolchain, SDK/NDK pinning, and asset staging steps.

## Key files (audit inventory)

| File | Responsibility |
|---|---|
| `android_main.h` | SDL1 aliases/constants, GL no-ops, SDL key types, forced include order *(removed — not compiled)* |
| `android_video.c` | Window/renderer/texture ownership, logical size, present/quit, mode validation |
| `android_main.c` | SDL entry, writable root, extraction/version stamp, argv, engine calls |
| `events.c` / `keyboard.c` | Key conversion, snapshots, text events, focus state, synthetic input |
| `pads.c` / `config.c` | Instance/slot mapping, button code domain, controller remap precedence |
| `video.c` / `gfx.c` / `frame.c` | Render policy, target sizes, submission, skip-draw behavior |
| `font.c` / `stream.c` / `bgmplugins.c` | SDL2 fonts, RWops, music signatures, lifetime contracts |
| Gradle/manifest/SDL Java files | Library load order, Java/native SDL release match, ABI, package, lifecycle |

## Still open (REVIEW.md A01–A10)

| ID | Area | Status |
|---|---|---|
| A01 | Key indexing / text | Host tested; device validation open |
| A02 | Vita policy leakage | Fixed in source; no Android Vita branches |
| A03 | Unavailable renderer accepted | Fixed; System selected before tables |
| A04 | Asset extraction / retry / upgrade | Upstream B6 fix retained; device validation open |
| A05 | Video cleanup order | Fixed in source, host tested |
| A06 | Stale texture dimensions/format | Fixed in source, host tested |
| A07 | Duplicate presents | Fixed in source, host tested |
| A08 | SDL_gfx imitation | Replaced by native SDL2 scaler |
| A09 | Unusable BGM route | Fixed for WAV/OGG/MP3; host tested |
| A10 | Native BGM thread / pointer ABI | Fixed in source, host tested |

## Evidence boundary

The prior host evidence (validation/release/host-evidence.txt, 8 037 443‑byte APK, SHA‑256 `54ccf452…`) predates the native API removal. A new APK must replace that build evidence.

## Verification performed (host)

- 23 bootstrap/orchestration fixture tests passed.
- Full Android-selected engine + vendor source compiled and linked on Linux x86_64 with native SDL2; Android-only `android_main.c` excluded (requires NDK).
- Native runtime tests passed under ASan/UBSan with dummy video/audio drivers: key IDs, unknown keys, bounds, repeat, text, focus, virtual joystick hotplug/instance IDs, INDEX8/RGBA scaling/transparency, display size/depth changes, reset/background/batching/teardown, music/effects, real BGM registry/worker startup/playback/shutdown.
- WAV, OGG, MP3 fixtures loaded and played through the native mixer route.
- Host ELF inspection: SDL2/mixer2/ttf2 dependencies found; no SDL1 calls, `zoomSurface`, compatibility implementation, or unresolved GL resource calls.
- Deliberate mutations (suppressing dirty marking, returning wrong Up key ID, dropping blend mode, truncating BGM pointers to 32 bits) all fail; original sources restored and suite passed again.
- Dependency gate: native host library accepted, SDL1 `DT_NEEDED` edge rejected; compile guards reject desktop/Vita flags and SDL1 alias.
- All 83 shared C translation units passed Linux syntax checks with desktop SDL2 headers.

## How to reproduce native host tests

```sh
cmake -S port/android/tests -B build/android-native-tests -DNATIVE_SDL2_SANITIZE=ON
cmake --build build/android-native-tests -j4
ctest --test-dir build/android-native-tests --output-on-failure
```

Run with dummy drivers to exercise the real BGM thread and effects:

```sh
cd build/android-native-tests
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./test_native_sdl2 /path/font.ttf /path/music.wav /path/music.ogg /path/music.mp3
```