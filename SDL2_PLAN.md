# SDL 1.2 to SDL2 Migration Plan

## Goal

Port the desktop M.U.G.E.N reconstruction from SDL 1.2 to SDL2 while preserving the original engine's observable behavior, especially input timing, audio output, rendering, and startup/shutdown behavior. Keep the current SDL 1.2 build usable until the SDL2 path passes the comparison gates below.

The repository already has an Android port that uses SDL2. Treat it as a source of project-specific experience and reusable code, but do not assume its mobile video/input backend is interchangeable with the desktop engine.

## Findings and constraints

- The desktop engine is a C reconstruction of the original 32-bit Windows executable. It uses SDL 1.2 and SDL add-ons, with MSVC/CMake and MinGW/Make build paths.
- `src/sdlmain.c` contains the reconstructed SDL 1.2 `SDLmain` behavior. SDL2's `SDL_main` is optional and has a different role; review this file and the entry point before deciding whether to retain, replace, or bypass it.
- Existing Linux, Windows, and Android ports mean the migration should be isolated behind a small platform layer or compile-time backend selection. Avoid broad changes to engine logic and reconstructed data structures.
- Rendering uses legacy OpenGL (GL 2.1 compatibility behavior per the Makefile), so use SDL2 windows plus an OpenGL context and retain the existing GL renderer. Do not switch to SDL_Renderer as part of the initial migration.
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

Prefer the existing OpenGL renderer for presentation. Do not mix SDL_Renderer presentation with direct OpenGL rendering in the first port. Verify SDL_gfx calls against SDL2_gfx equivalents and preserve their rasterization results.

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
