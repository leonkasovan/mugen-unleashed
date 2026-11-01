# mugen-unleashed on Android

A port of this engine to Android, built with the NDK and packaged as an APK.

The first target is **arm64-v8a** (64-bit ARM), API 21+. It runs the engine's
**software (System) renderer**; the OpenGL renderer is not built (see
[Why the software renderer](#why-the-software-renderer)).

```
port/android/
  build.sh                  one command → installable APK
  app/
    build.gradle            app module
    src/main/
      AndroidManifest.xml
      java/org/libsdl/app/  SDL2's Java activity (from the SDL 2.32.10 release)
      jni/src/              the Android back end (this port's C code)
        CMakeLists.txt      the engine + third-party build
        android_main.c      entry point, asset extraction, chdir
        android_main.h      the SDL 1.2 compatibility surface
        android_video.c     SDL 1.2 video subset over SDL2
        android_gfx.c       zoomSurface (the one SDL_gfx call the engine makes)
        android_glstub.c    stubs for the excluded GL renderer
      res/                  label, theme, launcher icon
  tools/
    build_sdl2_companions.sh   cross-compile SDL2_mixer + SDL2_ttf
```

---

## Building

### Prerequisites

Already present on this machine; listed so the build is reproducible elsewhere.

| Component | Version / path |
|---|---|
| Android SDK | `C:/Android/SDK` (platform 34, build-tools 30.0.3) |
| Android NDK | r27d — `C:/Android/SDK/ndk/r27d` |
| JDK | 17 — `C:/Android/jdk-17` (Gradle 8 requires it) |
| libSDL2.so | cross-compiled for arm64-v8a, staged in `build/android-deps/` |

The Android Gradle Plugin pins `ndkVersion "27.1.12297006"` and Gradle is 8.1.1
(the wrapper is included). Both match the reference build so the same cached
toolchain is reused.

### One-time: cross-compile the SDL2 companion libraries

The engine calls `SDL_mixer` (all audio) and `SDL_ttf` (fonts), and this
repository does **not** vendor them — `third_party/SDL_mixer-1.2.12` and
`third_party/SDL_ttf-2.0.11` are placeholder directories, because the desktop
builds take them from the system through pkg-config (the `Makefile`'s
`STATIC_PKGS`). Android has no such packages, so they are built:

```sh
port/android/tools/build_sdl2_companions.sh
```

This builds SDL2_ttf 2.20.2 (+ FreeType 2.12.1) and SDL2_mixer 2.6.3 into
`build/android-deps/arm64-v8a/`. OGG is decoded by the vendored stb_vorbis and
MP3 by dr_mp3, so no libogg/libvorbis cross-build is needed — the dependency set
stays at what the engine actually plays (WAV, OGG, MP3).

### Build the APK

```sh
port/android/build.sh                 # release APK with game data
port/android/build.sh --no-assets     # engine only — fast iteration on code
port/android/build.sh --debug         # debuggable APK
port/android/build.sh --clean         # wipe staging first
```

Output: **`build/android/mugen-unleashed-arm64-v8a.apk`**

Game data comes from `install/` (a real M.U.G.E.N 1.1b1 tree) unless
`GAME_DATA_DIR` says otherwise. The original's own executables and
`Elecbyte.MUGEN.libs/` are excluded — this build is self-contained.

### Signing

A release build is **debug-signed** unless you provide a real key. It installs and
runs, but it is not distributable and will not update over a build made on another
machine. Gradle prints a warning that says so.

To sign properly, set:

```sh
export ANDROID_KEYSTORE=/path/to/release.jks
export ANDROID_KEYSTORE_PASS=...
export ANDROID_KEY_ALIAS=androidkey     # default
export ANDROID_KEY_PASS=...             # defaults to the store password
port/android/build.sh
```

### Install

```sh
C:/Android/SDK/platform-tools/adb.exe install -r build/android/mugen-unleashed-arm64-v8a.apk
```

Watch the log with:

```sh
C:/Android/SDK/platform-tools/adb.exe logcat -s mugen
```

---

## How the port works

### SDL 1.2 over SDL2

The engine reproduces M.U.G.E.N 1.1, whose whole video contract is one
persistent software surface:

```c
surface = SDL_SetVideoMode(w, h, bpp, flags);   /* once   */
...write surface->pixels every frame...         /* always */
SDL_Flip(surface);                              /* present */
```

SDL2 removed all three calls. Rather than edit `src/video.c` and `src/gfx.c` —
faithful reconstructions of the original binary that must stay faithful for the
Windows and Linux builds — the removed subset is reimplemented in
`android_video.c`, and `android_main.h` is force-included into every translation
unit (`-include`) so the engine's call sites compile unchanged.

`SDL_SetVideoMode` becomes "create a window and a streaming texture, hand back a
software surface matching the requested format"; `SDL_Flip`/`SDL_UpdateRect`
upload that surface and present it.

Three details this gets right, each a failure mode seen on the Vita port:

- The surface is **always** a software surface. The engine writes
  `surface->pixels` directly and **never calls `SDL_LockSurface`** — the original
  never did either. A hardware surface's pixels cannot be touched unlocked.
- Presenting is an explicit **texture update + `SDL_RenderCopy`**, because SDL2
  has no "flip a software surface" call. Scaling happens in the GPU, which is
  free compared with CPU scaling.
- 8bpp is refused outright rather than silently producing a black screen.

### Entry point and asset extraction

`SDL_main.h` renames the engine's `main()` to `SDL_main`, and SDL2's Java
activity calls it. `android_main.c` runs first and:

1. extracts the APK's assets to external storage on first launch,
2. `chdir`s there — so **every relative path in `mugen.cfg` resolves exactly as
   it does on desktop**, and no engine path handling had to change,
3. appends the optional `cmdline` file to the arguments SDL parsed from the
   intent — so flags like `-nosound` can be set on a device with no shell
   (precedence: SDL's arguments first, then the file),
4. calls the engine.

`src/main.c`'s `main()` is renamed `Mugen_Main` for this target. That module is
compiled for its *helpers* as well (`Camera_GetX/Y`, `Rng_Seed`, `Random_Next`,
`Version_Parse`, `Screen_IsSpecial`) which the rest of the engine calls
unconditionally — excluding it leaves them undefined at link time.

Assets are enumerated from `assets/manifest.txt`, generated by Gradle at build
time because SDL2's asset API cannot list a directory. Extraction completes only
after every file succeeds, so an interrupted first run retries rather than
leaving a half-populated tree that fails somewhere confusing later.

Runtime location:

```
/sdcard/Android/data/org.mugen_unleashed.mugen/files/mugen
```

That is the app-specific external directory — writable without runtime
permissions, and reachable from a file manager, which matters because users
expect to drop new characters in.

### Why the software renderer

`glrender.c` and `gldetect.c` are excluded from the build. Two independent
reasons:

- **No desktop GL.** The engine's GL path is GL 1.x/2.1 fixed-function with glad
  generated for `wglGetProcAddress`/`glXGetProcAddressARB`. Android has GLES, and
  no such loader.
- **No GLEW/glad here.** `mugen.h` includes `<glad.h>` for the desktop builds;
  on Android that is skipped entirely (`#ifdef ANDROID_PORT`).

The engine's software renderer is a complete, faithful path — it is what the
Vita port ships — so nothing is lost.

`android_glstub.c` exists because the GL renderer's *symbols* are still
referenced from modules that **are** compiled: `gfx.c` selects a renderer through
the `g_gfx*` function-pointer tables, `sprite.c`/`palette.c` handle `GlTex`
objects, `video.c`'s teardown names `Gl_Deinit`. Eighteen undefined symbols
otherwise. The stubs return failure and do nothing; each is reachable only from a
`RENDER_OPENGL` branch, and `config.c` forces `RENDER_MODE_SYSTEM` on Android, so
none can run. If one ever does, it fails visibly.

### The `MUGEN_VITA_GL` macro

`MUGEN_VITA_GL` is the engine's existing "non-desktop build" guard and is
reused for Android. It already selects the System rendering path and the guarded
platform fixes; inventing a second macro would mean auditing ~45 occurrences for
the Android case. **The name is historical** — it means "not the desktop GL
build", not "Vita".

Where Vita-specific code sits behind it, Android gets its own branch with its own
comment. The places that differ are deliberate:

| Area | Android behaviour |
|---|---|
| Error dialog (`error.c`) | `__android_log_print` → `adb logcat -s mugen` |
| Window events (`events.c`) | SDL2's `SDL_APP_*` lifecycle events instead of `SDL_ACTIVEEVENT` |
| Key repeat (`console.c`) | no-op — SDL2 made it a platform setting |
| Unicode key callback | passes 0; SDL2 moved text to `SDL_TextInputEvent` |
| Window resize | absent — no resizable window |
| Frame profiling (`frame.c`) | **not implemented** (the Vita's `Vita_FrameLog` has no counterpart yet) |

`libresample`'s public header uses MSVC `__declspec` attributes, so the build
passes `-fdeclspec` (the same allowance the Makefile makes for MinGW).

### Changes to `src/`

Every one is guarded by `ANDROID_PORT`; Windows (MSVC) and Linux builds are
untouched, and the Vita build is unaffected.

| File | Change |
|---|---|
| `mugen.h` | skip `<glad.h>` on Android |
| `main.c` | `main()` → `Mugen_Main` (entry point supplied by the port) |
| `error.c` | logcat error reporting |
| `events.c` | SDL2 lifecycle events, unicode, no `SDL_VIDEORESIZE` |
| `console.c` | `SDL_EnableKeyRepeat` no-op |
| `gfx.c` | GL branches compiled out |
| `video.c` | no `Gl_Init`/`Gl_Deinit`; `Surface_ToRGBA32` uses SDL2 alpha API |
| `font.c` | `SDL_Color` has no `unused` field |
| `frame.c` | no `Vita_FrameLog` |
| `stream.c` | SDL2-signature `SDL_RWops` adapters (`Sint64`/`size_t`) |
| `pads.c` | `SDL_JoystickOpened` removed → check the handle |
| `globals.c` | explicit deinit-slot casts clang requires |
| `bgmplugins.c` | `Mix_LoadMUS_RW(rw, 0)` — SDL_mixer 2 added `freesrc` |

The `Mix_LoadMUS_RW` case is the only **semantic** difference: SDL_mixer 2 takes
ownership of the RWops when `freesrc` is non-zero, and the caller here closes it
itself, so `0` reproduces SDL_mixer 1.2's behaviour exactly.

---

## Status

**Verified**

- The engine cross-compiles for arm64-v8a: `libmain.so`, 1.4 MB, `ELF 64-bit LSB
  shared object, ARM aarch64, for Android 21`.
- The APK builds and packages correctly: 6.8 MB, with `libSDL2.so`,
  `libSDL2_mixer.so`, `libSDL2_ttf.so`, `libmain.so` and 95 asset files plus
  `manifest.txt`.
- The build is reproducible end to end from `build.sh`.

Every defect identified statically in [`REVIEW.md`](REVIEW.md) has been fixed —
including the scancode/keycode input bug, resolution changes, present batching,
Vita-only code leaking into the Android build, and release signing. MSVC and the
Vita build remain clean after the changes.

**Fixed in this pass**

| ID | Issue |
|---|---|
| H1 | `Keyboard_SyncState` copied scancode-indexed data into a keycode-indexed array — broke input on every app resume. Now translated via `SDL_GetScancodeFromKey`. |
| H3 | Release APK was always debug-signed. Now reads a real keystore from the environment. |
| M1 | A second `SetVideoMode` with a different size was ignored. |
| M2 | Every present call uploaded the whole surface; now batched once per frame. |
| M3 | Vita scaling code was compiled into the Android build. |
| M4 | A dead fake JNI export shipped in the library. |
| M5 | SDL's parsed `argv` was discarded. |
| L1–L8 | Duplicate macro block, dead variables, Vita leftover names, broken `--help`, hardcoded JDK path, and more. |

**Not yet verified**

- **Nothing has been run on a device.** No Android hardware was available in this
  environment, so startup, rendering, audio, touch/gamepad input and the asset
  extraction path are all untested at runtime. This is the single most important
  caveat in this document.
- Frame rate is unmeasured.
- Touch controls are **not implemented** — input requires a gamepad or keyboard
  attached to the device. This is the most obvious missing feature for a phone.

**Known gaps / next steps**

1. Run it on a device and read `adb logcat -s mugen`.
2. Touch controls: an on-screen pad would map to `SDL_JOY*` events (the engine's
   input is joystick- and keyboard-driven), which `port/android/app/src/main/
   jni/src` can synthesise — the same approach the Vita port used for its
   keyboard/pad bridge.
3. Frame statistics (the `Vita_FrameLog` equivalent) to decide whether the
   software renderer needs the same scaling work it got on the Vita.
4. `armeabi-v7a`: add it to `abiFilters` and re-run
   `build_sdl2_companions.sh` with `ANDROID_ABI=armeabi-v7a`.

---

## Reference

Built following two existing Android ports on this machine, which are the proven
pipeline here:

- `C:/Projects/ikemen-develop-update/tools/generate_android_via_native.sh` —
  SDK/NDK/JDK setup, SDL2 cross-compilation, jniLibs staging, `manifest.txt`
  generation, Gradle invocation and APK signing.
- `C:/Projects/ikemen-droid` — the Gradle project layout, `SDLActivity` Java side
  and `AndroidManifest.xml` shape.

This port reuses their toolchain and conventions but does not download or install
anything: those components are already present, and `build.sh` fails with a clear
message rather than installing silently.
