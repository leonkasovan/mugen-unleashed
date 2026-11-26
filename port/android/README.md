# mugen-unleashed on Android

Android now uses **native SDL2 APIs**, **SDL2_ttf** and **SDL2_mixer**.
There is no Android SDL1 API shim or SDL1 library dependency. The desktop
`src/sdl2video.c/.h` remains for desktop builds and is excluded from Android.
`android_main.h`, `android_gfx.c` and `android_glstub.c` have been removed.

Current target: **arm64-v8a**, minimum API 21, compile SDK 34, target SDK 29.
The build uses the **GLES 3.0 renderer** (`mugenGles=ON`, always), which supports
`RenderMode = OpenGL` through GLES 3.0. `OpenGLScreen` falls back to System.

See [REVIEW.md](REVIEW.md) for the native SDL2 migration evidence and
[PLAN.md](PLAN.md) for device qualification.

## Build entry points and layout

Android uses Gradle and the native CMake project. The root Makefile remains
for Windows/Linux; do not invoke it to build this Android port.

```text
port/android/
  build.bat                         setup/check tools, stage inputs, invoke Gradle
  build.ps1                         the same, PowerShell parameters
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
    build_sdl2_companions.bat       SDL2 + SDL2_ttf + SDL2_mixer cross-build
    build_sdl2_companions.ps1       the same, PowerShell parameters
    toolchain_metadata.ps1          Adoptium / Google metadata for the bootstrap
  tests/CMakeLists.txt              host build of the actual Android engine paths
  tests/test_native_sdl2.c           native SDL2 runtime tests
  tests/test_present_host.c          present/capture host harness
```

## 1. Toolchain setup

`port\android\build.bat` needs no Bash, Python or MSYS2: BITS (or `curl.exe`
when BITS is unavailable), bsdtar and
PowerShell are Windows' own, and CMake/Ninja come from the SDK. Missing pieces
are downloaded rather than left as documented prerequisites, and nothing is
fetched with git. The cmd.exe entry points build on Windows x64 only; the
pinned NDK ships a windows-x86_64 prebuilt.

From the repository root:

```bat
port\android\build.bat --check-tools     :: validate the tools, build nothing
port\android\build.bat                   :: release APK
```

`build.bat` checks the JDK 17 / SDK / NDK / CMake and installs whatever is
missing: Temurin from Adoptium's official metadata, the command-line tools from
Google's repository XML, and the SDK/NDK/CMake packages through the SDK's own
`sdkmanager.bat`. Missing SDL2 and its companions are cross-compiled next.
`--check-tools` validates only and never downloads.

```powershell
.\port\android\build.ps1 -CheckTools     # validate the tools, build nothing
.\port\android\build.ps1 -Debug -NoAssets
```

`build.ps1` is the PowerShell front end: PowerShell-style switches (`-Debug`,
`-NoAssets`, `-Gles`, `-Model3d`, `-Clean`, `-Offline`, `-Setup`) over the same
`build.bat`, with unrecognised arguments passed through. PowerShell may need
`-ExecutionPolicy Bypass` (or `Unblock-File build.ps1`) before it will run a
local script.

```bat
port\android\build.bat --setup
```

`--setup` installs the missing tool components and then cross-compiles SDL2,
SDL2_mixer and SDL2_ttf, stopping before Gradle and asset packaging. SDK licence
prompts are answered with `y` by default; set `MUGEN_ACCEPT_LICENSES=0` to
answer them yourself. Rerun after a failed download or interrupted installation.

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
| `build/android/toolchains/env.bat` | Environment exports for subsequent native-library commands |
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
| `MUGEN_ACCEPT_LICENSES` | Default `1`: answer `sdkmanager` licence prompts with `y`; `0` leaves them to you |
| `GRADLE_USER_HOME` / `ANDROID_USER_HOME` | Optional existing cache/state overrides |
| `SDL2_DEPS_DIR` | SDL2 prefix; default `build/android/sdl2/arm64-v8a` |
| `SDL2_VER` | SDL2 release to build when the prefix is absent; default `2.32.10` |
| `SDL2_COMPANION_DIR` | Mixer/TTF prefix; default `build/android/android-deps/arm64-v8a` |
| `FFMPEG_DEPS_DIR` | FFmpeg prefix for `--video-bg`; default `build/android/ffmpeg/arm64-v8a` |
| `GAME_DATA_DIR` | Game tree to copy; default repository `install/`, which is fetched from the release assets when a clone has none |

