# Review — Android port (`port/android`)

A review of the Android port as committed. Written by the author of the port, so
it is written adversarially: the goal is to find what is wrong, not to justify
what exists.

**Verdict (updated): every finding that could be fixed from the source has been
fixed and verified. Two remain open by nature: the port has never been run on a
device (H2), and touch controls are unimplemented (L10).**

The build system and the statically-identifiable defects are in good shape. The
runtime is still unproven, and this document should not be read as claiming
otherwise.

---

## 1. What is sound, and why

These decisions hold up under scrutiny and should not be revisited casually.

### 1.1 The SDL 1.2 shim instead of editing the engine

The engine's video contract is three SDL 1.2 calls (`SDL_SetVideoMode`,
`SDL_Flip`, `SDL_UpdateRect`). SDL2 removed all of them. The port reimplements
them in `android_video.c` and force-includes the compat header with
`-include` (`CMakeLists.txt:154`), so **every SDL call site in `src/` compiles
unchanged**.

This is the right call because `src/video.c` and `src/gfx.c` are faithful
reconstructions of a specific binary, and AGENTS.md is explicit that fidelity is
the product. Threading `#ifdef ANDROID_PORT` through every one of ~100 SDL call
sites would have buried the reconstruction under port code.

### 1.2 Keeping the software renderer

`glrender.c`/`gldetect.c` are excluded rather than ported to GLES. The engine's
GL path is desktop GL 1.x/2.1 with a glad loader generated for
`wglGetProcAddress`/`glXGetProcAddressARB`; Android has neither. The software
renderer is the same path the Vita port ships, so nothing is lost.

### 1.3 Assets extracted before `main`, with a `chdir`

`android_main.c` extracts to external storage and `chdir`s there, so every
relative path in `mugen.cfg` resolves as on desktop. This avoided touching any
path handling in the engine.

The generation stamp is written **only after every asset succeeds**, so an
interrupted first run retries instead of leaving a half-populated tree that fails
later somewhere confusing. That is the right failure mode.

### 1.4 `Mix_LoadMUS_RW(rw, 0)`

SDL_mixer 2 added `freesrc`. The caller at `bgmplugins.c:926` closes the RWops
itself for types 1 and 2, so `0` reproduces SDL_mixer 1.2 semantics exactly.
This is the only *semantic* change in `src/`, and it is correct.

### 1.5 Refusing 8bpp

`bpp_to_masks` rejects 8bpp with a real error instead of falling back. On the
Vita a silently-wrong format produced a black screen and cost a long
investigation; failing loudly is better.

### 1.6 Cross-platform guard discipline

All 13 `src/` changes are inside `#ifdef ANDROID_PORT`. Verified: MSVC release
build 0 errors, Vita VPK rebuilds clean (see §5).

---

## 2. High severity

### H1. `g_keyState` filled with scancodes but indexed by keycodes — **FIXED**

**Where:** `android_video.c` (`Android_GetKeyState`) → `android_main.h`
(`SDL_GetKeyState` macro) → `keyboard.c` `Keyboard_SyncState` → `system.c:1592`.

`Keyboard_SyncState` did:

```c
const Uint8 *state = SDL_GetKeyState(&count);
for (i = 0; i < count; i++) g_keyState[i] = state[i];
```

It assumes `state` is indexed by **SDL 1.2 keycode** (`SDLK_*`). SDL2's
`SDL_GetKeyboardState` returns an array indexed by **scancode**, and the two
numbering schemes are unrelated:

| | value |
|---|---|
| `SDLK_a` | `97` (`'a'`) |
| `SDL_SCANCODE_A` | `4` |
| `SDLK_LCTRL` | `0x400000E0` = `scancode \| SDLK_SCANCODE_MASK` |

So the loop copied scancode data into a keycode-indexed array, clobbering the
values `Keyboard_HandleKey` writes correctly from `ev.key.keysym.sym`, and
corrupting `g_inputState` (`events.c:438,446`) and the Ctrl-held debug reads
(`gameloop.c:147,185`). It fires on `System_OnFocusGained`, i.e. **every app
resume** — not an edge case.

