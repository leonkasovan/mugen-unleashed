# mugen-unleashed on Android

Android now uses **native SDL2 APIs**, **SDL2_ttf** and **SDL2_mixer**.
There is no Android SDL1 API shim or SDL1 library dependency. The desktop
`src/sdl2compat.c/.h` remains for desktop builds and is excluded from Android.
`android_main.h`, `android_gfx.c` and `android_glstub.c` have been removed.

Current target: **arm64-v8a**, minimum API 21, compile SDK 34, target SDK 29.
The engine's **System/software renderer** draws CPU pixels; a native SDL2
window, renderer and streaming texture present them. SDL2 may use GLES
internally. MUGEN's desktop `OpenGL`/`OpenGLScreen` sprite renderers remain
unavailable on Android; selecting either logs a fallback to System before
renderer tables or sprites are initialized.

The implementation and host tests are complete for the native API boundary.
**An NDK/APK rebuild and device qualification have not been run for this change.**
Asset-extraction device recovery tests, audible output, Android graphics/lifecycle,
performance and additional codecs remain qualification work.
See [REVIEW.md](REVIEW.md) for evidence and [PLAN.md](PLAN.md) for acceptance gates.

## Build entry points and layout

Android uses Gradle and the native CMake project. The root Makefile remains
for Windows/Linux; do not invoke it to build this Android port.

```text
port/android/
  build.sh                         setup/check tools, stage inputs, invoke Gradle
  README.md / REVIEW.md / PLAN.md
  app/build.gradle                 ABI, SDK/NDK/CMake pins and signing
  app/src/main/
    AndroidManifest.xml
    java/org/libsdl/app/            SDL2 Java integration (2.32.10)
    jni/src/
      CMakeLists.txt               libmain.so + engine/vendor sources
      android_main.c               SDL_main entry and APK asset extraction
      android_platform.h           native SDL2 engine operations (no aliases)
      android_video.c              window/renderer/streaming texture ownership
      android_dependencies.c       SDL2/ttf/mixer header version checks
      check_sdl2.cmake              reject SDL1 DT_NEEDED entries after linking
  tools/
    bootstrap_toolchains.sh        missing-tool installation
    toolchain_metadata.py          vendor metadata, checksums, extraction
    build_sdl2_companions.sh        SDL2_ttf / SDL2_mixer cross-build
  tests/test_build.py               offline bootstrap/orchestration fixtures
  tests/test_icons.py               launcher icons come from res/icons
  tests/CMakeLists.txt              host build of the actual Android engine paths
  tests/test_native_sdl2.c           native SDL2 runtime tests
```

## 1. Toolchain setup

A Bash environment, curl and Python 3.8+ are host prerequisites. Git and the
existing companion builder's unzip/tar tools are also needed for the optional
library source-build workflow below. On Windows,
use MSYS2/Git Bash with those programs on PATH; set `MUGEN_PYTHON` if the Python
executable is called `python` or is at a custom path. The script does not run
sudo, install OS packages or modify shell profiles.

Supported bootstrap host selection: Linux x86_64, Windows x64 under
MSYS/Cygwin, macOS x86_64/arm64. The pinned NDK uses darwin-x86_64 host tools
on macOS; Apple Silicon may need Rosetta. Native Windows/macOS execution is
not qualified by the Linux mock tests.

From the repository root:

```sh
bash port/android/build.sh --setup
source build/android/toolchains/env.sh
```

`--setup` installs missing tool components and exits before checking SDL
libraries or packaging assets. Review SDK licenses when sdkmanager asks;
the script does not accept them automatically. Rerun after a failed download,
declined license or interrupted SDK package installation.

| Component | Selection |
|---|---|
| JDK | Runnable JDK 17 with javac; missing JDK downloaded from official Temurin/Adoptium metadata |
| Android command-line tools | Version 19.0 bootstrap when needed; existing suitable command-line tools may be reused |
| SDK platform | `platforms;android-34` |
| SDK build tools | `build-tools;33.0.1`, explicitly matching the project's AGP 8.1 configuration |
| NDK | `ndk;27.1.12297006`; validate actual `source.properties` |
| CMake / Ninja | `cmake;3.22.1` from the SDK |
| Device tools | `platform-tools` |
| Gradle / AGP | Wrapper Gradle 8.1.1 and AGP 8.1.1; wrapper downloads Gradle on first real build |

