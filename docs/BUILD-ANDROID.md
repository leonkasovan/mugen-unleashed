# Building the Android port

The Android build uses Gradle and the native CMake project. The root Makefile
remains for Windows/Linux; do not invoke it to build this port.

## Prerequisites (host)

On Windows, `port\android\build.bat` needs no Bash and no Python at all: it
drives the JDK, the SDK's `sdkmanager.bat`/CMake, `gradlew.bat` and cmd
execution. `port\android\build.bat` is the Bash entry point (MSYS2/Git Bash) and
does require Bash, `curl` and Python 3.8+ for its tool-installation path.

| Component | Pins / notes |
|---|---|
| **JDK** | Adoptium/Temurin 17 (runnable `javac`); missing JDK downloaded from official metadata |
| **Android SDK** | `platforms;android-34`, `build-tools;33.0.1` (matching AGP 8.1) |
| **NDK** | `27.1.12297006`; validate `source.properties` |
| **CMake / Ninja** | `3.22.1` from the SDK |
| **Gradle / AGP** | Wrapper 8.1.1 / AGP 8.1.1 |
| **Device tools** | `platform-tools` (adb) |

## Build entry points

| Path | Purpose |
|---|---|
| `port\android\build.bat` | Host preflight, tool bootstrap, Gradle invocation, APK staging |
| `port/android/app/build.gradle` | ABI, SDK/NDK/CMake pins, signing |
| `port/android/app/src/main/jni/src/CMakeLists.txt` | Native `libmain.so` + engine vendor sources |
| `port/android/tests/test_build.py` | Offline bootstrap/orchestration fixtures |

## Typical workflow

```sh
# From the repository root
port\android\build.bat --setup       # installs missing tools, exits before SDL/lib checks
source build/android/toolchains/env.sh   # exports SDK/NDK/CMake roots

# Optional: source-build SDL2 (if no prebuilt prefix is available)
export SDL2_DEPS_DIR=/path/to/android-sdl2-prefix
export SDL2_COMPANION_DIR=/path/to/android-mixer-ttf-prefix
# ... clone SDL, cmake --build --install, then run the companion script ...

# Full build (arm64-v8a is the only ABI supported by Gradle)
GAME_DATA_DIR=/path/to/game-tree port\android\build.bat
# or, keep the repository's reference install:
GAME_DATA_DIR=install port\android\build.bat
```

Output: `build/android/mugen-android-arm64-v8a.apk`.

## Build variants

| Variant | Flag / note |
|---|---|
| `--setup` | Installs missing tool components; does **not** check SDL libraries or package assets |
| `--download-tools` | Downloads missing tool components (JDK/NDK SDK manager) |
| `--check-tools` | Verifies all required tools are present |
| `--offline` | Runs without downloading; requires a pre-populated Gradle cache |
| `--debug --no-assets` | Builds debug APK without repackaging assets |
| `--clean` | Removes generated app build/CMake output; does not uninstall tools |

## Signing

Gradle selects release signing when `ANDROID_KEYSTORE` identifies an existing
keystore; otherwise the release variant falls back to debug signing.

```sh
export ANDROID_KEYSTORE=/path/to/release.jks
export ANDROID_KEYSTORE_PASS=...
export ANDROID_KEY_ALIAS=androidkey
export ANDROID_KEY_PASS=...
port\android\build.bat
```

The key password defaults to the store password; the alias defaults to
`androidkey`. Missing or wrong credentials can still fail signing.

## Runtime configuration

The APK extracts game data into the app-specific external-storage `mugen/`
directory, with internal-storage fallback. Use `RenderMode = System` in
`mugen.cfg`. Android preserves the configured game resolution (default 640×480),
rather than requesting the Vita's 960×544 panel. `KeepAspect` uses SDL2
logical sizing/letterboxing; disabling it fills the physical output.

## Native host tests (Linux host, no device needed)

```sh
cmake -S port/android/tests -B build/android-native-tests -DNATIVE_SDL2_SANITIZE=ON
cmake --build build/android-native-tests -j4
ctest --test-dir build/android-native-tests --output-on-failure
```

Run with dummy drivers to exercise the real BGM thread and effects:

```sh
cd build/android-native-tests
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./test_native_sdl2 \
  /path/font.ttf /path/music.wav /path/music.ogg /path/music.mp3
```

## Known differences from the desktop build

| Area | Desktop | Android |
|---|---|---|
| **Renderer** | OpenGL / OpenGLScreen / System | System only; desktop GL modes log a fallback before table selection |
| **SDL1 compatibility** | `src/sdl2compat.c/.h` compiled | Excluded — `sdl2compat.c` and `sdlmixercompat.c` are not compiled |
| **Music route** | Windows plugin DLLs (mpg123/vorbisplug) | Native SDL2_mixer for WAV/OGG/MP3 only; mpg123/vorbisplug DLLs silently skipped |
| **Key translation** | Direct engine config IDs | SDL2 keycodes → engine config IDs at the boundary; inverse for focus snapshots |
| **Controllers** | Raw joystick API, Vita bridge | Native SDL2 instance IDs/topology; no Vita bridge or fixed 960×544 panel |
| **Fonts** | zoomSurface + SDL_gfx | Native SDL2_ttf + shared engine scaler; zoomSurface and SDL_gfx not exported |
| **Build system** | MSVC / Make / MinGW | Gradle + CMake + NDK |

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

The prior host evidence (validation/release/host-evidence.txt, 8 037 443‑byte APK,
SHA‑256 `54ccf452…`) predates the native API removal. A new APK must replace
that build evidence.