**Fix applied** (`src/keyboard.c`, `ANDROID_PORT` branch): the copy is replaced by
an explicit, bounded translation through the same conversion SDL 1.2 performed
internally:

```c
for (i = 0; i < INPUT_KEY_COUNT && i < (int)(sizeof(g_keyState)); i++) {
    SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)i);
    g_keyState[i] = (sc != SDL_SCANCODE_UNKNOWN) ? state[sc] : 0;
}
```

`count` is deliberately unused on this path: SDL2's array length is a *scancode*
count, which says nothing about how many keycodes are set.

**Verified** by disassembling the built `libmain.so` — the call is real, not
dead-code-eliminated:

```
bl 0x151f60 <Android_GetKeyState@plt>
bl 0x151f70 <SDL_GetScancodeFromKey@plt>
```

### H2. Nothing has ever been executed on a device

No Android hardware was available; the build host is Windows with an NDK and no
emulator was used. Everything below is **static** verification only:

- startup, `SDL_main` hand-off, asset extraction
- rendering and the present path
- audio (SDL2_mixer 2.6.3 is a *different major version* from the 1.2 the engine
  was written against)
- gamepad/keyboard input
- the H1 bug's actual severity

`libmain.so` links and exports `SDL_main`, which SDLActivity looks up by name
(`SDLActivity.java:258`, `getMainFunction() → "SDL_main"`). Confirmed present:

```
000000000010e288 T SDL_main
```

That removes the most likely instant-crash-at-startup cause, and nothing more.
**A port that has never run should not be described as working**, and the README
says so.

### H3. The release APK was signed with the debug keystore — **FIXED**

`app/build.gradle` unconditionally used `signingConfig signingConfigs.debug`. Fine
for sideloading, not fine as a release: that is the *public* debug key, it cannot
ship on Play, and two machines produce different signing identities, breaking
updates.

**Fix applied:** a real `signingConfigs.release` block, fed from environment
variables the same way the reference script does it:

| Variable | Purpose |
|---|---|
| `ANDROID_KEYSTORE` | path to a `.jks`/`.keystore` |
| `ANDROID_KEYSTORE_PASS` | store password |
| `ANDROID_KEY_ALIAS` | key alias (default `androidkey`) |
| `ANDROID_KEY_PASS` | key password (defaults to the store password) |

If they are absent the config falls back to **debug signing** and Gradle prints a
warning naming exactly which variables are missing — so a local build still works
and nobody mistakes the output for a distributable one. `build.sh` passes the
variables through when set.

---

## 3. Medium severity

### M1. `Android_SetVideoMode` ignored every call after the first — **FIXED**

**Where:** `android_video.c:129-158`.

The window and renderer are created once (`if (s_window == NULL)`), and
`SDL_RenderSetLogicalSize` is called once with the *first* size. If the engine
later requests a different size — a resolution change, or a second
`Video_SetMode` — then:

- the texture and software surface are correctly recreated at the new size,
- but the window keeps its original size and the logical size stays stale,

so the result is stretched incorrectly with no error. The Vita port learned this
class of bug expensively.

**Fix applied** (`android_video.c`): the created size is tracked in
`s_created_w`/`s_created_h`; when a later `SetVideoMode` asks for a different
size, `SDL_SetWindowSize` and `SDL_RenderSetLogicalSize` are both called with the
new dimensions before the window/rendering setup continues.

### M2. Every `SDL_UpdateRect` presented; no dirty flag — **FIXED**

**Where:** `android_video.c` (`present()`), reached from `gfx.c:476`.

Each call does a **full-surface upload** + `RenderClear` + `RenderCopy` +
`RenderPresent`. At 640×480×16 that is ~600 KB of upload per call. The engine
calls `Gfx_Present` once per frame in the normal path, so this is acceptable —
but nothing enforces it, and if a draw path calls `SDL_UpdateRect` more than once
per frame the cost multiplies silently.