The NDK version is the numeric Gradle pin. The previous instructions called it
r27d while defaulting to an independently named directory; that did not prove
the selected revision. No global NDK/JDK upgrade is performed here.

Downloaded JDK archives are checked against the vendor's SHA256 metadata.
Android command-line-tool archives use the checksum algorithm/value published
in Google's repository XML. Archives are verified before staged extraction.
SDK package downloads are delegated to sdkmanager. The wrapper retains its
existing Gradle distribution mechanism.

Default generated locations:

| Location | Contents |
|---|---|
| `build/android/toolchains/sdk` | Missing SDK/NDK/CMake components |
| `build/android/toolchains/jdk-17` | Downloaded JDK, if no usable existing one was selected |
| `build/android/toolchains/downloads` | Vendor metadata and verified bootstrap archives |
| `build/android/toolchains/tmp` | Bootstrap extraction and temporary files |
| `build/android/toolchains/env.sh` | Shell exports for subsequent native-library commands |
| `build/android/gradle-user-home` | Default Gradle distribution/dependency cache |
| `build/android/toolchains/android-user-home` | Default Android/JVM user state |

Existing explicitly supplied SDK/JDK/cache paths are respected. SDK package
installation can add the missing pinned side-by-side packages to that chosen
SDK. A custom incomplete NDK path is rejected instead of being overwritten.

### Overrides

| Variable | Meaning |
|---|---|
| `ANDROID_SDK_ROOT` | First choice for SDK location |
| `ANDROID_HOME` / `ANDROID_HOME_DIR` | SDK fallbacks, in that order |
| `ANDROID_NDK_HOME` | Optional external NDK; must match the pinned revision, forwarded to Gradle's ndkPath |
| `MUGEN_JAVA_HOME` | Explicit JDK 17; an invalid explicit choice fails |
| `JAVA_HOME` | Reuse when it identifies a runnable JDK 17 |
| `MUGEN_ANDROID_TOOLS_DIR` | Override bootstrap/cache root; default `build/android/toolchains` |
| `MUGEN_PYTHON` | Python 3.8+ executable |
| `GRADLE_USER_HOME` / `ANDROID_USER_HOME` | Optional existing cache/state overrides |
| `SDL2_DEPS_DIR` | SDL2 prefix; default `build/android/sdl2/arm64-v8a` |
| `SDL2_COMPANION_DIR` | Mixer/TTF prefix; default `build/android/android-deps/arm64-v8a` |
| `GAME_DATA_DIR` | Game tree to copy; default repository `install/` |

The script normalizes its exported SDK roots so Gradle and sdkmanager use the
same SDK. An unsuitable inherited Java 8/11/21 is not used as the Gradle JVM;
select JDK 17 or request setup. A valid existing JDK does not get replaced.

## 2. Supply the native SDL2 libraries

Toolchain setup does **not** download or build SDL2, SDL2_mixer or SDL2_ttf.
Supply matching Android arm64-v8a/API-21-compatible libraries and headers.
The Java integration currently comes from SDL 2.32.10: use that SDL release
until both sides are deliberately updated together.

If existing prefixes are available:

```sh
export SDL2_DEPS_DIR=/path/to/android-sdl2-prefix
export SDL2_COMPANION_DIR=/path/to/android-mixer-ttf-prefix
```

SDL2's prefix must contain `include/SDL2/SDL.h`, `lib/libSDL2.so` and its CMake
package files when building the companions. The companion prefix needs its
matching headers plus `lib/libSDL2_mixer.so` and `lib/libSDL2_ttf.so`.

One source-build workflow, after tool setup, is:

```sh
source build/android/toolchains/env.sh
mkdir -p build/android/sources
git clone --depth 1 --branch release-2.32.10 \
  https://github.com/libsdl-org/SDL.git build/android/sources/SDL

export SDL2_DEPS_DIR="$PWD/build/android/sdl2/arm64-v8a"
cmake -S build/android/sources/SDL -B build/android/build-sdl2 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL2_DEPS_DIR" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
cmake --build build/android/build-sdl2
cmake --install build/android/build-sdl2

bash port/android/tools/build_sdl2_companions.sh
```