The script normalizes its exported SDK roots so Gradle and sdkmanager use the
same SDK. An unsuitable inherited Java 8/11/21 is not used as the Gradle JVM;
select JDK 17 or request setup. A valid existing JDK does not get replaced.

## 2. SDL2 and the native companions

`--setup` (or the first plain build) downloads SDL2 2.32.10's release tarball
with BITS, cross-compiles it, then builds SDL2_ttf and SDL2_mixer against it
into `build/android/android-deps/arm64-v8a/`. All of it lives under
`build/android/`; nothing is fetched with git. The Java integration is from SDL
2.32.10, so that release is used until both sides are deliberately updated
together.

If existing prefixes are available:

```sh
export SDL2_DEPS_DIR=/path/to/android-sdl2-prefix
export SDL2_COMPANION_DIR=/path/to/android-mixer-ttf-prefix
```

SDL2's prefix must contain `include/SDL2/SDL.h`, `lib/libSDL2.so` and its CMake
package files when building the companions. The companion prefix needs its
matching headers plus `lib/libSDL2_mixer.so` and `lib/libSDL2_ttf.so`.

`port\android\tools\build_sdl2_companions.bat` builds SDL2 first when the
prefix is absent, then installs the two companion libraries into
`build/android/android-deps/arm64-v8a/`, which `build.bat` uses by default. It
reads the NDK and CMake pins from `app/build.gradle`, downloads the pinned
upstream releases into `build/android/android-deps-src/`, and needs no Bash or
Python:

```bat
set SDL2_DEPS_DIR=C:\Projects\mugen-unleashed\build\android\sdl2\arm64-v8a
port\android\tools\build_sdl2_companions.bat
```

```powershell
.\port\android\tools\build_sdl2_companions.ps1 -Sdl2DepsDir .\build\android\sdl2\arm64-v8a
```

The version tags and target are environment overrides in the `.bat`
(`ANDROID_ABI`, `ANDROID_PLATFORM`, `SDL2_VER`, `SDL2TTF_VER`, `SDL2MIXER_VER`,
`FREETYPE_VER`) and parameters in the `.ps1` (`-Abi`, `-Platform`,
`-Sdl2Version`, `-TtfVersion`, `-MixerVersion`, `-FreetypeVersion`). It
installs nothing outside the repository.

The equivalent manual workflow, without the script, is:

```sh
source build/android/toolchains/env.sh
mkdir -p build/android/sources
curl -L -o build/android/sources/SDL2-2.32.10.tar.gz \
  https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
tar -xf build/android/sources/SDL2-2.32.10.tar.gz -C build/android/sources

export SDL2_DEPS_DIR="$PWD/build/android/sdl2/arm64-v8a"
cmake -S build/android/sources/SDL2-2.32.10 -B build/android/build-sdl2 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL2_DEPS_DIR" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
cmake --build build/android/build-sdl2
cmake --install build/android/build-sdl2
```

Do not re-extract into an existing source directory; reuse the recorded tree or
prepare a separate clean one.

The companion builder (`tools/build_sdl2_companions.bat`) selects SDL2_ttf
2.20.2 with FreeType 2.12.1 and SDL2_mixer 2.6.3 with WAV/OGG/MP3-capable
decoders. **That does not establish
engine BGM support:** the current engine route rejects those formats while
this mixer profile disables the tracker/MIDI formats it accepts. This remains
open in REVIEW.md. The companion source downloads/build itself were not rerun
in the documentation/bootstrap validation; record their hashes and dependency
closure when doing a real build.

## 3. Build and install

The APK uses the GLES 3.0 renderer. The build stages a copy of the game data and
the Android config overlay.

```sh
port\android\build.bat --check-tools
GAME_DATA_DIR=/path/to/game-tree port\android\build.bat
port\android\build.bat --debug --no-assets
port\android\build.bat --download-tools --debug --no-assets
```

