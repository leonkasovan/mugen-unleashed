# Android native SDL2 review

Updated: 2026-11-02 (Asia/Jakarta). Implementation base: `2ab0dcd` on branch `sdl2`.
The upstream B0–B8 source fixes and baseline APK evidence are retained.
The native API removal below supersedes the adapter-based architecture.

## Current architecture

| Area | Implemented Android behavior |
|---|---|
| SDL ABI | Native SDL2 headers/runtime; companion headers must be major 2 |
| Compatibility code | Android does not compile `sdl2compat.c` or `sdlmixercompat.c`; no forced header or SDL1 aliases |
| Removed Android files | `android_main.h`, `android_gfx.c`, `android_glstub.c` |
| Platform identity | `ANDROID_PORT`; neither `MUGEN_VITA_GL` nor desktop `MUGEN_SDL2` is enabled |
| Video | SDL_Window + SDL_Renderer + streaming SDL_Texture; CPU pixels at game resolution |
| Renderer selection | System is selected before tables/resources; unavailable desktop GL modes log a fallback |
| Keys | OS SDL2 keycodes translated to engine config IDs; exact inverse for names/focus state |
| Controllers | Native joystick instance IDs/topology; hotplug and removal release; no Vita map override |
| Text/fonts | SDL_TEXTINPUT, SDL2_ttf, SDL2 surface/palette/alpha APIs; no SDL_gfx dependency |
| Music/effects | Native SDL2_mixer; Android BGM route supports WAV/OGG/MP3; no Win32 plugin dependency |
| Streams | SDL_RWFromFP/SDL_RWFromMem, including native size/seek semantics and explicit ownership |
| BGM concurrency | SDL2 thread/mutex/condition; full-width pointers in the two-word command payload |
| Shutdown | Video resources released before SDL_Quit; texture/window/pixel pointers reset |
| Build ownership | Android Gradle/CMake; root Makefile remains Windows/Linux only |

CPU/software rendering is a supported SDL2 design. SDL2's internal GLES
backend does not implement MUGEN's desktop OpenGL sprite renderer.
The engine's saved numeric key schema does not constitute SDL API emulation.

## Finding disposition

| ID | Status | Implementation/evidence |
|---|---|---|
| A01 key indexing/text | Fixed in source, host tested | Native key forward/inverse mapping, bounded writes, keyboard snapshots, text events, stable engine constants |
| A02 Vita policy leakage | Fixed in source, host tested for video/input | Vita macro removed; native header rejects it; no Android Vita panel/key/stdio/config branches |
| A03 unavailable renderer accepted | Fixed in source | Config selects System before Gfx_SelectRenderer; GL bodies/table entries excluded rather than stubbed |
| A04 asset extraction/retry/upgrade | Upstream B6 source fix retained; device validation open | Atomic .part copy, size/FNV verification, generation stamps, cfg preservation; writable-path failure/upgrade recovery still need qualification |
| A05 video cleanup order | Fixed in source, host tested | Video_Deinit destroys native display before System_Deinit calls SDL_Quit; repeated native teardown tested |
| A06 stale texture dimensions/format | Fixed in source, host tested | Mode creation allocates matching texture/surface before replacing old resources; dimensions/depth validated |
| A07 duplicate presents | Fixed in source, host tested | Dirty marking only records work; frame boundary presents once, respects skip_draw and background state |
| A08 local SDL_gfx imitation | Replaced | Android text scaler operates on native INDEX8/32-bit SDL2 surfaces, preserving palette/key/modulation/blend; shared pixel algorithms retained; 16-bit equivalence is not newly inferred from host text tests |
| A09 unusable BGM route | Fixed for enabled formats, host tested | One Android SDL2_mixer route for WAV/OGG/MP3, native RWops ownership, no mpg123 DLL startup dependency |
| A10 native BGM thread/pointer ABI | Fixed in source, host tested | Removed Windows pthread-storage assumptions on Android; SDL2 handshake/thread/join and full 64-bit command-pointer packing |