**Fix applied:** an `s_dirty` flag. `Android_Flip`/`Android_UpdateRect` mark the
surface dirty and present only if it is still set; `present()` clears it.
`Android_PresentFrame()` is called from `Frame_UpdateSkip`'s caller in `frame.c`
to mark each frame boundary, so repeated present requests inside one frame
collapse into a single upload + `SDL_RenderPresent`.

Still unmeasured — the frame-statistics equivalent was not ported (see §M3 and the
`frame.c` note), so the *effect* of this fix is unquantified.

### M3. `MUGEN_VITA_GL` compiled Vita code into the Android build — **FIXED**

The macro reuse is defensible (it is the engine's existing "not desktop GL"
guard; auditing ~45 sites for a second macro is worse). But it is not free, and
the consequences are live in the tree:

| Site | Effect on Android |
|---|---|
| `gfx.c:301` | `Vita_ScaleToVideo` is **compiled**, including its cached column map |
| `gfx.c:433-468` | the scaling branch is **live**; it only skips because `surface->w == g_videoSurface->w` |
| `gfx.c:482-505` | the "flip if double-buffered" block runs; inert because the surface never carries `SDL_DOUBLEBUF` |
| `Vita_PerfAddScale/Flip` | mapped to no-ops in `android_main.h` |
| `symbols.h:175` | `g_glewExtBlendLogicOp` → `MugenVita_GLExtBlendLogicOp`, which exists **only** because `android_glstub.c` defines it |

So the Android build contains Vita-specific code that is inert only because of
surface-size and flag coincidences. If `Android_SetVideoMode` were ever changed to
open a *panel-sized* surface (a reasonable future optimisation), `Vita_ScaleToVideo`
would start running on Android — carrying Vita pixel-format assumptions.

**Fix applied** (`src/gfx.c`): the driver-specific blocks are now
`#if defined(MUGEN_VITA_GL) && !defined(ANDROID_PORT)`, and the System-renderer
present path has an explicit `ANDROID_PORT` branch that blits and presents
without CPU scaling. `MUGEN_VITA_GL` keeps its "software renderer" meaning, and
`Vita_ScaleToVideo` is no longer compiled into the Android build at all — so a
future size change cannot accidentally activate it.

### M4. A fake JNI export — **FIXED**

**Where:** `android_main.c` (end of file):

```c
__attribute__((visibility("default"))) void Java_org_libsdl_app_SDLActivity_nativeInit(void) {}
```

`SDLActivity` has no `nativeInit` — verified by grepping `SDLActivity.java`. This
is dead code whose comment claims it exists because "SDLActivity.java locates this
symbol by name", which is false (it locates `SDL_main`). It is exported into the
APK (`T Java_org_libsdl_app_SDLActivity_nativeInit`), so it also ships.

**Fix applied:** deleted. The comment in its place records why no JNI export is
needed (SDLActivity `dlopen`s the library and `dlsym`s `SDL_main`).
Verified: `llvm-nm -D` no longer lists
`Java_org_libsdl_app_SDLActivity_nativeInit`, and `T SDL_main` is still present.

### M5. SDL's own argv was discarded — **FIXED**

`android_main.c` builds `argv` from a `cmdline` file and ignores the arguments
SDL2 parsed from the Java side (`SDLActivity` passes an `arguments` string). The
`cmdline` file is a genuinely useful idea on a device with no shell, but
silently dropping the JNI path means an intent-supplied argument appears to do
nothing.

**Fix applied:** SDL's parsed `argv` is now the base, and the `cmdline` file is
appended to it. An intent-supplied argument therefore reaches the engine, and the
device-local override still works. The precedence (SDL first, file second) is
noted in the code and in the README.

---

## 4. Low severity / cleanups