`--no-assets` really clears the generated asset staging. It does not provide
game data at runtime: use it only for a previously provisioned data directory
or diagnostic builds. The first-run extraction/update limitations still apply.

### GLES sprite atlas and batching

Build the Android GLES APK with `port\android\build.bat`. In the
installed game's `data/mugen.cfg`, set these keys in the existing `[Video]`
section and restart the app.

```ini
[Video]
RenderMode = OpenGL
SpriteAtlas = 1
SpriteBatch = 1
```

The config is under `Android/data/org.mugen_unleashed.mugen/files/mugen/` on
app-specific external storage. A fresh install gets the embedded default; APK
updates preserve an existing edited `mugen.cfg`.

`SpriteAtlas` packs eligible indexed SFF sprites into shared textures.
`SpriteBatch` combines compatible consecutive atlas draws while preserving
draw order. The atlas is off by default, and omitting `SpriteBatch` (or setting
it to `-1`) enables batching only when the atlas program is available. To
compare against the original GLES path, set both keys to `0` and run the same
scene at the same resolution. With `[Debug] ShowFPS = 1`, compare `calls` and
`draw` in the `perf:` lines of `mugen.log`. When both settings take effect, look for
`GLES: indexed sprite atlas enabled` and `GLES: sprite batching enabled`.

### WebM backgrounds

