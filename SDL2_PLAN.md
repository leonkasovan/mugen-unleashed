# SDL 1.2 to SDL2 Migration Plan

## Goal

Port the desktop M.U.G.E.N reconstruction from SDL 1.2 to SDL2 while preserving the original engine's observable behavior, especially input timing, audio output, rendering, and startup/shutdown behavior. Keep the current SDL 1.2 build usable until the SDL2 path passes the comparison gates below.

The repository already has an Android port that uses SDL2. Treat it as a source of project-specific experience and reusable code, but do not assume its mobile video/input backend is interchangeable with the desktop engine.

## Findings and constraints

- The desktop engine is a C reconstruction of the original 32-bit Windows executable. It uses SDL 1.2 and SDL add-ons, with MSVC/CMake and MinGW/Make build paths.
- `src/sdlmain.c` contains the reconstructed SDL 1.2 `SDLmain` behavior. SDL2's `SDL_main` is optional and has a different role; review this file and the entry point before deciding whether to retain, replace, or bypass it.
- Existing Linux, Windows, and Android ports mean the migration should be isolated behind a small platform layer or compile-time backend selection. Avoid broad changes to engine logic and reconstructed data structures.
- Rendering has both System/software and legacy OpenGL paths. Preserve both. Use an SDL2 window and GL context for GL; retain CPU surfaces for System and choose one explicit presentation backend for it.
- SDL2 has separate versioned add-ons (SDL2_gfx, SDL2_mixer, SDL2_ttf, etc.). Verify exact API and ABI requirements for every linked library and the compatibility shim `src/sdlmixercompat.c`.

## Recommended implementation sequence

### 1. Inventory SDL 1.2 dependencies

Create a call-site inventory before editing. Search all first-party source, headers, resources, and build files for:

- Video: `SDL_SetVideoMode`, `SDL_Flip`, `SDL_UpdateRect(s)`, surface flags, palettes, `SDL_VideoInfo`, mode enumeration, caption/icon/cursor calls.
- OpenGL: `SDL_GL_* `, context setup, swap buffers, fullscreen and resize handling.
- Input/events: event unions, key symbols and Unicode, mouse coordinates/buttons, joystick APIs, event masks, resize/focus/quit handling.
- Audio and add-ons: SDL audio callback/buffer assumptions, SDL_mixer, SDL_ttf, SDL_gfx, RWops, timers, threads, and initialization flags.
- SDLmain: `main`/WinMain entry flow and command-line handling.

Record each call site, current behavior, proposed SDL2 equivalent, and test case. Include generated or vendored code only when it is compiled into the desktop targets.

### 2. Add SDL2 dependencies and a selectable build

- Add an explicit SDL2 build option/preset while retaining SDL 1.2 as the temporary default.
- Update both `CMakeLists.txt` and `Makefile` to discover SDL2 and the matching SDL2 add-ons. Keep SDL1 and SDL2 include/library paths isolated to prevent header or linker mixing.
- Update CI to build both configurations where dependencies are available.
- Pin/document supported SDL2 and add-on versions, static versus shared linking, runtime DLL deployment, and Windows/Linux package requirements.
- Keep platform-specific entry-point and resource handling separate from the engine migration.

### 3. Port initialization and window/context management

Replace SDL 1.2 global video mode setup with SDL2 window creation and explicit OpenGL context creation. Set required GL attributes before creating the window/context, preserve the engine's requested color/depth/double-buffer behavior, and check every creation result.

Handle swap interval through SDL2's swap-interval API. Implement fullscreen and display selection through SDL2 window APIs. Preserve the original logical game resolution and scaling behavior; test high-DPI drawable size separately from window size. Ensure resize, focus, minimize, restore, and display-change events do not invalidate the GL context or game state.

### 4. Port event and input handling

Translate the event loop to SDL2 event types. Handle window events explicitly, including resize, focus changes, minimize/restore, and close/quit. Review keycode versus scancode use; preserve original game controls and key repeat semantics. SDL2 text input is delivered through text-input events, so replace any dependence on SDL 1.2's Unicode field with an explicit decision about whether text entry is needed.

Update mouse coordinate mapping for window scaling and high-DPI output. Migrate joystick enumeration and event IDs to SDL2's joystick/game-controller model, and verify hot-plug behavior. Preserve input ordering and polling cadence where the engine relies on them.

### 5. Port surfaces and drawing without changing the renderer

SDL2 keeps surfaces but changes video presentation. Identify whether the desktop path draws into software surfaces before uploading/drawing through OpenGL. Replace SDL 1.2 screen-surface assumptions with an SDL2 window/context presentation path; preserve pixel format, pitch, palette/colorkey/alpha semantics, and locking behavior for off-screen surfaces.