| # | Where | Issue |
|---|---|---|
| L1 | `android_main.h` | The `SDL_SetVideoMode`/`SDL_UpdateRect`/`SDL_Flip` macro block was defined **twice**. **FIXED** — the duplicate is gone, the full block remains. |
| L2 | `android_video.c` | `SDL_BlendMode blend;` was declared, never initialised, "used" only as `(void)blend;` inside an `if` whose body did nothing. **FIXED** — replaced with a real `SDL_SetSurfaceBlendMode(..., SDL_BLENDMODE_NONE)` call and a comment explaining why opaque is correct. |
| L3 | `android_video.c` | `Android_SetPreferredSize` was public API that **nothing called**. **FIXED** — removed, along with its declaration and the stale reference in the header comment. |
| L4 | `android_main.c:34` | `ASSET_STAMP` was `"ux0_mugen_assets_v1.stamp"` — `ux0:` is a **PS Vita** mount point, left over from the other port. **FIXED** → `"mugen_assets_v1.stamp"`. |
| L5 | `gradle.properties:11` | `org.gradle.java.home=C:/Android/jdk-17` hardcoded a machine-specific path in a **tracked** file. **FIXED** — removed from `gradle.properties`. See the trap in §4.1 below; the naive replacement did not work. |
| L6 | `build.sh` | `--help` was broken: `sed -n '2,25p'` ran past the comment header and printed shell code (`set -euo pipefail`). **FIXED** — replaced with an `awk` one-liner that stops at the first non-comment line. Verified: prints exactly the usage header. |
| L7 | `android_main.c` (`extract_asset`) | `rel[strlen(rel) - 1]` was UB for an empty string. **FIXED** — explicit empty check first. |
| L8 | `android_video.c` | `Android_VideoQuit` was **never called**. **FIXED** — called from the engine's successful-exit path in `src/main.c` (inside `ANDROID_PORT`). |
| L9 | `build.sh:44`, `tools/build_sdl2_companions.sh:29` | Default `SDL2_DEPS_DIR` points into **another repository** (`ikemen-develop-update/build/...`). Practical here, but it makes the port non-self-contained and silently depends on a sibling checkout. **Open, documented.** Overridable via `SDL2_DEPS_DIR`; missing it produces a clear failure, not a confusing one. |
| L10 | — | **No touch controls.** Input requires a gamepad or a keyboard. For a phone this is the biggest functional gap, and it is not a bug — it is unimplemented. **Open.** |

### 4.1 A trap found while fixing L5

Removing the hardcoded JDK path from `gradle.properties` **broke the build**, and
not in an obvious way. On this machine `JAVA_HOME` is already set to a **Java 8**
JDK, so the launcher JVM started as Java 8 and Gradle failed with:

```
No matching variant of com.android.tools.build:gradle:8.1.1 was found
... compatible with Java 8, the consumer needed a component compatible with Java 11
```

That message names neither Java versions in a useful way nor `JAVA_HOME`; it reads
like a dependency-repository problem. Worse, both plausible fixes are wrong:

- writing `org.gradle.java.home` into `local.properties` does **not** help — that
  property is read after the launcher JVM has already started;
- passing `-Dorg.gradle.java.home=...` on the `gradlew` command line does **not**
  help either, for the same reason.

What works is `JAVA_HOME` itself, set before `gradlew` runs. `build.sh` now:

- prefers `MUGEN_JAVA_HOME`, then `/c/Android/jdk-17`, then the existing
  `JAVA_HOME` (so an explicitly-set Java 8 no longer wins by default),
- prints the version actually in use, and
- fails with a direct message if the version is 1.8/1.7/11.

An earlier `if [ -z "${JAVA_HOME:-}" ]` guard was the reason this went unnoticed:
`JAVA_HOME` was *not* empty, it was simply wrong, so the guard never fired.

---

## 5. What was actually verified (and how)

Every claim in §1 and the findings above was checked against the tree, not
assumed.