The Android build follows [ikemen-droid’s per-ABI native-library workflow](https://github.com/satan-1412/ikemen-droid/blob/502b72cd29ca8e926467f867582f0ef9bddadf24/README.md).
Build the FFmpeg libraries separately, then let the Android Gradle Plugin
package the four shared libraries imported by CMake into the arm64-v8a APK.

From the repository root on Linux, WSL or macOS, with the host's Android NDK
and GNU Make available:

```sh
git submodule update --init third_party/ffmpeg-7.1.5
export ANDROID_NDK_HOME=/path/to/ndk/27.1.12297006
sh port/android/tools/build_ffmpeg.sh
```

The script builds FFmpeg 7.1.5 for API 21 and installs headers and libraries
under `build/android/ffmpeg/arm64-v8a`. Use that prefix from the Windows checkout
or set `FFMPEG_DEPS_DIR` to the path where you copied it.

```bat
set "GAME_DATA_DIR=C:\Projects\mugen-data"
port\android\build.bat --debug --video-bg
```

The PowerShell equivalent is `.\port\android\build.ps1 -Debug -VideoBg`.
Both switches enable GLES. In a direct Gradle invocation, pass
`-PmugenGles=ON -PmugenVideoBg=ON -PffmpegDepsDir=<prefix>` along with the usual
SDL2 paths. CMake supplies the FFmpeg binaries to Gradle, so keep the prefix
outside `app/src/main/jniLibs`.

Use `RenderMode = OpenGL` and place the `.webm` beside the DEF that declares
`type = video` and its relative `path`. The regular asset manifest includes
the video when it is in the packaged game tree. Supported video codecs are VP8
and VP9. Video plays silently and fits the existing render target.

### The launcher icon

The icon is not a hand-drawn resource either. `build.bat` runs
`tools/make_android_icons.sh` (removed with the MSYS2 entry points) before Gradle, which scaled `res/icons/1.ico` —
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
replacing `res/icons/` and re-deriving the committed copies; nothing does that
automatically any more (the Python checker that watched them went with the MSYS2
entry points), so the two `tools/iconpack.py` subcommands below are the whole
procedure.

**`port\android\build.bat` uses no Python**, so it never runs that derivation: it
removes any `app/build/generated/res/mugen-icons/` left by an earlier run (that
directory wins over the committed copies) and packages the committed
`app/src/main/res/mipmap-*` artwork as it stands. After changing `res/icons/`,
regenerate the PNGs with the two
`tools/iconpack.py` subcommands before building with it.
`--clean` removes generated app build/CMake output; it does not uninstall tools.

Output: **`build/android/mugen-android-arm64-v8a.apk`**; with `--debug`,
`build/android/mugen-android-arm64-v8a-debug.apk`. Only arm64-v8a is currently
supported by Gradle; another `ANDROID_ABI` is rejected rather than producing a
mislabeled arm64 APK.

After sourcing the tool environment, use the SDK's adb:

```sh
adb install -r build/android/mugen-android-arm64-v8a.apk
adb logcat -s mugen
```

### Building without network access

There is no `--offline` flag anywhere in this port, or in any other build entry
point: building is online. What a machine without network access needs is an
already-complete environment rather than a flag — the toolchain (JDK, SDK, NDK,
CMake) must already be installed, and Gradle needs a populated distribution and
dependency cache from a previous online run. With those in place,
`--check-tools` proves the toolchain is complete, and a normal build downloads
nothing new.

```sh
port\android\build.bat --check-tools --no-assets
```

Every fetch that remains checks its local copies first — `build/downloads/` for
the pinned archives, the extracted SDK and NDK trees for the tools — so a warm
cache does not touch the network at all. The Gradle wrapper is the exception: it
starts a distribution download whenever its cache key is missing, which is
exactly what an offline machine cannot satisfy. Warm the cache with one online
build first.

### Signing

Gradle selects release signing when `ANDROID_KEYSTORE` identifies an existing
keystore; otherwise the release variant falls back to debug signing and warns.
Provide the required key credentials for your distributable build:

```sh
export ANDROID_KEYSTORE=/path/to/release.jks
export ANDROID_KEYSTORE_PASS=...
export ANDROID_KEY_ALIAS=androidkey
export ANDROID_KEY_PASS=...
port\android\build.bat
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

The software renderer uses `RenderMode = System`. Android preserves the
configured game resolution. `KeepAspect`
uses SDL2 logical sizing/letterboxing; disabling it fills the physical output.
The System rasterizer expects RGB565 and tightly packed rows. Android display
creation rejects unsupported depths and incompatible pitch instead of uploading
mismatched pixels. Offscreen indexed font surfaces remain supported. Text scaling uses the shared
`src/sdl2surface.c` engine operation; Android does not export `zoomSurface`.

### `data/mugen.cfg` is the project default, with an Android overlay

`res/configs/mugen.cfg` -- the same default configuration every target embeds
-- is compiled into `libmain.so` and written into the extracted asset tree as
`data/mugen.cfg` when it is absent (`src/configembed.c`). `build.bat` stages no
configuration, and `GAME_CFG` is gone.

Nothing Android-specific is in it. The device's settings are in
`res/configs/mugen.android.cfg`, which is embedded too and written into the
extracted asset tree as `data/mugen.android.cfg` when it is absent; the engine
merges it over `data/mugen.cfg` per key at
start-up (`src/configmerge.c`): `RenderMode`, `FullScreen`, `KeepAspect`,
`StageFit`/`SystemFit`, `GameWidth`/`GameHeight`, `FirstRun` and the sound
cache. A key the overlay does not name keeps the game tree's value, so a tree
that sets its own resolution still gets the panel's.

The overlay may not force `[Options]`, `[P<n> Keys]`, `[P<n> Joystick]`,
`[P<n> Gamepad]`, the three `[Sound]` volumes or `[Input] p<n>.joystick.type`:
the options screen writes those back into `data/mugen.cfg`, so a forced value
would be reverted on the next boot. Set them in the game's file instead.

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
the key configuration menu.

**Touch-only play works.** `TouchControllerOverlay` draws an on-screen pad
(d-pad, left stick, six attack buttons, Start and Select) over the SDL surface
and registers a virtual joystick through `SDLControllerManager.nativeAddJoystick`,
so touch input travels the same path a physical pad does: SDL reports it as an
Xbox 360 controller, `pads.c` maps it through `pad_ctrl_map[]`, and the
`[P<n> Gamepad]` groups in `mugen.cfg` apply. Nothing in `src/` is
Android-specific for it. Verified on a P50 Pro: `mapped=1`, attached to slot 0
(player 1), d-pad and all six attack buttons and Start/Select arriving as codes
1/4/5/6/7/8/9/10/11/12, and the stick as `LEFTX/LEFTY -> dir`.

The overlay hides itself while a physical gamepad is connected and returns when
it is removed (`InputManager.InputDeviceListener`), releasing any held button as
it goes. It also applies the window insets as padding, so controls are not
placed under a display cutout; without that a right-anchored button on a P50 Pro
was laid out at the panel's 2700 px width and clipped to one pixel by the
2464 px the notch leaves usable. The layout is landscape, assumes no safe area
beyond those insets, and does not scale its buttons with screen size.

**Select Game Folder** (`R.string.game_folder_str`, top-right) points the engine
at a game tree anywhere on shared storage instead of the default
`Android/data/<pkg>/files/mugen`. The choice is written to SharedPreferences and
to `mugen_gamedir.txt` in the app's external files directory; the native side
reads that file in `android_main.c` because the working directory is resolved in
C before any Java could hand it over. A marker naming a path that no longer
exists is ignored, so an uninstalled card cannot strand the engine. When the
chosen folder already holds a game (`data/mugen.cfg`, `data/system.def` or
`mugen.cfg`), the bundled assets are **not** extracted into it -- a pack the
player installed is theirs, and adding the sample characters on top of it is
not what the button promises.

On Android 11+ the folder is only usable with the **All files access** grant, so
the button asks for it first and opens the picker on return. `targetSdkVersion`
is still 29, but that exemption does **not** hold: with legacy storage the engine
reported `could not create data/mugen.cfg: Permission denied` when pointed at
`/sdcard/TestMugen` on an Android 12 device, and wrote there normally once the
grant was given. The check is therefore on the platform version, not the target.
A chosen folder must also be writable, not just listable -- the engine writes
`mugen.log` and its config into the tree -- so the picker rejects one that fails
a write probe instead of restarting into an error dialog.

Selecting a folder **reloads the engine in place**, it does not ask the player
to quit. The handshake is three steps: Java calls `nativeApplyGameFolder(path)`,
which only records the request under a mutex and raises a flag; the engine
unwinds the current screen at a frame boundary; and `Mugen_Main` then tears the
session down with `System_Deinit()` and calls `Android_OpenRequestedFolder()`,
which opens the new tree exactly as a cold start does. The SDL thread and the
window are left alone, so this is a full reload without a process restart.

The UI thread deliberately does **not** `chdir`. The working directory is
process-wide and a game is still running in the tree being left, so moving it
there would make every relative load or save in that window resolve against the
new tree -- a save from the options screen would land in the pack the player had
just selected. The engine thread moves itself, after teardown.

The reload is consumed at the frame boundary (`Screen_Tick`), not only between
gameflow states, because a title screen with the attract demo disabled can sit in
one state indefinitely. The standalone `-storyboard` path handles it separately,
since that path never enters the gameflow.

Ordering rules this depends on, each found by breaking it on the device:

- `Android_ApplyChosenFolder()` runs before `Log_Init()`. Resolving the tree
  afterwards put the log in one folder and the engine in another.
- The working directory is settled inside `Mugen_Main`, not in
  `android_main.c`'s `main()`, so a chosen folder takes effect. Doing both ran
  extraction twice.
- `assets_dir()` writes into `g_assets_buf` by name. An intermediate
  `char *buf = g_assets_buf` alias made `sizeof(buf)` a pointer size, so a
  `snprintf` kept only "/storag" and the engine looked for its tree there.
- `<tree>/cmdline` is read in `Mugen_Main` after the tree is open (via
  `CmdLine_AppendFile`), not in `android_main.c` before it. Read earlier, the
  handle resolved against whatever directory the process started in, so the
  selected tree's file was silently ignored. The read sits *after* the `restart:`
  label so a switch re-parses the new tree's file; above it, only a cold start
  ever read one. The argv the tree's options are appended to is rebuilt from the
  caller's original argv each pass, so a switch cannot accumulate the previous
  tree's options.

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

### 5.1 Host native tests

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
`libmain.so` plus imported SDL libraries for SDL1 `DT_NEEDED` entries.
`tools/check_sdl2_only.py` checks for legacy SDL includes, calls and aliases in
the engine and native port sources.
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
Build: `port\android\build.bat`, `data/mugen.cfg` with
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
port\android\build.bat
adb install -r build/android/mugen-android-arm64-v8a.apk
adb logcat -c; adb shell am force-stop org.mugen_unleashed.mugen
adb shell am start -n org.mugen_unleashed.mugen/org.libsdl.app.SDLActivity
adb exec-out run-as org.mugen_unleashed.mugen cat files/mugen/mugen.log   # needs a debug build
```
