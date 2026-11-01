# mugen-unleashed on Android

The Android port already links **SDL2**, while a compatibility adapter supplies
SDL 1.2-style engine calls. The remaining migration is about correct SDL2
semantics and platform isolation, not replacing an Android SDL1 library.

Current target: **arm64-v8a**, minimum API 21, compile SDK 34, target SDK 29.
Only the engine's **System/software renderer** is implemented. A streaming
texture/SDL renderer presents its pixels; this is not the MUGEN OpenGL renderer.

**Runtime qualification is incomplete.** The validated [review](REVIEW.md)
documents open key-indexing, Vita-policy leakage, asset recovery, video lifetime,
resizing, frame presentation, scaling and BGM-routing defects. A successful
tool setup or APK build does not establish working gameplay.
See [PLAN.md](PLAN.md) for the detailed migration and acceptance gates.

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
      android_main.c/.h            entry, assets, legacy API aliases
      android_video.c              software-surface/streaming presentation
      android_gfx.c                current local scaling implementation
      android_glstub.c             excluded-renderer symbols
  tools/
    bootstrap_toolchains.sh        missing-tool installation
    toolchain_metadata.py          vendor metadata, checksums, extraction
    build_sdl2_companions.sh        SDL2_ttf / SDL2_mixer cross-build
  tests/test_build.py               offline bootstrap/orchestration fixtures
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
`--clean` removes generated app build/CMake output; it does not uninstall tools.

Output: **`build/android/mugen-unleashed-arm64-v8a.apk`**.
Only arm64-v8a is currently supported by Gradle; another `ANDROID_ABI` is rejected
rather than producing a mislabeled arm64 APK.

After sourcing the tool environment, use the SDK's adb:

```sh
adb install -r build/android/mugen-unleashed-arm64-v8a.apk
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

`android_main.c` extracts APK assets, changes the working directory, appends
optional `cmdline` file tokens to SDLActivity's arguments, then calls
`Mugen_Main`. The file tokenizer is whitespace-based, has bounded line/argument
storage and does not implement shell quoting. Argument order is preserved;
the engine parser determines the precedence of repeated options.

The intended data directory is the app-specific external files directory plus
`mugen`, often:

```text
/sdcard/Android/data/org.mugen_unleashed.mugen/files/mugen
```

There is an internal-files fallback. Consult logcat for the actual path.
App access to its directory does not imply every file manager can edit it on
every Android version. Broad storage permission is not an import UI. A future
SAF/import workflow is covered by PLAN.md.

Assets currently use `manifest.txt` and a constant extraction stamp. Existing
files are skipped, including potentially interrupted files; new payloads are
not automatically reconciled. Do not delete user data blindly to “fix” an
upgrade. Use backed-up test data while validating recovery behavior.

Touch controls are not implemented. Keyboard/gamepad input also needs the
domain/mapping fixes in REVIEW.md before it can be described as qualified.
FPS, audio playback, background/resume, real display scaling and clean exit have
not been verified on a device by this update.

## 5. Validation and troubleshooting

```sh
bash -n port/android/build.sh
bash -n port/android/tools/bootstrap_toolchains.sh
bash -n port/android/tools/build_sdl2_companions.sh
python3 port/android/tests/test_build.py
```

The tests cover bootstrap and orchestration with simulated vendors/tools,
including paths with spaces and checksum/download failures. They are not
real SDK downloads, native compilation or an APK/device test.

| Failure | Check |
|---|---|
| Missing JDK/SDK/NDK/CMake | Run `--setup` or supply the documented matching paths |
| NDK revision mismatch | Read the reported `source.properties` revision; directory names are not a version check |
| Download checksum/network failure | Nothing unverified is extracted; rerun setup after checking connectivity/vendor metadata |
| sdkmanager fails | Read the vendor error/license prompt; do not treat an incomplete installation as success |
| Missing SDL libraries/headers | Follow section 2 or set both dependency prefixes; tool setup alone is insufficient |
| UnsatisfiedLinkError | Inspect ABI, SDL Java/native version match and every native `DT_NEEDED` dependency in the APK |
| Black/blank renderer | Verify actual cfg says System; check the open panel-size/renderer-selection defects |
| Input crash or wrong action | See A01/A02 in REVIEW.md; do not assume raw SDL2 keys/buttons are engine codes |
| Extraction/upgrade problem | Preserve user data and investigate the stamp/partial-file issue (A04) |
| Normal exit crash | Investigate cleanup ordering (A05), not just the last success log line |

References: [validated review](REVIEW.md), [migration plan](PLAN.md),
[AGP 8.1 requirements](https://developer.android.com/build/releases/agp-8-1-0-release-notes),
[SDK manager](https://developer.android.com/tools/sdkmanager),
[NDK configuration](https://developer.android.com/studio/projects/configure-agp-ndk),
and [Adoptium installation API](https://adoptium.net/installation/ci-scripts/).