For GL modes, retain the existing OpenGL renderer. For System mode, retain CPU rasterization and present through a dedicated streaming texture/SDL_Renderer or a window-surface backend. Select and validate one System presentation strategy; never use SDL_Renderer and direct GL on the same window. Verify SDL_gfx calls against SDL2_gfx equivalents and preserve their rasterization results.

### 6. Port audio and add-ons

- Link SDL2_mixer and SDL2_ttf (and SDL2_gfx if required) instead of SDL 1.2 variants.
- Audit `Mix_QuerySpecEx` compatibility shim against the SDL2_mixer API and the engine's expected ABI.
- SDL2 audio callbacks must fully initialize every byte of the output buffer on every callback, including underrun paths; explicitly write silence where needed.
- Re-check audio format, channel count, callback size, music/sound loading, pause/resume, and shutdown behavior.
- Confirm that the required codecs are built into or deployed with the selected SDL2_mixer package.

### 7. Lifecycle and error handling

Use SDL2's subsystem initialization and teardown consistently. Make partial initialization failures unwind safely, destroy the GL context before the window, close audio and input devices, and call SDL_Quit once. Revisit `src/sdlmain.c`, error dialogs, and startup logging so the Windows GUI and console builds retain their intended behavior.

### 8. Validation and rollout

Run the SDL1 and SDL2 builds against the same game data and configuration. Compare `mugen.log` and engine output for title screen, quick VS match, AI-vs-AI, character select demo, options/menu navigation, storyboards, `-nosound`, `-nomusic`, and missing-character error handling.

Also manually verify windowed/fullscreen transitions, resizing, focus loss/restore, multiple display behavior, keyboard repeat/text input, mouse mapping, joystick hot-plug, audio underrun/resume, and shutdown. Use screenshots or frame captures to compare rendering, and compare audio output where practical. Exercise debug and optimized builds on Windows and Linux; keep Android as a separate regression target.

Only make SDL2 the default after both build systems pass and the behavior comparison is clean or every known difference is documented. Then remove SDL1 dependencies in a separate cleanup change.

## Main migration mappings

| SDL 1.2 pattern | SDL2 direction |
| --- | --- |
| `SDL_SetVideoMode` | `SDL_CreateWindow` + `SDL_GL_CreateContext` for this OpenGL renderer |
| `SDL_GL_SwapBuffers` | `SDL_GL_SwapWindow(window)` |
| `SDL_GL_SWAP_CONTROL` attribute | `SDL_GL_SetSwapInterval` |
| `SDL_Flip` / `SDL_UpdateRect` | Present through the existing OpenGL path; use `SDL_RenderPresent` only if a future backend deliberately adopts SDL_Renderer |
| `SDL_VIDEORESIZE` | `SDL_WINDOWEVENT_RESIZED` / `SDL_WINDOWEVENT_SIZE_CHANGED` |
| `SDL_Keysym.unicode` / `SDL_EnableUNICODE` | `SDL_TEXTINPUT` where text input is required |
| Joystick device index assumptions | SDL2 joystick instance IDs and device add/remove events |
| SDL 1.2 add-on libraries | SDL2-specific add-on builds and link names |

## SDL2 migration references