A10 was discovered while exercising the real Bgm_Init → Bgm_Play → Bgm_Deinit
path. The old pointer-sized pthread fields cannot store Android pthread mutexes
or conditions, and storing a pointer in only the first 32-bit command word
truncates it on arm64. Both fixes are Android-specific deliberate port changes.

## Evidence boundary

[The prior host evidence](validation/release/host-evidence.txt) records an
8,037,443-byte APK, SHA-256
`54ccf452eb1eb0708acb3421dfe299c2398143c7a0894e0b18945a6a2ccfa350`,
with arm64/API-21 native libraries and SDL_main/Mugen_Main exports. It predates
this shim removal and does not verify the new source. Its original files and
Gradle/asset changes are preserved; a new APK must replace that build evidence.

## Verification performed

- 23 bootstrap/orchestration fixture tests passed.
- The complete Android-selected engine/vendor source set compiled and linked on
  Linux x86_64 with native SDL2. Android-only `android_main.c` was excluded from
  this host test; it still requires the NDK and Android platform libraries.
- Native runtime tests passed under ASan/UBSan with dummy video/audio drivers:
  key IDs/unknown keys/bounds/repeat/text/focus, virtual joystick hotplug and
  instance IDs, INDEX8/RGBA scaling and transparency, real solid/blended TTF,
  display size/depth changes, reset/background/batching/teardown, music and
  effects, and real BGM registry/worker startup/playback/shutdown.
- WAV, OGG and MP3 fixtures were each loaded and played through both the direct
  native mixer route and the engine BGM worker. Output was not listened to.
- Host dependencies tested: SDL2 2.30.0, SDL2_mixer 2.8.0, SDL2_ttf 2.22.0.
  Android production dependencies remain separately pinned by its build setup.
- Host ELF inspection found SDL2/mixer2/ttf2 dependencies and no SDL1 calls,
  `zoomSurface`, compatibility implementation, or unresolved GL resource calls.
- Deliberate mutations were detected: suppressing dirty marking, returning the
  wrong Up key ID, dropping surface blend mode and truncating BGM pointers to
  32 bits all fail. Original sources were restored and the suite passed again.
- The dependency gate accepts the native host library and rejects an injected
  SDL1 DT_NEEDED edge. Compile guards reject desktop/Vita flags and an SDL1 alias.
- All 83 shared C translation units passed Linux syntax checks with SDL1 and
  with desktop SDL2 headers; actual MSVC/MinGW/Vita builds remain unverified.
- LeakSanitizer could not inspect `/proc` in this environment; leak detection
  was disabled for these runs. Address/undefined-behavior checks remained on.

Reproduce the native tests with [README.md](README.md). No fixture APK or
mocked SDK/Gradle result is counted as a native Android build.

## Still open before a qualified Android release

1. Qualify the retained B6 asset implementation for interrupted extraction,
   upgrades, user-edited files, full storage and inaccessible storage paths.
2. Rebuild the actual arm64 APK after these native API changes using NDK 27.1.12297006 and the pinned
   Android SDL2/ttf/mixer libraries. Verify Java/native version agreement,
   SDL_main exports and the complete APK ELF dependency closure.
3. Test physical input and all title/options/character-select/fight/console
   paths, controller reconnect/remapping and background input reconciliation.
4. Test Android window/context recreation, pause/resume, audio focus/device
   changes, error exit and repeated activity start/stop on devices.
5. Listen to music/effects; validate per-format looping, seeking, volume,
   muting, transitions and missing/unsupported-file errors.
6. Qualify game frames/screenshots, pitch/depth limits, performance, memory
   use and fidelity against the desktop/reference engine.
7. Add tracker/MIDI decoders only with actual engine routing and codec tests.
   Touch controls, a native GLES sprite renderer, extra ABIs and release-store
   policy/tool updates remain separate work.
8. Run MSVC, MinGW and device/Vita regression checks on their toolchains.
   These toolchains/devices are not available in this session.