Do not repeat the clone into an existing directory; reuse the recorded checkout
or prepare a separate clean source directory.

The existing companion script selects SDL2_ttf 2.20.2 with FreeType 2.12.1 and
SDL2_mixer 2.6.3 with WAV/OGG/MP3-capable decoders. **That does not establish
engine BGM support:** the current engine route rejects those formats while
this mixer profile disables the tracker/MIDI formats it accepts. This remains
open in REVIEW.md. The companion source downloads/build itself were not rerun
in the documentation/bootstrap validation; record their hashes and dependency
closure when doing a real build.

## 3. Build and install

Prepare a copy of the game tree with `[Video] RenderMode=System`. The current
native config path does not reliably reject unsupported GL modes, and the build
script does not rewrite the cfg. Do not change game dimensions to work around
the known forced-Vita-panel bug; fix the native policy as described in PLAN.md.
Reference `install/` data is copied, not modified by the build script.

```sh
bash port/android/build.sh --check-tools
GAME_DATA_DIR=/path/to/game-tree bash port/android/build.sh
bash port/android/build.sh --debug --no-assets
bash port/android/build.sh --download-tools --debug --no-assets
```

`--no-assets` really clears the generated asset staging. It does not provide
game data at runtime: use it only for a previously provisioned data directory
or diagnostic builds. The first-run extraction/update limitations still apply.

### The launcher icon

The icon is not a hand-drawn resource either. `build.sh` runs
`tools/make_android_icons.sh` before Gradle, which scales `res/icons/1.ico` —
the same artwork the Windows and PS Vita builds use — into
`app/build/generated/res/mugen-icons/`:

| Resource | Sizes | Where it shows |
|---|---|---|
| `mipmap-<density>/ic_launcher.png` | 48/72/96/144/192 | the legacy icon (below Android 8) |
| `mipmap-<density>/ic_launcher_foreground.png` | 108/162/216/324/432 | the adaptive icon's foreground, on a 108dp canvas with the artwork inside the 72dp safe zone |

`build.gradle` adds that directory to the debug and release resource sets, which
override the committed copies in `app/src/main/res/mipmap-*` instead of colliding
with them; those copies are the same generation and are what a build without
Python (or a bare `gradlew assembleDebug`) shows. Replacing the artwork means
replacing `res/icons/`, and `python3 port/android/tests/test_icons.py` keeps the
two in step.
`--clean` removes generated app build/CMake output; it does not uninstall tools.

Output: **`build/android/mugen-android-arm64-v8a.apk`**.
Only arm64-v8a is currently supported by Gradle; another `ANDROID_ABI` is rejected
rather than producing a mislabeled arm64 APK.

After sourcing the tool environment, use the SDK's adb:

```sh
adb install -r build/android/mugen-android-arm64-v8a.apk
adb logcat -s mugen
```

For offline builds, first populate the selected Gradle distribution/dependency
cache online, then run:

```sh
bash port/android/build.sh --offline --no-assets
```

Without a complete cached Gradle 8.1.1 distribution this fails before invoking
Gradle. Offline mode runs that distribution directly, so the wrapper cannot
start a distribution download from a different cache key.
Gradle also needs cached plugin/Maven dependencies. `--offline` cannot be
combined with setup/download options. A normal online build may still download
Gradle or dependencies; the opt-in flags specifically control tool provisioning.

### Signing

Gradle selects release signing when `ANDROID_KEYSTORE` identifies an existing
keystore; otherwise the release variant falls back to debug signing and warns.
Provide the required key credentials for your distributable build:

```sh
export ANDROID_KEYSTORE=/path/to/release.jks
export ANDROID_KEYSTORE_PASS=...
export ANDROID_KEY_ALIAS=androidkey
export ANDROID_KEY_PASS=...
bash port/android/build.sh
```

The key password defaults to the store password, and the alias defaults to
`androidkey`. Missing/wrong credentials can still fail signing. Updating an
installed APK requires a compatible signing identity; debug keys from different
machines are not necessarily identical. The bootstrap does not create a release
key or print signing credentials.

## 4. Runtime and configuration