- [SDL 1.2 to SDL2 Migration Guide](https://wiki.libsdl.org/SDL2/MigrationGuide)
- [SDL2 video API](https://wiki.libsdl.org/SDL2/CategoryVideo)
- [SDL2 OpenGL support](https://wiki.libsdl.org/SDL2/CategorySDLVideo)
- Repository Android port: `port/android/` (SDL2-based; consult as implementation evidence)


## Source validation and implementation detail (2026-11-01)

This revision is based on direct inspection of the current sdl2 branch: AGENTS.md, Makefile, CMakeLists.txt, src/{gfx,font,video,keyboard,events,pads,sound,sdlmixercompat,bgm,bgmplugins,bgmthread,stream,mugen}.{c,h} where applicable, and Android's android_gfx.c. This is a source review, not proof that an SDL2 desktop build works. No engine build or runtime tests were performed for this documentation change. Read the source again before implementation because the branch is active.

AGENTS.md requires preserving reconstructed behavior, address annotations, and floating-point rules. Its initial x86-only statement conflicts with its later Linux instructions and the current Makefile; keep Windows i686 as the reference and validate existing Linux targets independently. SDL2 dependencies are explicitly within this migration's requested scope. Preserve the current SDL1 build and platform ports during rollout.

### Corrected conclusions

1. System/software rendering is real: src/video.c creates software/video surfaces and src/gfx.c blits, updates, and flips them. A GL-only migration would remove supported behavior.
2. SDL_gfx is not being used here as a general drawing renderer. The inspected gfx.c and font.c call zoomSurface with smooth=0. SDL2_gfx's surface rotozoom API is relevant; its SDL_Renderer primitive API is not a reason to rewrite this engine.
3. Android already contains SDL2-specific handling in shared source. Separate SDL version checks from Android lifecycle checks; desktop SDL2 must not inherit mobile-only omissions.
4. Android's android_gfx.c claim that there is no SDL2_gfx port is incorrect: upstream publishes SDL2_rotozoom.c. Its replacement must be treated as an unvalidated alternative, not a pixel-equivalent implementation.
5. Music has two paths: SDL_mixer music in bgmplugins.c and a ring-buffer custom music hook in bgmthread.c. Both must be tested.
6. The Makefile has evolved since the earlier review: platform-aware toolchain/library checks are present. Do not carry old review findings forward without checking current code.

### File and work map

| Files | Observed integration | Required work |
|---|---|---|
| src/video.c, src/gfx.c, src/softrender.c, src/glrender.c | Software surfaces plus GL presentation | Separate window ownership, software framebuffer and GL context; preserve renderer selection |
| src/font.c, src/font.h | TTF font handles, measurement, solid/blended rendering, surface scaling | Use SDL2_ttf headers; validate metrics, alpha and surface ownership |
| src/gfx.c, src/font.c | zoomSurface, externally declared | Link SDL2_gfx rotozoom or adopt a proven equivalent; eliminate conflicting declarations |
| src/sound.c, src/sdlmixercompat.c | Channel effects, WAV conversion, custom QuerySpecEx | Handle obtained audio format, chunk-size contract and ownership explicitly |
| src/bgmplugins.c | SDL_mixer music, completion hook, seeking | Port RWops ownership and callback lifecycle; validate MOD/MIDI behavior |
| src/bgmthread.c | Mix_HookMusic, ring buffer, SDL_LockAudio | Validate callback byte length and synchronization with the chosen mixer version |
| src/stream.c | Custom RWops and Android widened adapters | Supply SDL2 size callback, bounds checks and correct seek/read/write signatures (verified gap: the SDL2 branch of `Stream_ToRWops` never sets `rw->size`) |
| src/events.c, src/keyboard.c, src/pads.c | Legacy key arrays and joystick indexing | Translate SDL2 keys and instance IDs; preserve saved controls |
| src/sdlmain.c, src/mugen.h, cmake/MugenSdk.cmake, Makefile | Legacy startup/header/link assumptions | Isolate SDL major version; avoid SDK SDL1 headers and Elecbyte SDL1 DLL imports |

### SDL_gfx: choose surface scaling deliberately

Recommended first implementation: use SDL2_gfx's zoomSurface from SDL2_rotozoom.h for SDL2 builds. Keep a narrow engine wrapper if needed so dependency details do not spread. Confirm the exact exported signature and Windows calling convention against the selected headers; do not retain handwritten extern declarations alongside incompatible headers. (2026-11-01: the x86devkit does **not** ship SDL2_gfx — no `SDL2_rotozoom.h`, `libSDL2_gfx.a` or `SDL2_gfx.pc` — so this choice means building ferzkopp's SDL2_gfx from upstream or adopting the Android wrapper. The 1.2 side's `SDL_rotozoom.h` in the kit does declare `zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)`, matching the handwritten `extern`s in `src/font.c:17` and `src/gfx.c:24`.)

Inventory every zoomSurface use, including platform-conditional fallbacks. In font.c, scaled text is blitted and then freed. In gfx.c the inspected additional scaling use is a Vita fallback. Keep platform conditions intact rather than adding a desktop dependency solely because a Vita-only call exists.

Before accepting SDL2_gfx, compare it with the original SDL_gfx on:
- indexed 8-bit surfaces with nontrivial palettes and transparent index;
- 16-bit inputs that the library may convert, and 32-bit RGBA inputs;
- fractional scale factors, odd dimensions, 1-pixel sizes, enlargement and reduction;
- color keys, per-surface alpha, per-pixel alpha, and clip rectangles;
- output dimensions, palette, pitch, pixel format and actual pixel content.

Do not assume output always retains the source format or that dimensions are simply truncated. Inspect upstream zoomSurfaceSize/zoomSurface behavior and compare against the old dependency.

Android's implementation currently truncates dimensions, rejects nonpositive factors, uses SDL_BlitScaled, copies palettes/color keys, but does not explicitly restore alpha modulation/blend mode on the destination. Scaling through a blitter can blend during transformation rather than merely copy source pixels, and indexed formats need specific support checks. These are review risks, not runtime failures proven here. Reuse only after differential images pass. If choosing a small local implementation instead of SDL2_gfx, document supported inputs and ownership and justify it with those comparisons.

### SDL_ttf: preserve layout and compositing

Observed calls in font.c include TTF_OpenFont, TTF_CloseFont, TTF_FontAscent, TTF_SizeUTF8, TTF_RenderUTF8_Solid and TTF_RenderUTF8_Blended. Font handles are already opaque in font.h.

Work items:
1. Link SDL2_ttf and compile all consumers against its headers. Locate initialization/TTF_Quit ownership and make failure cleanup symmetric.
2. Set SDL_Color alpha explicitly for SDL2, normally 255 for the existing opaque foreground intent. Review the Android conditional around color initialization; do not copy SDL1 padding assumptions.
3. Retain UTF-8 and existing newline/space handling. Keep font size, baseline/ascent adjustment, scale and rounding order.
4. Test solid text's indexed/color-key output and blended text's alpha output through both Surface_ToRGBA32/GL upload and direct software blits. Review video.c's Android alpha/blend save/restore path for reuse under an SDL-version condition.
5. Compare TTF_SizeUTF8 metrics and rendered placement before considering optimizations. SDL2_ttf/FreeType versions and optional shaping features can change glyph pixels and widths. Pin the actual dependency versions and record any accepted differences.
6. Keep bitmap/SFF fonts working independently. Do not introduce an atlas/cache refactor in the initial migration.

Acceptance: title/menu/console text and scaled TrueType text retain baseline, clipping, spacing, alignment and transparency in both rendering modes. Include ASCII, multibyte UTF-8, spaces, empty strings, missing glyphs, solid/blended modes, and failed font loads. Free every successful rendered surface exactly once; do not silently change documented original error quirks.

### SDL_mixer: effects, streamed music and RWops

Effects in sound.c:
- Mix_OpenAudio requests AUDIO_S16LSB, a configured rate/channel count and g_sndChunkSize.
- Mix_QuerySpecEx updates the engine's audio format.
- Mix_AllocateChannels, Mix_PlayChannelTimed, Mix_Volume, Mix_SetPanning, Mix_Playing and Mix_HaltChannel implement effect slots.
- WAV input uses Mix_LoadWAV_RW; the custom resample path uses Mix_QuickLoad_RAW and application-managed buffers.

Preserve slot/channel mapping, reserved channels, percentage-to-volume rounding, panning and looping. Do not confuse output channel count with allocated effect channels. Trace chunk->allocated and buffer cleanup around Mix_QuickLoad_RAW/Mix_FreeChunk to prevent leaks or double frees.

QuerySpecEx is an Elecbyte extension, not a stock SDL2_mixer API. Compile the local adapter for every SDL2 target, including CMake, instead of importing the original DLL symbol. Mix_QuerySpec reports rate/format/channels but not callback size. The current shim echoes g_sndChunkSize; that does not establish the device's actual callback length. Audit all chunk-size consumers, drive callback processing from its len argument, and label the request separately from observed callback lengths. Do not claim logs alone prove callback-size equivalence. Check the query return value and all unsupported output formats.

Music in bgmplugins.c:
- The inspected sdlmix path refuses WAV/MP3/OGG and routes tracker/MIDI through SDL_mixer; inspect the surrounding plugin dispatch before changing codec routing.
- Reuse SDL2's Mix_LoadMUS_RW(rw, freesrc) signature, but trace success and failure lifetimes. Streaming music may retain the RWops until Mix_FreeMusic. Ensure the underlying Stream outlives it and close/free exactly once.
- Preserve existing repeat, pause/resume, seek, rewind, volume and finished-hook behavior, including intentional quirks.
- Pin codec backends and MIDI configuration/SoundFont requirements. A linked library alone does not guarantee MOD/MIDI playback is usable.

Custom streaming in bgmthread.c:
- Mix_HookMusic replaces normal mixer music while effect chunks still mix afterward. Preserve transitions between custom BGM and sdlmix; unregister hooks before destroying callback state.
- Inspect BgmThread_MixCallback and fully fill every supplied byte, including short reads, empty ring buffer, end-of-stream and pause. Use silence appropriate to the obtained audio format.
- Retain a single mixer-owned output device. Do not open a second SDL device just to obtain a device ID.
- Review SDL_LockAudio usage against the selected SDL2_mixer implementation; choose its documented compatible synchronization or an engine-owned nonblocking handoff. Avoid recursive audio locks, blocking I/O, allocation and thread joins inside callbacks.
- Order shutdown so workers stop, hooks detach under safe synchronization, callback users quiesce, and buffers/music/device resources are freed safely.

RWops in stream.c:
- Existing SDL2 adapters widen callback signatures; audit narrowing conversions to the engine's int-based stream API.
- Implement rw->size where required, reject offsets/count multiplication beyond representable limits, and return item counts, not bytes, from read/write.
- Distinguish SDL_FreeRW (wrapper only) from SDL_RWclose (close callback plus wrapper ownership). Validate EOF, seek failure and truncated streams.

Acceptance: overlap effects and each BGM path; exercise volume/pan/mute, repeated play/stop, loop boundaries, seek, pause/resume, focus changes, malformed WAVs, missing codecs and clean shutdown. Verify both SDL resampling and custom libresample modes. Capture PCM or loopback output in addition to logs; logs cannot demonstrate audio equivalence.

### Video/input blockers and implementation decisions

Keep 640x480 as the handheld acceptance mode; do not silently change its physical output size. Preserve other existing desktop configuration choices. Separate logical size, window size and GL drawable size.

For System mode, first retain CPU surfaces and choose a window-surface update backend for minimum implementation complexity, or a streaming texture backend if scaling is required. If selecting textures, keep the CPU framebuffer owned by the engine, respect pitch during upload and use explicit scaling/filtering. GL and System windows must have separate creation/presentation rules; recreate resources deliberately when switching modes.

Do not put SDL2 objects inside reconstructed structures at assumed SDL1 offsets. Audit sizeof(SDL_Surface), SDL_Rect field widths, surface flag tests and direct SDL_PixelFormat field accesses. Keep SDL_Window/SDL_GLContext in platform-owned state. (Verified: `SDL_Rect` widens from `Sint16 x,y; Uint16 w,h` to `int x,y,w,h`; `SDL_Surface.pitch` widens from `Uint16` to `int` and `offset`/`hwdata` are replaced by `userdata`/`list_blitmap`; `SDL_PixelFormat` gains a `Uint32 format` field and loses `Rshift/Gshift/Bshift/Ashift`. `src/` currently uses none of the removed fields — details in the header-validation section.)

events.c currently sends SDL key symbols directly to Keyboard_HandleKey. SDL2 special keycodes are large values; feeding them into legacy key arrays can corrupt memory. Use explicit SDL2-to-engine legacy key translation for events, bindings and focus-state refresh. keyboard.c's Android scancode adaptation is useful evidence but does not prove all special keys are translated. Implement text-input handling for the desktop console rather than Android's zero-Unicode placeholder. Preserve independent key repeat policy for gameplay and text entry.

pads.c must map SDL_JoystickID to engine slots, not use the instance ID as an array index. Preserve existing axis/hat/button numbering and four-pad limits. Keep the joystick API initially; GameController mapping is optional later work.

### Build integration and staged acceptance gates

Proposed options (not implemented): SDL_BACKEND=1|2 for Make and MUGEN_SDL_MAJOR=1|2 for CMake. Include the backend in output/object directories so flag changes cannot reuse incompatible objects.

SDL2 pkg-config candidates: sdl2 SDL2_ttf SDL2_mixer SDL2_gfx plus existing zlib/libpng. Verify names for the selected distribution. Preserve static transitive dependency discovery on MinGW and shared dependencies on Linux. Remove SDL1 headers/import definitions from the SDL2 include/link path without losing unrelated SDK headers. Keep a separate SDL2 startup/manifest path and ensure Elecbyte.MUGEN.libs is not required by it. Record exact package versions and verify produced DLL dependencies. No SDL_image addition is needed solely for this work: current PNG handling already uses libpng. (2026-11-01 kit check: `sdl2`, `SDL2_ttf` and `SDL2_mixer` are present; **SDL2_gfx is not shipped by the kit** — see the header-validation section.)

| Stage | Deliverable | Exit gate |
|---|---|---|
| 0 | Frozen SDL1 reference commit, compiler/dependency versions and same game-data/config fixtures | Baseline logs, screenshots and audio captured |
| 1 | Backend build switch, headers, entry point, structs/RWops adapters | SDL1 still builds; SDL2 compiles/links without SDL1 ABI imports |
| 2 | System and GL windows/presentation | Both render title/select/fight at 640x480; screenshots and fullscreen/focus checks pass |
| 3 | Key/text/joystick adaptation | Existing bindings work; no out-of-range key/instance indexing |
| 4 | zoomSurface and TrueType integration | Scaling fixtures and font layout/compositing comparisons pass |
| 5 | Effects, sdlmix music and custom stream hook | Audible/PCM checks and lifetime/underrun tests pass |
| 6 | Windows MSVC/MinGW, Linux and existing port regression checks | Debug/release comparisons pass; differences documented |
| 7 | Make SDL2 default in a separate change | Review accepted; SDL1 rollback instructions retained |

Use tools/compare_logs.py for normalized logs with matched durations/configuration. Run -nosound scenarios for deterministic engine comparison, then separate sound-enabled scenarios. Include -nomusic, absent audio device, bad character/font/music input, repeated restart, and mode switching. Use available sanitizers for adapter bounds/lifetime checks without treating sanitizer timing as a performance reference. Put artifacts under project build/ per AGENTS.md. Do not delete user game data or existing logs.

Release blockers: corrupted key indexing, unsafe callback teardown, mixed SDL1/SDL2 ABI, broken System renderer, altered palette/alpha behavior, or unapproved font/audio differences. Retain the SDL1 build and a baseline commit for rollback. Effort depends on how much Android adaptation can be generalized; no delivery date is promised before stages 1–2 resolve these risks.

### Additional primary references

- [SDL2_gfx surface rotozoom implementation/API](https://www.ferzkopp.net/Software/SDL2_gfx/Docs/html/_s_d_l2__rotozoom_8c.html)
- [SDL2_ttf solid UTF-8 rendering](https://wiki.libsdl.org/SDL2_ttf/TTF_RenderUTF8_Solid)
- [SDL2_ttf blended UTF-8 rendering](https://wiki.libsdl.org/SDL2_ttf/TTF_RenderUTF8_Blended)
- [SDL2_mixer audio format query](https://wiki.libsdl.org/SDL2_mixer/Mix_QuerySpec)
- [SDL2_mixer custom music hook](https://wiki.libsdl.org/SDL2_mixer/Mix_HookMusic)
- [SDL2_mixer device opening and format changes](https://wiki.libsdl.org/SDL2_mixer/Mix_OpenAudioDevice)

## Header and kit validation against x86devkit (2026-11-01)

Everything below was verified directly against `C:\x86devkit\include\SDL2`,
`C:\x86devkit\include\SDL`, `C:\x86devkit\lib` and the kit's `.pc` files. It
replaces the generic "verify the API" notes above with concrete facts. Re-check
against your distribution if you build with something other than this kit.

### Kit contents

| Component | SDL 1.2 side | SDL2 side |
| --- | --- | --- |
| SDL | 1.2.15 (`SDL_version.h`) | 2.32.10 |
| Mixer | `SDL_mixer.h`, `libSDL_mixer.a` | `SDL_mixer.h` 2.8.2, `libSDL2_mixer.a` |
| TTF | `SDL_ttf.h`, `libSDL_ttf.a` | `SDL_ttf.h` 2.24.0, `libSDL2_ttf.a` |
| GFX | `SDL_rotozoom.h` (`zoomSurface`), `libSDL_gfx.a` | **absent** — no `SDL2_rotozoom.h`, `libSDL2_gfx.a`, `SDL2_gfx.pc` |
| Image | — | `SDL_image.h` 2.8.x, `libSDL2_image.a` (present; not needed — PNG already goes through libpng) |
| SDLmain | — | `libSDL2main.a` |
| pkg-config | `sdl.pc`, `SDL_gfx.pc`, `SDL_mixer.pc`, `SDL_ttf.pc` | `sdl2.pc`, `SDL2_mixer.pc`, `SDL2_ttf.pc`, `SDL2_image.pc` |

### Verified API differences, header against header

| Area | SDL 1.2 (kit) | SDL2 (kit) | Consequence for this engine |
| --- | --- | --- | --- |
| `SDL_Rect` | `Sint16 x,y; Uint16 w,h` | `int x,y,w,h` | Existing 1.2-era casts (e.g. `dst.y = (Sint16)(int)...` in `src/font.c`) stay range-safe for 640×480, but rects now hold full ints; do not rely on 16-bit truncation |
| `SDL_Surface` | `Uint16 pitch`; `offset`, `hwdata` | `int pitch`; `userdata`, `list_blitmap`; no `offset`/`hwdata` | Layout differs; code touching only `w/h/pitch/pixels/format` ports unchanged |
| `SDL_PixelFormat` | `palette` first, then BPP; has `Rshift/Gshift/Bshift/Ashift` | `Uint32 format` first; shifts replaced by `padding` | `src/` uses neither the shifts nor `SDL_SetColors` today (grepped), so only the new `format` field is visible through `format->BitsPerPixel` etc. |
| `SDL_Keysym` | `Uint8 scancode; SDLKey sym; mod; Uint16 unicode` | `SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; Uint32 unused` — **no unicode** | `src/events.c` already branches on `ANDROID_PORT` for the missing unicode (passing 0); desktop SDL2 must feed the console real `SDL_TEXTINPUT` instead. `SDL_EnableUNICODE` does not exist in SDL2 |
| Key repeat | `SDL_EnableKeyRepeat` | removed | Called in `src/console.c` and `src/keyboard.c`; both need SDL2 guards (console.c already notes this for Android) |
| `SDL_INIT_NOPARACHUTE` | real flag | compatibility define, ignored | `src/sdlmain.c`'s `SDL_Init(SDL_INIT_NOPARACHUTE)` compiles as-is |
| Joystick events | `which` is a `Uint8` device index; no timestamp | `which` is `SDL_JoystickID` (`Sint32`) instance id; `timestamp` added | `src/pads.c` indexes pads directly with `ev->jaxis.which` etc. — correct for 1.2, must be remapped for 2.0 exactly as the plan warns |
| `SDL_RWops` | int-based `read/write/seek`; no `size` | `Sint64` `read/write/seek`; new `size` callback | `src/stream.c` already carries widened 64-bit adapters under `ANDROID_PORT`; that branch never sets `rw->size`, and SDL2_mixer music seeking needs it — confirmed gap |
| `Mix_OpenAudio` | 4 args | still present with the same 4-arg signature (equivalent to `Mix_OpenAudioDevice(..., NULL, 0)`); `Mix_OpenAudioDevice` adds `device` and `allowed_changes` | `src/sound.c`'s request (AUDIO_S16LSB, `g_sndChunkSize`) ports directly; use `allowed_changes=0` or re-read with `Mix_QuerySpec` |
| `Mix_QuerySpec` | `(freq, format, channels)` | identical 3 args | `src/sdlmixercompat.c`'s `Mix_QuerySpecEx` keeps echoing `g_sndChunkSize`; still a request, not the observed callback size |
| `Mix_LoadMUS_RW` | `(rw)` — never takes ownership | `(rw, freesrc)` | `src/bgmplugins.c` already has the `ANDROID_PORT` conditional adding `freesrc`; generalize that pattern |
| `Mix_HookMusic` / `Mix_HookMusicFinished` | `(mix_func, arg)` / `(cb)` | same shape (`Mix_MixCallback` typedef) | `src/bgmthread.c` and `src/bgmplugins.c` port without signature changes |
| `Mix_LoadWAV_RW`, `Mix_QuickLoad_RAW`, `Mix_PlayChannelTimed`, `Mix_SetPanning`, `Mix_Volume` | 1.2 kit | identical signatures in 2.8.2 | `src/sound.c` ports as-is (its `Mix_LoadWAV_RW(rw, 0)` already passes `freesrc`) |
| SDL_ttf (`TTF_OpenFont`, `TTF_CloseFont`, `TTF_FontAscent`, `TTF_SizeUTF8`, `TTF_RenderUTF8_Solid`, `TTF_RenderUTF8_Blended`) | 1.2 kit | identical names and signatures in 2.24.0 | `src/font.c` compiles against SDL2_ttf unchanged; the diff risk is FreeType's pixels/metrics, not the API |
| Window/video | `SDL_SetVideoMode`, `SDL_WM_SetCaption`, `SDL_WM_ToggleFullScreen`, `SDL_Flip`, `SDL_UpdateRect` | `SDL_CreateWindow` (+`SDL_WINDOW_OPENGL`), `SDL_SetWindowTitle`, `SDL_SetWindowFullscreen`, `SDL_GL_SwapWindow`, `SDL_GetWindowSurface`/`SDL_UpdateWindowSurface` | All SDL2 names verified present in `SDL_video.h`; `SDL_WINDOWEVENT_RESIZED`/`SDL_WINDOWEVENT_SIZE_CHANGED` (in `SDL_video.h` for 2.32) and `SDL_GL_SetSwapInterval(int)` exist |
| Text input | unicode rides `SDL_KEYDOWN` | `SDL_TEXTINPUT`, text capped at `SDL_TEXTINPUTEVENT_TEXT_SIZE` (32) | Console text entry must be separated from gameplay key handling, as the plan requires |

### Corrections to earlier statements in this document

- **Build integration:** the verified in-kit SDL2 packages are `sdl2`,
  `SDL2_ttf`, `SDL2_mixer` (plus unused `SDL2_image`). **No SDL2_gfx in the kit.**
- **SDL_gfx choice:** upstream ferzkopp SDL2_gfx exists and ships `zoomSurface`
  in `SDL2_rotozoom.h` with the same signature as the 1.2 `SDL_rotozoom.h`
  verified above, so the Android wrapper's "no maintained SDL2 port" comment is
  inaccurate — but since the kit does not ship it, the practical options are
  (a) build SDL2_gfx from upstream, or (b) adopt the Android `SDL_BlitScaled`
  wrapper after the differential tests this plan already demands. Either way
  the handwritten `extern ... zoomSurface` declarations in `src/font.c:17` and
  `src/gfx.c:24` must be replaced by the real header.
- **zoomSurface call sites:** confirmed as `src/font.c:292` (smooth=0) and
  `src/gfx.c:330` (smooth=0, inside the `MUGEN_VITA_GL`-only `Vita_ScaleToVideo`
  fallback) — those are the only two.
- **`src/stream.c` RWops adapters:** they exist (under `ANDROID_PORT`) but do
  not set the new `size` callback; now a verified gap, not a maybe.

## Stage 0-1 implemented (SDL_BACKEND=2)

The build switch and the compatibility layer are in. `make SDL_BACKEND=2` builds
`build/mingw-sdl2/mugen-static-sdl2.exe` from the same `src/`, and
`make SDL_BACKEND=1` (the default) still builds the reference executable.

### How it is wired

- `SDL_BACKEND=1|2` (Makefile) selects the packages (`sdl2 SDL2_mixer SDL2_ttf`
  instead of `sdl SDL_gfx SDL_mixer SDL_ttf`), adds `-DMUGEN_SDL2
  -DSDL_MAIN_HANDLED`, and suffixes the output directory and executable name, so
  the two backends can never share objects.
- `-Dmain=SDL_main` and `-lSDL2main` are filtered out of the pkg-config output:
  this build keeps the reconstructed `WinMain` in `src/sdlmain.c` and calls
  `main()` itself, which is exactly what `SDL_MAIN_HANDLED` means. A second
  WinMain from SDL2main would not link.
- `src/mugen.h` maps `ANDROID_PORT || MUGEN_SDL2` to `MUGEN_SDL2_API`. The 13
  sites that had to change for SDL2's semantics (no `SDL_Keysym.unicode`,
  no `SDL_EnableKeyRepeat`, no `SDL_JoystickOpened`, RWops signature widening,
  no `SDL_JA_*`/`SDL_SetAlpha`, opaque `TTF_Font`/`Mix_Music` tags) now test
  that macro; Android-only behaviour keeps testing `ANDROID_PORT`.
- `src/sdl2compat.c` + `src/sdl2compat.h` supply the removed entry points
  (`SDL_SetVideoMode`, `SDL_Flip`, `SDL_UpdateRect`, `SDL_WM_SetCaption`,
  `SDL_WM_ToggleFullScreen`, `SDL_GL_SwapBuffers`, `SDL_GL_SWAP_CONTROL`,
  `SDL_AudioDriverName`, `SDL_GetKeyState`, `SDL_EnableKeyRepeat`,
  `SDL_EnableUNICODE`, `SDL_GetWMInfo`'s one field, `zoomSurface`) so the
  reconstructed call sites keep their shape and their address comments.
- One window, one backend at a time: a GL window + context for
  `RENDER_OPENGL`/`RENDER_OPENGL_SCREEN`, and for `RENDER_SYSTEM` a plain
  software surface (the engine writes `surface->pixels` and never locks) that is
  uploaded to a streaming texture and drawn with `SDL_RenderCopy`. Never both,
  because `SDL_Renderer` and a GL context cannot share a window.

### Input: one bug worth recording

SDL2 renumbered the non-printable `SDLK_*` values to `scancode |
SDLK_SCANCODE_MASK` (`SDLK_UP` is 1073741906, not 273). The original's built-in
key tables in `system.c` hold SDL 1.2 *keycodes*, and the engine indexes
`g_keyState`/`g_keyMap` by keycode, so the first SDL2 build wrote far outside
`g_inputState` and died silently during `Input_Remap` — no crash dialog, no log
line, just a truncated `mugen.log`. `Input_EngineKey()` now converts a table
entry through `SDL_GetScancodeFromKey` before it becomes an engine key, which is
the same number SDL 1.2 held there. `gameloop.c` and `video.c` had the same
pattern for the held-modifier tests and are translated the same way.

### Validation so far

`mugen.log` for `-nosound -p1 kfm -p2 kfm` from the SDL2 build is **identical**
to the SDL 1.2 build's after numeric normalisation (`tools/compare_logs.py`,
exit 0, 102 fragments); the log ends at `Match loop init`, i.e. the match is
running, not merely loading. The SDL 1.2 executable is unchanged by this work
(`build/mingw/mugen-static.exe` is byte-identical with `sdl2compat.c` excluded
from its build).

### Still open

- Stage 2+: the shim presents System mode through a streaming texture; the
  GL renderer path has not been exercised on this desktop build yet
  (the test environment reports `render mode 2`, i.e. System).
- `zoomSurface` is the `SDL_BlitScaled` substitute, still awaiting the
  differential image test listed under "SDL_gfx" above.
- Joystick hot-plug, text input on the desktop console, and audio (the test
  runs use `-nosound`) are unverified.
- The Linux target has not been built with `SDL_BACKEND=2`.