| Check | Result |
|---|---|
| Engine cross-compiles for arm64-v8a | `libmain.so`, `ELF 64-bit LSB shared object, ARM aarch64, for Android 21` |
| Port entry points are real code | `T SDL_main`, `T Mugen_Main`, `T Android_SetVideoMode`, `T zoomSurface`, `T Sound_Init` in `llvm-nm -D` |
| SDLActivity's lookup will resolve | `getMainFunction() → "SDL_main"`; symbol present |
| APK packages 4 native libs + assets | 6,777,748 bytes; `libSDL2/mixer/ttf/main.so`; 96 `assets/` entries; `manifest.txt` 95 lines |
| Build reproducible from clean | `rm -rf app/build app/.cxx local.properties && ./build.sh` → same APK size |
| MSVC unaffected | 0 `error C` |
| Vita build unaffected | 0 errors; `mugen.vpk` rebuilt |
| `zoomSurface(NULL)` cannot crash callers | both call sites (`gfx.c:327`, `font.c:289`) test for NULL — **no finding** |
| 8bpp cannot silently misrender | rejected in `bpp_to_masks` |

**Could not verify:** anything requiring a device (§H2).

---

## 6. Recommended order of work

1. **Run it.** `adb install -r`, then `adb logcat -s mugen`. If `mugen.log`
   appears under `/sdcard/Android/data/org.mugen_unleashed.mugen/files/mugen`,
   the extraction and entry-point path are proven.
2. **Fix H1** (`g_keyState` scancode/keycode). Decide the fix from observed
   behaviour: if only Ctrl-debug and focus-resync are affected, making
   `Keyboard_SyncState` a no-op under `ANDROID_PORT` is the honest minimal change;
   if `g_inputState` visibly misbehaves, translate properly.
3. **Fix M1** (size change) before anyone edits `GameWidth`/`GameHeight` and
   concludes the renderer is broken.
4. **Fix M3** (widen the `MUGEN_VITA_GL` guards in `gfx.c`) before touching
   surface sizes for performance.
5. Clean up the remaining §4 items — L8 and L9. (L1-L7 are already done; see the
   table and §4.1.)
6. Touch controls (§L10), then frame statistics so any further performance work
   is driven by a number instead of a guess.

---

## 7. Closing note

The build system in this port is in good shape: reproducible, and loud when
something is missing (SDL2, the companion libraries, `data/mugen.cfg`, a wrong
JDK). It does not silently ship a wrong configuration — the lesson from the Vita
port's two silently-lost `mugen.cfg` patches.

Every defect that could be identified and settled **from the source** has now been
fixed, each one written in a form that can be checked rather than trusted: H1 was
confirmed in the disassembly, the cleanup items by re-grepping and re-building,
and all three platforms (Android, MSVC, Vita) were rebuilt afterwards.

What remains is not code hygiene — it is observation. The port has never been
executed, and that is a materially weaker claim than "it works". The fixes above
address defects I could prove statically; a device run will find others, including
plausibly some of my own making, since SDL_mixer 2.6.3 is a major version newer
than the 1.2 the engine was written against.

---

## 8. Change log of this review

| Finding | Status | Verified by |
|---|---|---|
| H1 scancode/keycode | **Fixed** | disassembly shows `bl SDL_GetScancodeFromKey` |
| H2 never run on a device | **Open** | — (no hardware) |
| H3 debug-signed release | **Fixed** | real `signingConfigs.release` from env; warning fires when absent (observed in output) |
| M1 size change ignored | **Fixed** | rebuilt; code path present |
| M2 present per call | **Fixed** | `Android_PresentFrame` in `llvm-nm` |
| M3 Vita code compiled in | **Fixed** | `gfx.c` guards widened; rebuild clean |
| M4 fake JNI export | **Fixed** | absent from `llvm-nm -D` |
| M5 argv discarded | **Fixed** | code path in `android_main.c` |
| L1–L8 | **Fixed** | re-grep + rebuild |
| L9 sibling-repo default | Open, documented | — |
| L10 no touch controls | **Open** | unimplemented feature |

Post-fix verification, all clean: Android `libmain.so` builds for
`aarch64-none-linux-android21` and the APK packages (6,776,874 bytes, 4 native
libraries, 96 asset entries); MSVC release build 0 errors; Vita VPK rebuilds with
0 errors.

One incidental fix: `build.sh` was writing `ndk.dir` into `local.properties`,
which AGP 8 deprecates (`CXX5106`, one warning per Gradle invocation). It now
relies on the `ndkVersion` already pinned in `app/build.gradle`; the warning count
is 0.