The APK extracts game data into the app-specific external-storage `mugen/`
directory, with internal-storage fallback, then starts `Mugen_Main` through
SDLActivity's `SDL_main` entry. The upstream B6 changes are retained: sibling
`.part` writes, size/FNV-1a verification, payload generation stamps and
preservation of user-edited `data/mugen.cfg`. Storage failures, interrupted
upgrades and user-data recovery still need device tests; see [REVIEW.md](REVIEW.md).

Use `RenderMode = System`. Android preserves the configured game resolution
(default 640x480), rather than requesting the Vita's 960x544 panel. `KeepAspect`
uses SDL2 logical sizing/letterboxing; disabling it fills the physical output.
The System rasterizer expects RGB565 and tightly packed rows. Android display
creation rejects unsupported depths and incompatible pitch instead of uploading
mismatched pixels. Offscreen indexed font surfaces remain supported. Text scaling uses the shared
`src/sdl2surface.c` engine operation; Android does not export `zoomSurface`.

### `data/mugen.cfg` is the Android one

`port/android/mugen.cfg` is the platform's config and `build.sh` installs it over
the staged copy, so the APK ships settings that suit a device rather than a
desktop's: `RenderMode = System` (the GL/GLES sprite renderer is not runnable on
Android yet; the software renderer presents through SDL2's `SDL_Renderer`), both
players' joysticks enabled, and the `[P1 Gamepad]` / `[P2 Gamepad]` groups.
`GAME_CFG=<path>` stages a different file.

The gamepad groups are **not** the `[P<n> Joystick]` device indices. Android
binds a pad through SDL's game-controller API, where A/B/X/Y, the d-pad, the
shoulders and Start mean the same thing on every controller, and maps them onto
this compact per-pad layout:

| code | control | | code | control |
|---|---|---|---|---|
| 1 | up | | 5 | A |
| 2 | down | | 6 | B |
| 3 | left | | 7 | C (RB) |
| 4 | right | | 8 | X |
| | | | 9 | Y |
| | | | 10 | Z (LB) |
| | | | 11 | Start |

The left stick steers with codes 1..4, the same as the d-pad, so either works.
**Select (Back) opens the in-match menu**: `Game_ReadInput` turns its pad code
into the engine's pause key, which is the same path the Pause key uses.

If `[P1 Gamepad]` / `[P2 Gamepad]` are absent, the built-in layout above is
used *and* both groups are appended to `data/mugen.cfg` at start-up, so the file
becomes editable. Configure buttons in the input menu, then choose
**Options → Save** to write the changes to these gamepad groups. Codes in the
file are local to each pad (1..11); player 2's runtime code 37 is saved as 5.
Cleared bindings are saved as 0. Raw joystick mappings are kept separately in
the [P<n> Joystick] groups. Save also creates missing gamepad groups or keys.
If a controller is connected or removed while assigning buttons, the pending
edit is cancelled; reopen the input menu for the new device.

Saved numeric key IDs remain compatible with `mugen.cfg`. These are engine
configuration identifiers, not SDL2 keycodes or scancodes. Native SDL2 events
are translated before array access; focus snapshots use the inverse mapping from the shared
`src/sdl2keys.c` tables.
Menus, debug bindings and Lua key diagnostics use the same engine IDs.
`SDL_TEXTINPUT` supplies console UTF-8 separately. Gameplay ignores OS repeat;
console editing accepts it. No SDL key constants are redefined.

Native SDL2 joystick events use instance IDs and runtime topology. Controller
insertion/removal preserves existing slots and releases removed-device inputs.
Android no longer installs Vita pad defaults or its synthetic-key bridge.
User P1/P2 bindings are retained; configure the actual Android controller in
the key configuration menu. Touch-only play is not implemented.

Android BGM uses one built-in SDL2_mixer backend for **WAV, OGG/Vorbis and MP3**.
Windows `[Music]` plugin DLL selections are replaced by that Android backend;
the desktop configuration file is not rewritten. Native SDL2 RWops are owned
by the mixer, with the underlying Stream kept alive until playback closes.
BGM commands retain full 64-bit pointers; synchronization uses native SDL2
mutexes, conditions and threads. Sound effects also use SDL2_mixer.
Tracker/MOD and MIDI are not enabled by the current companion builder and are
not advertised by the Android music route. Custom loop-point and decoder
behavior still need per-format device qualification.

Video resources are destroyed by `Video_Deinit` before final `SDL_Quit`.
Only the frame boundary uploads/presents; dirty marking does not present,
and skipped/background frames do not trigger duplicate presentation.
Renderer-reset events invalidate the texture and retain CPU pixels for upload.

## 5. Verification

### 5.1 Host fixtures and native tests

Bootstrap/orchestration fixtures:

```sh
bash -n port/android/build.sh port/android/tools/bootstrap_toolchains.sh port/android/tools/build_sdl2_companions.sh
python3 port/android/tests/test_build.py
python3 port/android/tests/test_icons.py     # launcher icons vs res/icons
```

Native Linux host tests require a C compiler, GNU-compatible linker,
CMake 3.22.1+, zlib headers and native
SDL2/SDL2_mixer/SDL2_ttf development packages. They compile **all** Android engine
and vendor translation units except the Android-only JNI/asset entry. They
link with no undefined symbols, no SDL compatibility implementation and no GL
stubs. Run from the repository root:

```sh
cmake -S port/android/tests -B build/android-native-tests -DNATIVE_SDL2_SANITIZE=ON
cmake --build build/android-native-tests -j4
ctest --test-dir build/android-native-tests --output-on-failure
```

CTest generates a WAV in its build directory, exercises the real BGM thread
and effects, then removes it. DejaVuSans is used when installed; otherwise
real TTF rendering is skipped and indexed/RGBA surface cases still run.
Additional codec/font fixtures can be passed explicitly:

```sh
cd build/android-native-tests
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./test_native_sdl2 /path/font.ttf /path/music.wav /path/music.ogg /path/music.mp3
```

The Android CMake build checks companion header major versions and scans
`libmain.so` plus imported SDL libraries for SDL1 `DT_NEEDED` entries. Compile
errors reject SDL1 function aliases and accidental desktop/Vita backend flags.
For release qualification inspect the **whole APK dependency closure**, not
only the directly imported libraries. A caller-supplied prebuilt must also
match arm64-v8a/API 21 and the pinned SDL Java/native integration.

### 5.2 What the host tests do not cover

The root Makefile remains for Windows/Linux. Host dummy-driver tests do not
qualify NDK compilation, the Android GPU, physical controllers, real sound
output or suspend/resume behavior. Record those separately in [PLAN.md](PLAN.md).

### 5.3 Device run: the GLES renderer

Recorded from a real device, because none of the above can reach a GPU. This is
evidence for the M8 GLES backend, not a pass from `tools/test_qualify.sh`, which
is host-only and keeps `ES01-device` OPEN.

Device: Retroid Pocket Flip2 (kona), arm64-v8a, Android 13 (API 33), Adreno.
Build: `port/android/build.sh --gles`, `data/mugen.cfg` with
`RenderMode = OpenGL`.

| Check | Result |
|---|---|
| Renderer selected | `renderer: requested=opengl effective=opengl caps=0x1F` |
| GL context | real Adreno ES context (`AdrenoGLES-0`, `OpenGL ES Shader Compiler E031.42.24.00`) |
| Programs | `GLES: renderer ready (indexed+rgba+solid, max texture 16384)` |
| Startup | reaches `Mode select init` and `Match loop init`, process stays alive |
| Frame coverage | bbox of non-black pixels `x=0..1916 y=0..1076` of 1920x1080 — identical to the System renderer |
| Colour parity | mean RGB within one unit per channel of System over three consecutive frames |
| Frame pacing | 60 swaps per 1000 ms with 1.0 tick per swap — identical to System |
| GL errors | none reported on the draw or present path |

What this run does **not** establish: context loss and restore (12.6), nested
render targets, shader variant selection beyond `PERSPECTIVE_EVEN`, primitive and
font coverage, or capture. Those are tracked in
[RENDERING_PLAN.md §12](../../RENDERING_PLAN.md).

Reproduce with:

```sh
export ANDROID_NDK_HOME="$PWD/build/android/toolchains/sdk/ndk/27.1.12297006"
bash port/android/build.sh --gles
adb install -r build/android/mugen-android-arm64-v8a.apk
adb logcat -c; adb shell am force-stop org.mugen_unleashed.mugen
adb shell am start -n org.mugen_unleashed.mugen/org.libsdl.app.SDLActivity
adb exec-out run-as org.mugen_unleashed.mugen cat files/mugen/mugen.log   # needs a debug build
```
