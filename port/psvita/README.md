# mugen-unleashed — PlayStation Vita port

Cross-build of the reconstructed M.U.G.E.N 1.1 Beta 1 engine for the
**PS Vita**, using [VitaSDK](https://github.com/vitasdk) (ARMv7-A, newlib).

The engine itself (`../../src`) is the Windows reconstruction, unchanged in
behaviour. This directory holds only what the Vita needs: a `CMakeLists.txt`,
the LiveArea assets, and the OpenGL adaptation.

Output: a `mugen-psvita.vpk` you can copy to the console and install with VitaShell.

## Quick start

From the repository root, in MSYS2 / Git Bash, on Windows:

```sh
port/psvita/build.sh
```

That is the whole thing on a clean machine. It downloads VitaSDK (196 MB,
cached in `build/downloads/vitasdk.zip`, SHA-256 checked), installs it to
`C:\vitasdk`, puts it on `PATH` for the build, checks the VitaSDK packages,
configures CMake and builds. The result:

```
build/psvita/mugen-psvita.self   2171 KB   the runnable module
build/psvita/mugen-psvita.vpk    2203 KB   installable, engine only
                                 4957 KB   with install/ packed as /data
```

Because `install/` is a real M.U.G.E.N tree, the VPK is built with its data
inside by default — a VPK without data installs and then exits. `--no-data`
builds the engine-only one, `--data DIR` packs another tree.

| Flag | Effect |
|---|---|
| *(none)* | install what is missing, then build `build/psvita/` |
| `--setup` | install VitaSDK and its packages, then stop |
| `--check-tools` | report VitaSDK, packages, cmake/ninja/pkg-config; never downloads |
| `--clean` | remove `build/psvita/` before building |
| `--offline` | fail instead of downloading (needs the cached zip or an SDK already present) |
| `--debug` | `Debug` build instead of `Release` |
| `--jobs N` | parallel jobs (default: CPU count) |
| `--data DIR` / `--no-data` | pack this game tree / pack no game data |
| `--titleid ID` | nine-character title ID (default `MUGEN0001`) |
| `--renderer MODE` | *retired*: set `[Video] RenderMode` in `mugen.psvita.cfg` |
| `--resolution WxH` | *retired*: set `[Config] GameWidth/GameHeight` and `[Video] Width/Height` |
| `--game-resolution WxH` / `--output-resolution WxH` | *retired*: same file |
| `--precache N` | *retired*: set `[Misc] Precache` |
| `--shader-compiler FILE` | bundle this `libshacccg.suprx` instead of the fetched default |
| `--no-shader-compiler` | bundle no compiler (an OpenGL build then needs it on the console) |
| `--gl-selftest` | startup GL smoke test and frame-60 capture |
| `--render-cache` / `--no-render-cache` | draw-state A/B test (CMake default: on) |
| `--shader-cache` / `--no-shader-cache` | experimental vitaGL disk shader cache (default: off) |
| `--env` | print the three exports for your own shell, build nothing |

The environment it sets for its children (a script cannot change its caller's):

```sh
export VITASDK=C:/vitasdk
export PATH=$VITASDK/bin:$PATH
export CMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
```

`eval "$(port/psvita/build.sh --env)"` applies them to the current shell, which
is what the manual CMake commands under "Building" below need. `VITASDK_DIR=`
installs or uses the SDK somewhere else; an exported `VITASDK` is used as-is
when it already holds a compiler, so an SDK you installed yourself (including
one from `setup_psvita_dev.bat`) is never re-downloaded.

The script needs Windows: the pinned archive is the Windows-hosted VitaSDK, its
compiler is `arm-vita-eabi-gcc.exe`. On Linux/macOS install VitaSDK natively and
use the manual commands below.

---

## What this build actually is

| | |
|---|---|
| Target | ARMv7-A, Arm® Cortex®-A9, VFPv3/NEON |
| Toolchain | VitaSDK `arm-vita-eabi-gcc` (GCC 15.2.0) |
| libc | newlib |
| Graphics | **vitaGL** — GL 1.x/2.1 compatibility layer over SceGxm |
| SDL | **SDL 1.2.16** from VitaSDK (`libSDL.a`), not SDL2 |
| Output | `mugen-psvita.self` (runnable) and `mugen-psvita.vpk` (installable) |

### Why SDL 1.2 and not SDL2

VitaSDK ships both. SDL2 is the one its own samples use, because SDL2 is what
its samples are written against — but SDL2 is the wrong fit here:

* the 78 engine modules call `SDL_SetVideoMode`, pass `SDL_HWSURFACE`,
  `SDL_HWACCEL` and `SDL_DOUBLEBUF`, and keep exactly one `SDL_Surface` for the
  screen;
* there is no renderer abstraction — `src/gfx.c` picks a renderer and
  `src/glrender.c` draws through it, both written against the SDL 1.2 surface
  model;
* SDL2 removed `SDL_SetVideoMode` and the software surface as a drawing target
  entirely.

Porting the engine to SDL2 would mean rewriting the renderer and the video
setup, which is a different project. VitaSDK's SDL 1.2 is a real 1.2.16 built
for the Vita (its `SDL_config_vita.h` enables the SceCtrl/SceAudio/SceThread
backends) and its OpenGL driver runs on vitaGL, so the engine's existing GL
path works as-is.

SDL 1.2, SDL_gfx, SDL_mixer, SDL_ttf, zlib and libpng all come from VitaSDK.
Lua 5.1.4, minizip 0.15 and libresample are built from `../../third_party`,
because `src/symbols.h` pins their versions and no Vita package matches them.

---

## Prerequisites

`port/psvita/build.sh` provides all of this on Windows. The list is what it
provides, and what to arrange yourself on a host it does not cover.

**VitaSDK.** The project publishes the SDK as a release asset
(`vitasdk.zip` on the `toolchains-v1.0` release, 196 MB packed / 600 MB
installed), pinned by SHA-256 in the script and unpacked to `C:\vitasdk`. It is
a complete SDK with the packages below already installed, so there is no `vdpm`
step on a clean machine. The environment it needs is

```bat
set VITASDK=C:\vitasdk
set PATH=%VITASDK%\bin;%PATH%
set CMAKE_TOOLCHAIN_FILE=%VITASDK%\share\vita.toolchain.cmake
```

which is also what `C:\Projects\setup_psvita_dev.bat` sets for an SDK installed
by hand. Any equivalent shell works; the build only needs `VITASDK` and the
toolchain file.

**VitaSDK packages.** The pinned SDK ships all of them. With a hand-made SDK
they are separate installs:

```sh
vdpm install sdl sdl_gfx sdl_mixer sdl_ttf zlib libpng
vdpm install vitaGL      # pulls in vitaShaRK, SceShaccCgExt and libmathneon
```

SDL 1.2's OpenGL video driver cannot initialise without vitaGL, and the CMake
configure fails with an explicit message if any of the four libraries is
missing. `build.sh` checks for the artefacts themselves (`libSDL.a`,
`libvitaGL.a`, ...) rather than trusting `vdpm`'s bookkeeping, because an SDK
unpacked from the archive has no package database to ask.

**A host `pkg-config`.** VitaSDK's `arm-vita-eabi-pkg-config` is a bash wrapper
around the host `pkg-config`, so one has to be on `PATH` — MSYS2's `pkgconf` or
w64devkit's.

**CMake ≥ 3.16 and Ninja** (or Make). CMake 4.x is fine.

---

## Building

### With the script

```sh
port/psvita/build.sh
```

See "Quick start" at the top for what it installs and the flags it takes.

### By hand

From the repository root, in a shell with `VITASDK` set:

```sh
export VITASDK=/c/vitasdk        # C:\vitasdk

cmake -S port/psvita -B build/psvita -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build/psvita
```

Result (verified against the pinned VitaSDK, clean build directory, nothing else
set; the exact bytes move as the engine changes):

```
build/psvita/mugen       4208 KB   ARM EABI ELF (intermediate)
build/psvita/mugen-psvita.self  2171 KB   SELF, compressed
build/psvita/mugen-psvita.vpk   2203 KB   installable homebrew, no game data
```

### Building without Ninja

From an MSYS2/Git Bash shell, where `sh.exe` is on `PATH`, use Unix Makefiles —
`MinGW Makefiles` refuses to configure while `sh.exe` is visible:

```sh
cmake -S port/psvita -B build/psvita -G "Unix Makefiles" ...
cmake --build build/psvita -j8
```

From `cmd.exe` (no `sh.exe` on `PATH`) `-G "MinGW Makefiles"` is the equivalent,
and `cmake --build build/psvita` picks the tool itself. `port/psvita/build.sh`
chooses Ninja, then Unix Makefiles, for exactly this reason.

### The packed `mugen.cfg` comes from `port/psvita/mugen.cfg`

`port/psvita/mugen.cfg` is the Vita's own config: `RenderMode = System`,
`GameWidth/GameHeight = 960x544` and explicit video `Width/Height = 960x544`,
`FullScreen = 1`, and `[P1 Joystick]` bound to the Vita driver's engine codes
(Triangle 9, Circle 10, Cross 11, Square 12, L 13, R 14, Down 15, Left 16,
Up 17, Right 18, Select 19, Start 20). `MUGEN_VITA_CFG` overrides the template;
with it empty, the data directory's `data/mugen.cfg` is used as before.

### `mugen.psvita.cfg` is merged over the game's `mugen.cfg`

A desktop `data/mugen.cfg` needs Vita-specific settings, so the port ships its
own file — `port/psvita/mugen.psvita.cfg`, packed as `data/mugen.psvita.cfg`:

```ini
[Config]
GameWidth  = 960        ; the game's file usually says 1280x720
GameHeight = 544

[Video]
RenderMode = OpenGL     ; see "The OpenGL renderer" below
FullScreen = 1          ; was 0
```

The values above are not packed by rewriting anything: `data/mugen.cfg` goes into
the VPK exactly as the game tree ships it, and `port/psvita/mugen.psvita.cfg` is
packed beside it as `data/mugen.psvita.cfg`. At start-up the engine merges that
file over the game's **per key** (`src/configmerge.c`, called from `Config_Load`
and `Options_Load`): a key the platform file names wins, every other key keeps
the game's value, and a group only the platform file has is appended.

```ini
[Config]
GameWidth = 960         ; the game's file says 1280x720
GameHeight = 544
FirstRun = 0
[Video]
Width = 960
Height = 544
RenderMode = OpenGL
FullScreen = 1
```

To change any of it, edit `port/psvita/mugen.psvita.cfg` and rebuild — there is no
flag for these values, and `--renderer`/`--precache`/`--resolution` now fail with
a message naming the file rather than being silently ignored. A plain
`cmake --build` repacks the edited file; no reconfigure is needed.

Groups the engine writes back at runtime (`[Options]`, `[P<n> Keys]`,
`[P<n> Joystick]`, `[P<n> Gamepad]`, the `[Sound]` volumes and
`p1/p2.joystick.type`) are **ignored** in the platform file: `Options_Save` and
the gamepad writer would revert them on the next boot, so the options screen
would appear not to save. The port's joystick layout comes from the game tree's
own `[P<n> Joystick]` group instead.

Check `gameCoord` and the native presentation descriptor separately in fresh
logs. A lower internal size is a test configuration, not a guarantee of any
particular frame rate. OpenGL output must be one of 960x544, 720x408, 640x368 or
480x272 — for example 640x480 is allowed *internally* with native output, but is
rejected as OpenGL output. OpenGLScreen ties its game and display sizes
together, so independent sizes require OpenGL. These rules are enforced at
configure time against the platform file itself, so a bad combination fails the
build instead of the console.

**Your game tree is never modified, and nothing in it is rewritten.** The VPK
carries `data/mugen.cfg` byte-for-byte as the tree ships it, plus
`data/mugen.psvita.cfg` from `port/psvita/`. Because the merge happens in memory
at start-up, a user who edits `data/mugen.cfg` on the console keeps their
change; only the keys the platform file names are overridden.

The one engine change this affects: `src/video.c` checks for a NULL surface
under `MUGEN_VITA_GL` and returns `LIBERR_VIDEO_MODE`, so a bad resolution now
produces a clear `mugen.log` line and the error dialog instead of a crash. The
Windows and Linux builds keep the original behaviour untouched.

### How pkg-config is handled

Nothing has to be configured by hand: `CMakeLists.txt` absorbs three VitaSDK
packaging problems at configure time. They are listed here because they are the
likely cause if the configure step fails.

1. **`freetype2.pc` requires `libpng`, which VitaSDK does not ship.** It ships
   `libpng16.pc` only, so `SDL_ttf` — whose `Requires.private` reaches freetype
   — is unresolvable. A one-line `libpng.pc` shell module is generated into
   `build/psvita/pkgconfig-shim/` on every configure.

2. **`arm-vita-eabi-pkg-config` cannot be executed on Windows.** It is a bash
   script with no file extension, so CMake's `execute_process` fails with
   "inappropriate file type or format" before the script ever runs. A host
   `pkg-config` is used instead, pointed at the SDK's `.pc` directory.

3. **Its `PKG_CONFIG_LIBDIR` is `:`-separated**, which MSYS `pkg-config` splits
   at the drive letter — `C:/vitasdk/...` becomes `C` and `/vitasdk/...`, and
   every module is reported missing. The build sets the search path
   `;`-separated instead.

To bypass all of it:

```sh
cmake -S port/psvita -B build/psvita -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DPKG_CONFIG_EXECUTABLE=/path/to/pkg-config \
      -DMUGEN_VITA_PKGCONFIG_DIRS="dir1;dir2"
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `MUGEN_VITA_DATA_DIR` | *(empty)* | M.U.G.E.N directory to pack into the VPK as `/data` |
| `MUGEN_VITA_TITLEID` | `MUGEN0001` | 9-character title ID |
| `MUGEN_VITA_VERSION` | `01.00` | `APP_VER`, format `##.##` |
| `MUGEN_VITA_MEMSIZE` | `0` | `vita-make-fself -m`: `0` for a normal app (recommended), `0x1000`–`0x12800` KiB for system mode |
| `MUGEN_VITA_HEAP_MIB` | `64` | newlib heap; SDL's Vita video driver needs process memory for GXM too, so an oversized heap starves `sceGxmInitialize` |
| `MUGEN_VITA_CFG` | `port/psvita/mugen.psvita.cfg` | Platform config packed verbatim as `data/mugen.psvita.cfg`. Required: a missing file, a missing `RenderMode`, or an unsupported resolution fails the configure |
| `MUGEN_VITA_SHADER_COMPILER` | `build/downloads/libshacccg.suprx` | Module bundled as `module/libshacccg.suprx`; `build.sh` unpacks the release asset there. Its default is passed on every build |
| `MUGEN_VITA_ICON_ICO` | `res/icons/1.ico` | the artwork every `sce_sys/` image is derived from |
| `MUGEN_VITA_ICON_SIZE` | `128` | `icon0.png` edge length (the Vita expects 128) |
| `MUGEN_VITA_BG_W` / `_BG_H` | `840` / `500` | LiveArea background size |
| `MUGEN_VITA_STARTUP_W` / `_H` | `280` / `158` | LiveArea start-up image size |

### The bubble artwork

`sce_sys/` is not hand-drawn: `cmake/Icon.cmake` derives all three images from
`res/icons/1.ico` — the same artwork the Windows and Android builds use — at
configure time, with `tools/iconpack.py` (standard library only):

| Packed as | From | Size | Format |
|---|---|---|---|
| `sce_sys/icon0.png` | the icon, flattened onto its average colour | 128x128 | 8-bit indexed, opaque |
| `sce_sys/livearea/contents/bg.png` | the icon on the average colour of its own pixels | 840x500 | 8-bit indexed, opaque |
| `sce_sys/livearea/contents/startup.png` | the icon on a transparent frame | 280x158 | 8-bit indexed, tRNS transparency |

**The format is not cosmetic.** The Vita's package validation accepts only
8-bit *palette* PNGs (colour type 3) for this artwork; an RGBA one packs
correctly, copies correctly, and is then rejected by VitaShell on the console
with `Error 0x8010113D` — which names neither the file nor the reason, and is
the same code a truncated copy produces. Every VitaSDK sample ships colour type
3 (check `$VITASDK/share/gcc-arm-vita-eabi/samples/*/sce_sys`), and the assets
here were RGBA until that was tracked down.

So `tools/iconpack.py` takes `--indexed` (write a palette PNG, quantised to 256
colours) and `--flatten [auto|#rrggbb]` (composite the alpha away), and
`cmake/Icon.cmake` passes both for `icon0`/`bg` and `--indexed` alone for
`startup`. The same module then **reads each packed asset's IHDR and fails the
configure** unless it is colour type 3, bit depth 8 (1 is also accepted, which
is what the sample backgrounds use) and non-interlaced — so a regression costs a
configure error instead of a console round trip.

The generated files go into the build directory; the committed copies under
`port/psvita/sce_sys/` are the same derivation — same commands, same bytes — and
are packed only when the conversion cannot run (no Python, or a missing `.ico`),
so a tree without Python still builds a VPK the console accepts. Replacing the
artwork means replacing `res/icons/`, not editing PNGs. `bash
tools/test_vita_icon.sh` checks the conversions (and the format) without a
VitaSDK.

---

## Installing

```sh
# Copy to the console (with VitaShell in USB/FTP mode, or a memory card reader)
cp build/psvita/mugen-psvita.vpk /path/to/ux0:/

# Then in VitaShell: select mugen-psvita.vpk -> "Install"
```

Installs to `ux0:app/MUGEN0001/`. Start it from the LiveArea bubble.

### Verify the copy before installing

`port/psvita/build.sh` ends by printing the VPK's **exact byte size and
SHA-256**. Those describe *that build* (the archive carries file timestamps), so
the digest changes whenever the VPK is rebuilt — compare against the run that
produced the file you copied, not against this document:

```
bytes     5076204  mugen-psvita.vpk
sha256    9aa8c82a…
```

A short copy is the most common reason a VPK fails at install time, and the
console reports it as an anonymous promoter error with no hint that the file is
the problem. On the console, VitaShell's file list shows the size; with a memory
card reader `sha256sum` closes the question.

If the installer rejects a package that verifies, work through this in order.
The code is a `ScePromoterUtil` value (`0x801011xx`), which the console may also
show as a dialog code (`0x8010113D` is `C2-12691-9`), and it means "this package
was refused", not which check failed:

1. **The `sce_sys/` artwork format.** This was the cause the first time it was
   reported: RGBA LiveArea PNGs are rejected, 8-bit indexed ones are accepted,
   and the per-file format is invisible from the outside. It is guarded at
   configure time now, so a VPK from this tree cannot carry the wrong type —
   check with `unzip -p build/psvita/mugen-psvita.vpk sce_sys/icon0.png | xxd -l 32`:
   byte 25 is the colour type and must be `3`. See "The bubble artwork" above.
2. **Free space on `ux0:`.** The promoter unpacks the package beside the app and
   then moves it; it needs roughly twice the VPK's unpacked size free. A nearly
   full memory card fails here.
3. **A leftover bubble with the same title ID.** A previous failed install can
   leave `ux0:app/MUGEN0001/` behind, and the promoter then refuses the reinstall.
   Delete the bubble (or the directory) and retry.
4. **Transfer method.** Copy over VitaShell's USB/FTP mode into `ux0:` and
   install from there. A file on a USB-mounted partition (`uma0:`) is not a
   location VitaShell installs from.
5. **The SELF.** `vita-make-fself -c` (no `-s`, no `-m`) is what this build runs,
   which is the normal-app form the promoter expects. A SELF made by a mismatched
   `vita-make-fself` is also refused, so if the package verifies, the console has
   space and the artwork is indexed, reinstall VitaSDK from the pinned archive
   (`port/psvita/build.sh --setup`) and rebuild.

A `--no-data` build is **not** a way to isolate this failure: the artwork, the
SELF and the SFO are identical to the full VPK, only the game tree is missing, so
a packaging fault fails both exactly alike. It is still useful for a different
question — whether a *large* package installs at all — but check items 1–4 first.

Once it installs, the failure modes move to launch and `mugen.log` — see
"Diagnosing a failure on the console" below.

### Game data — read this

**This repository ships no M.U.G.E.N data and none of Elecbyte's files.** The
`install/` tree in the repository is a placeholder. The engine starts, reads
`data/mugen.cfg`, and exits with an error if the game tree is not there.

Two ways to supply it:

**A. Pack it into the VPK** (self-contained; good for a dedicated build)

```sh
cmake -S port/psvita -B build/psvita -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -DMUGEN_VITA_DATA_DIR=C:/mugen-1.1b1
cmake --build build/psvita
```

The configure warns if `data/mugen.cfg`, `data/mugen1/system.def` or `font/` is
missing, because a VPK built from an incomplete tree installs but exits at
start.

**B. Copy it separately** (keeps the VPK small and the game tree editable)

```
ux0:data/mugen/            <- the whole M.U.G.E.N 1.1b1 tree
    data/mugen.cfg
    data/mugen1/
    chars/
    stages/
    font/
    sound/
```

Then start the game with `-gamedir`:

```
ux0:app/MUGEN0001/eboot.bin -gamedir ux0:data/mugen
```

That flag cannot be passed from the LiveArea bubble, so for a normal launch
either set `gamedir` in a small launcher or use option A. The engine also looks
for its files relative to the current directory, so a `data/` folder next to
`eboot.bin` works too.

---

## Renderer

The engine defaults to `rendermode = system` (software), which draws to a
16-bit SDL surface in main memory and blits it to the screen once a frame. On
the Vita that means the CPU writes the whole frame and SceDisplay copies it.
`rendermode = opengl` goes through vitaGL/SceGxm instead.

Edit `data/mugen.cfg` in the game tree:

```ini
[Video]
Width  = 960
Height = 544
Depth  = 32
RenderMode = opengl
blitmode   = hardware
```

`960x544` is the native output/render default, set by `[Config]`
`GameWidth/GameHeight` and `[Video]` `Width/Height` in
`port/psvita/mugen.psvita.cfg`. For an explicit smaller software target, change
both pairs in that file (e.g. `480x272`) and rebuild. Native resolution
performance must be measured on hardware; the successful game run reported
below used 480x272.

**Do not set `vretrace` too high.** The engine's frame pacing assumes a 60 Hz
display; the Vita panel is 60 Hz, so the default is right.

---

## Controls

The engine reads SDL 1.2 keyboard, joystick and — on Windows — DirectInput. On
the Vita, SDL 1.2's `SDL_JOYSTICK_VITA` driver exposes the physical buttons as
a single joystick (12 buttons, 4 axes, 0 hats — the d-pad is buttons, not a
hat). The engine reads bindings from the `[P1 Joystick]` / `[P2 Joystick]`
groups, gated by `P1.UseKeyboard` and `P1.Joystick.Type` — there is no
`P1.Keyboard`, `P1.Joystick` or `P1.Joy.Bn` key, so the engine ignores an
`[Input]` block written that way.

The values are also **engine button codes, not SDL indices**: `hat_base = 4*2 =
8`, so Triangle (raw 0) arrives as code 9. The C code in `src/config.c`
installs the correct codes for this pad on startup and therefore overwrites
whatever is saved, which means a remap written to the file does not survive
the next launch. Until that precedence is fixed, treat the startup override
as the binding of record (see `port/psvita/PLAN.md` M2). Use the hardware
test in `tools/test_vita_pad.sh` — which asserts all 11 delivered codes —
rather than trusting the raw indices from the input mapper.

---

## Fidelity: what differs from the Windows build, and why

This project's rule is that behaviour is the product, bugs included. The Vita
target cannot reproduce all of it, because the hardware differs in ways the
original code never had to consider. Every difference below is deliberate and
marked in the source.

### Floating point: no x87

The Windows build pins x87 semantics (`/arch:IA32`, `-mfpmath=387
-fexcess-precision=standard`) so that intermediates stay 53-bit and round on
assignment to `float`, exactly as MSVC 2008 did. **ARMv7 has no x87.** VFP/NEON
round every operation correctly, so `FLT_EVAL_METHOD` is 0 and float results
drift from the reference.

This is the same situation as the `linux-arm64` target (see `PLANS.md`), and
the consequence is the same: `mugen.log` is **not** byte-identical to the
original on the numeric lines. It is identical on the structural ones (state
transitions, triggers, log ordering). Comparing logs is still useful; an exact
diff is not the acceptance criterion here.

### `mugen.log` is still written

The engine writes `mugen.log` next to the current working directory. On the
Vita that is `ux0:app/MUGEN0001/` for a LiveArea launch, and `ux0:data/mugen.log`
when that directory is not writable. That is the single most useful debugging
aid this port has: the same log the Windows build produces, reachable through
VitaShell.

Every line carries the local wall clock (`[12:34:56] ...`) and the header records
the session's date, which the Windows build does not do: it is the only way to
line a crash up with what was being done, and this port has no log-comparison
workflow to protect. The switch is `[Debug] TimeStamps` in `data/mugen.cfg` — the
Vita template sets it to 1, the desktop builds leave it off so `mugen.log` stays
diffable against the original (`TimeStamps = 1` turns it on there too). Because
the packed `data/mugen.cfg` is the Vita template, this is the file the build
ships; the value read at start-up applies from the moment it is loaded.

### SDL_mixer without `Mix_QuerySpecEx`

The original linked Elecbyte's own SDL_mixer, which exported `Mix_QuerySpecEx`
(reporting the chunk size). VitaSDK's stock SDL_mixer 1.2.12 does not.
`src/sdlmixercompat.c` supplies it, exactly as the static Windows build does.

### BGM plugins: none load

`src/bgmplugins.c` loads `libmpg123-0.dll` and friends with `LoadLibraryA` /
`GetProcAddress`. The Vita has no dynamic loader for homebrew
(`SDL_config_vita.h` sets `SDL_LOADSO_DISABLED`), so
`src/compat_linux.c` returns a NULL handle for every load.

That is the truthful answer, not a silent failure: the call sites already treat
a NULL handle as "plugin not installed", so the log reads exactly like a
missing DLL on Windows.

What actually plays through this path is narrower than the codec list suggests.
`Sdlmix_Open` (`src/bgmplugins.c`) routes only tracker and MIDI files through
SDL_mixer and explicitly refuses WAV, OGG and MP3 before the mixer is even
reached -- linking libmikmod/FLAC/vorbis/mpg123 does not wire those formats
into the engine. So: MIDI/MOD-style files are the supported BGM route here
(pending on-device verification), WAV/OGG/MP3 through `sdlmix` are not, and
a missing codec library is a different failure from a deliberately refused
format. See `port/psvita/PLAN.md` M4 and scenario A03 for the format-by-format
status to test.

### GLU

`src/video.c` uses exactly one GLU function, `gluOrtho2D`, and vitaGL is GL
only. It is defined inline in `video.c` under `MUGEN_VITA_GL` as GLU defines it
(`glOrtho(l, r, b, t, -1, 1)`), so GLU is not a dependency.

### GL entry points vitaGL lacks

vitaGL implements GL 1.x/2.1 but not three entry points the engine calls.
`port/psvita/src/vita_gl_shim.c` supplies them:

| Call | Handling | Effect |
|---|---|---|
| `glMultiTexCoord4f` | forwards `(s, t)` to `glMultiTexCoord2f` | Q coordinates dropped |
| `glBlendColor` | records the value, does nothing | constant-alpha blend lost |
| `glIsShader` | tracks handles from `glCreateShader` | correct |
| `glLogicOp` | no-op | unreachable; the probe reports the extension absent |

**`glBlendColor` is the one visible loss.** `Gl_DrawSprite` sets
`glBlendFunc(GL_ONE, GL_CONSTANT_ALPHA)` and then `glBlendColor(...)` for
software-blended sprites. With no constant-alpha source on vitaGL those sprites
blend additively instead of by their alpha. It is a rendering difference, not a
gameplay one — state, damage, timers and hit detection are untouched.

The capability probe (`VitaGl_Probe`) reads `GL_VERSION` and `GL_EXTENSIONS`
from `glGetString`, exactly as GLEW did, so if a future vitaGL gains
`EXT_blend_logic_op` or `ARB_fragment_program` the engine picks it up with no
code change.

Two GL *constants* also needed adapting, and `vita_gl_shim.h` carries both
(guarded `#ifndef`, so they are inert wherever the real header defines them):
`GL_FRAMEBUFFER_BINDING_EXT` (the desktop glad set's name for `0x8CA6`, which
vitaGL and GLES call `GL_FRAMEBUFFER_BINDING`) and `GL_FRAMEBUFFER_UNSUPPORTED`
(`0x8CDD`, which `vitaGL.h` does not define). Without them `Gl_AttachFramebuffer`
and the FBO probe do not compile on this target, which is how they were found.

### Where the port's code lives

The engine sources are guarded with `MUGEN_VITA_GL` and are otherwise
unchanged. The port's own code is `port/psvita/src/`; `src/mugen.h` includes
`vita_gl_shim.h` in the Vita branch so `src/symbols.h` can map the original's
`g_glew*` names onto it. The Windows and Linux builds never see any of it.

---

## Layout

```
port/psvita/
  build.sh                installs VitaSDK if needed, then builds (see above)
  CMakeLists.txt          the build
  README.md               this file
  src/vita_gl_shim.c      GL capability probe + the entry points vitaGL lacks
  src/vita_gl_shim.h
  cmake/Icon.cmake        derives sce_sys/icon0.png from res/icons/*.ico
  sce_sys/                LiveArea assets (from the VitaSDK sample; MIT/zlib)
    icon0.png             the fallback: replaced by the generated one
    livearea/contents/bg.png
    livearea/contents/startup.png
    livearea/contents/template.xml
  ../../src/              the engine (shared with every other target)
  ../../third_party/      lua-5.1.4, minizip-0.15, libresample (vendored)
```

`sce_sys/` is the VitaSDK sample's artwork. Replace it with your own before
distributing a custom bubble — it is not this project's art.

**The bubble icon** is the exception: it comes from `res/icons/*.ico`, the same
artwork the Windows build embeds, so both platforms show one picture. At
configure time `cmake/Icon.cmake` runs `tools/iconpack.py png` to turn
`res/icons/1.ico` (64x64) into the 128x128 `sce_sys/icon0.png` the Vita wants,
writing it into the build tree — the committed file above is left untouched and
is only packed when the conversion cannot run (no Python3, or a missing `.ico`,
both of which are reported at configure time). The conversion uses the Python
standard library only; no imaging package is needed, and it runs on the host, so
cross-compiling is unaffected. Pick a different source with
`-DMUGEN_VITA_ICON_ICO=...`.

`tools/test_vita_icon.sh` checks all of this without a VitaSDK: it runs the
module through `cmake -P` and verifies the PNG's size, alpha and artwork, plus
both fallbacks.

---

## Diagnosing a failure on the console

The Vita reports almost everything as a generic error code and writes nothing
anywhere you can read. Work through these in order — each step narrows the
fault to a different layer.

### 1. Read the error code

| Code | Layer | Meaning |
|---|---|---|
| **C1-2609-7** | module load | The kernel refused the SELF **before any of the game ran**. The eboot itself is malformed or asks for something it is not allowed. Nothing in `mugen.log` will exist. |
| **C2-12828-1** | runtime (or earlier) | Crashed or aborted. A `mugen.log` means it reached `main`; **no** log means it died in C++ static init, which points at the newlib heap. Either way a `psp2dmp` is written. |
| **C1-6703-6** | signature / install | The VPK is corrupt or the title ID clashes with an installed app. Delete the bubble and reinstall. |
| **C0-9250-6** | not enough space | Free space on `ux0:`. |

Both of these were hit while bringing this port up, so both are documented
with the fix that resolved them:

* `C1-2609-7` — an out-of-range `vita-make-fself -m` memory budget.
* `C2-12828-1` — the newlib heap larger than the process budget, so every
  allocation failed. Fixed by defining `_newlib_heap_size_user`.

The `C2-12828-1` was diagnosed from two core dumps, and the cause is worth
recording in full because it looks like nothing to do with memory.

**Both dumps had the identical fault address** (`_kill_r`, newlib's `kill()`,
i.e. the `abort()` path). Account for the module's randomised load base — it was
`0x81015000` in one run and `0x81061000` in the other, so raw addresses never
compare directly; rebase by `MODULE_INFO`'s segment base before believing any
address in a dump. With that done the backtrace was:

```
abort
__gnu_cxx::__verbose_terminate_handler
__gxx_personality_v0
std::unexpected / __cxa_allocate_exception / __cxa_rethrow
_GLOBAL__sub_I__ZN10expression4ptokE   <- a C++ static constructor
__libc_init_array
_start
```

So the process aborted in **C++ static initialisation, before `main`** — which
is why no `mugen.log` was ever written. The constructor belongs to
`expression.o` in `libvitaGL.a` (its GLSL expression parser) and its first act
is `operator new`, so the throw was `std::bad_alloc`.

Why it was out of memory: VitaSDK's crt0 sizes the newlib heap from the *weak*
symbol `_newlib_heap_size_user` and **defaults to 128 MiB** when it is
undefined. `vita-make-fself -m` caps the whole process budget at `0x12800` KiB
(74 MiB), so `sceKernelAllocMemBlock(128 MiB)` fails; crt0 then zeroes
`_newlib_heap_size`, and from that point every allocation returns NULL. The
first allocation in the program happens to be that C++ constructor's.

`port/psvita/src/vita_newlib.c` defines `_newlib_heap_size_user` so the heap
fits inside the budget, and `MUGEN_VITA_HEAP_MIB` (default 64 MiB) sets it.
The diagnosis above describes the old system-mode configuration
(`MUGEN_VITA_MEMSIZE=0x12000`, 74 MiB hard cap), not the current default:
`MUGEN_VITA_MEMSIZE=0` means "normal app, let the system size it", which is
what the VitaSDK samples do. Passing an explicit `0x12000` capped the process
at a system-mode budget and `sceGxmInitialize` then failed with
`SCE_GXM_ERROR_OUT_OF_MEMORY` — a graphics error that is really a memory one.
CMake refuses to configure if the heap would not fit, because the failure is
otherwise silent at build time.

An earlier revision of this port raised the *budget* to 128 MiB instead, which
is out of `vita-make-fself`'s accepted range and produced `C1-2609-7` at module
load. The two errors are two halves of the same constraint: **budget ≤ 74 MiB,
and heap comfortably inside the budget.**

### Why "Can't change video mode" was really an *audio* problem

A third core dump still showed `C2-12828-1` with no `mugen.log`. The stack was
`Error_Exit` -> `Error_ShowDialog` -> `GameVars_Free` -> `Match_FreeAssets`, and
`Error_Exit` builds its message on the stack — which the dump includes. Reading
it out of the dump gave the actual text:

```
Library (built in) message: Can't change video mode.
```

Three separate faults were stacked on top of each other:

1. **The teardown crashed, hiding the error.** `GameVars_Free` -> `Match_FreeAssets`
   dereferences `g_game->palgroups`, which only `GameVars_Init` allocates — and
   `GameVars_Init` runs *after* video and font setup. Any start-up failure
   reached `Error_Exit` with `palgroups` still NULL and died in the cleanup
   instead of reporting. On the Vita, `Error_Exit` now skips the teardown (the
   process is about to `exit(1)` and the OS reclaims it); Windows and Linux keep
   the original order.

2. **The error was invisible.** There is no console and no readable stderr on the
   Vita, so `Error_ShowDialog`'s stderr fallback went nowhere. It now calls
   `SDL_VITA_ShowMessageBox`, so start-up failures appear on screen. Relatedly,
   `Log_Init` falls back to `ux0:data/mugen.log` when the launch directory is not
   writable — otherwise the log silently never existed and *every* diagnostic
   vanished with it.

3. **The video mode failed because audio was initialised first.** SDL 1.2's
   `SDL_Init` walks TIMER, AUDIO, VIDEO, CDROM, JOYSTICK in that order and
   returns `-1` at the first failure — so if `SDL_AudioInit` fails, the video
   subsystem is never initialised, and `SDL_SetVideoMode` then fails as though
   the problem were graphics. The engine ignores `SDL_Init`'s return value (as
   the original did). The Vita build now requests video and audio separately, so
   audio cannot take video down with it.

4. **The process memory budget was in the wrong bracket.** `vita-make-fself
   --help` reads *"Normal app: 0, System mode app: 0x1000 - 0x12800"*. That
   range is the **system-mode** bracket, not a ceiling for a normal app — 0
   means "let the system size it", which is what the VitaSDK samples do. Passing
   `0x12000` capped the process at a system-mode budget, and `sceGxmInitialize`
   then failed with `SCE_GXM_ERROR_OUT_OF_MEMORY` (`0x805B0002`). That surfaces
   as `gxm init failed: -2141519870` and, because SDL's Vita video driver reports
   a GXM shortfall as a failed video mode, as **"Can't change video mode"** —
   a graphics error that is really a memory one. The default is now 0 (no `-m`
   at all); an explicit budget is still accepted via `MUGEN_VITA_MEMSIZE`.

`Video_SetMode` also degrades instead of giving up: if the first
`SDL_SetVideoMode` is refused it retries without `SDL_DOUBLEBUF`, then without
`SDL_HWSURFACE|SDL_HWACCEL`, then without `SDL_FULLSCREEN`, logging the driver's
reason for each rejection and the flag set it finally accepted. The Vita's SDL
GL driver accepts a narrower set of surface flags than the desktop drivers the
engine was written for. The log from the failing run shows this working:

```
video: 960x544x32 flags 0xC0000103 refused (gxm init failed: -2141519870)
video: flags 0x80000103 refused (gxm init failed: -2141519870)
video: flags 0x80000002 refused (gxm init failed: -2141519870)
video: flags 0x2 refused (gxm init failed: -2141519870)
```

Every retry carried the same underlying GXM error, which is what pointed at
memory rather than at the flags.

5. **VitaSDK's SDL 1.2 has no OpenGL.** With the memory fixed, the log reached:

   ```
   Initializing graphics...trying 960x544x32 mode 0x0...failed.
   ```

   No surface refusal at all — `SDL_SetVideoMode` succeeded — so the failure was
   `Gl_Init`. SDL's Vita video driver (`SDL_vitavideo.o`) is a SceGxm 2D blitter:
   it references no GL context API and never calls `vglInit`. `SDL_OPENGL`
   therefore returns a surface with **no context**, `glGetString` (resolved to
   vitaGL) reports nothing, and the GL renderer cannot start. This SDL cannot
   support the OpenGL renderer at all — it is not a configuration problem.

   So the port now:
   * ships `RenderMode = system` in the packed `mugen.cfg`, which needs only a
     surface to blit to — exactly what that driver provides;
   * **falls back** from `opengl`/`openglscreen` to `system` automatically if a
     GL renderer fails, so a config asking for OpenGL still starts the game;
   * logs the probe result to `mugen.log` (`GL: "…"`, `GL: flags …`), since
     `Gl_Init`'s own diagnostics went to the match log, which is only written
     with `-log` — unreachable from a LiveArea launch.

   The software renderer draws into a 16-bit surface and lets SDL's GXM backend
   present it. It is the same renderer the original used by default, so nothing
   about the game's behaviour changes; it is only slower than GL would have been.

6. **The software renderer drew into memory that was never shown.** With
   `RenderMode = system` the log ran all the way to the end —

   ```
   Entering gameflow loop ... Entering mode select. ... Game loop init
   Mode select init
   ```

   — and the panel stayed black, with no crash. The cause was `SDL_HWSURFACE`:
   the software renderer writes straight into `surface->pixels` and **never
   calls `SDL_LockSurface`** (there is no lock anywhere in `src/`, that is how
   the original works). SDL 1.2 requires a hardware surface to be locked before
   its pixels are touched, and on the Vita's SceGxm backend a hardware surface
   keeps them in a texture that only locking maps out. So every write went to
   memory nothing presented. The Vita build now asks for `SDL_SWSURFACE`, whose
   pixels are ordinary memory that `SDL_UpdateRect` uploads.

   The build also logs what the driver returned, which is what distinguishes a
   working surface from a silent black one:

   ```
   video: surface 960x544 16bit flags 0x… [pixels=NULL]
   ```

7. **The driver hands back a double-buffered surface, so the engine never
   presented a frame.** The surface diagnostic added in the previous round
   answered it outright:

   ```
   video: surface 960x544 16bit flags 0xC0000020
   ```

   `0xC0000020` is `SDL_FULLSCREEN|SDL_DOUBLEBUF|SDL_NOFRAME` — and no
   `SDL_HWSURFACE`, so the software-pixels fix worked (`SDL_MUSTLOCK` is false
   for that flag set). But the surface is **double buffered even though
   `SDL_DOUBLEBUF` was never requested**. SDL 1.2 only presents a double-buffered
   surface on `SDL_Flip`; `SDL_UpdateRect` just marks the shadow buffer dirty.

   The engine flips only when `VIDEO_HWBLIT` is set (`BlitMode = PageFlip`, or
   the OpenGL renderers), and the System renderer does not set it. Every frame
   therefore went into the back buffer and was never shown — the log ran to
   completion, the loop paced at 60 fps, the panel stayed black.

   `Gfx_Present` now flips whenever the driver *actually* returned double
   buffering, rather than trusting the flags the engine asked for, and stays out
   of the way when `VIDEO_HWBLIT` is set and the generic path already flips.

8. **The gamepad could not drive the menus at all.** M.U.G.E.N's system
   screens are keyboard-only: joystick input goes through `Pads_HandleEvent`
   into pad input codes (`Input_TranslateJoy` adds `INPUT_PAD_BASE`), which only
   the fight engine reads. The menus read `screen->key`, which comes from the
   keyboard queue. On a desktop there is always an F1, an Escape and arrow keys;
   on the Vita there are none, so the first-run info box ("Press F1 to
   continue.") could not be dismissed and the game sat there forever.

   `src/events.c` now translates pad events into keyboard events on the Vita.
   The indices are **read out of the driver**, not guessed: SDL's Vita joystick
   driver (`SDL_sysjoystick.o` in VitaSDK's `libSDL.a`) reports **12 buttons,
   4 axes and no hats at all** — the d-pad arrives as ordinary buttons — and its
   update function maps `SCE_CTRL` bits like this:

   | idx | bit | button | idx | bit | button |
   |---|---|---|---|---|---|
   | 0 | 0x1000 | Triangle | 6 | 0x40 | Down |
   | 1 | 0x2000 | Circle | 7 | 0x80 | Left |
   | 2 | 0x4000 | Cross | 8 | 0x10 | Up |
   | 3 | 0x8000 | Square | 9 | 0x20 | Right |
   | 4 | 0x100 | L | 10 | 0x1 | Select |
   | 5 | 0x200 | R | 11 | 0x8 | Start |

   Bound to keys, following the console's own convention as set on the device —
   **Cross = cancel/back, Circle = confirm**:

   | Input | Key |
   |---|---|
   | Circle, Start | `Return` (confirm) |
   | Cross, Select | `Escape` (cancel / back) |
   | d-pad, left analog stick | arrow keys |
   | Triangle | `F1` (info box, as on the keyboard) |
   | Square, L, R | unbound — M.U.G.E.N has no menu function for them |

   The analog stick is axis 0 (X) and 1 (Y) with a `0x4000` deadzone, edge
   triggered so holding it does not repeat. The right stick (axes 2, 3) is
   deliberately left alone.

   `port/psvita/input-mapper/` is a separate small app that displays the live
   button/axis/hat state, for confirming this on a given console or SDL build:
   `build/psvita/input-mapper/sdl-input-mapper.vpk`.

9. **The info box was sized for a 640x480 screen and clipped its own text.**
   `system.def` declares `localcoord = 320,240`, so at 960x544
   `motif_scale = 3.0` and all motif text is drawn at 3x. The info box, though,
   used fixed pixel constants (`600x400`) from the original's 640x480 target,
   where 600 spanned 94% of the width. At 960 it spanned 62% while the text
   inside was still 3x, so lines ran out of room and were cut at the right edge.
   The Vita build keeps the original 600/640 and 400/480 proportions and centres
   the box, which is the width the motif was designed for.

One packing detail learned here: `install/` doubles as the place this project's
own test executables get copied, so a stale `mugen-msvc-win32-sdl1.exe` (a 1.7 MB
Windows PE) was being packed into the VPK. Those build outputs are now excluded
by name; the tree itself is untouched.

`C1-2609-7`'s usual causes are all in the SELF header, not in the game:

* an `-m` memory budget outside the accepted `0x1000`–`0x12800` KiB range;
* a "safe" module trying to use restricted APIs (`SceGxm`, taiHEN);
* a SELF built for a newer firmware than the console runs;
* a truncated `eboot.bin` (an incomplete copy to the memory card).

### 2. Check the SELF's own header on the host

Before blaming the console, look at what was built:

```sh
# the memory budget and safety flag the SELF was built with
grep vita-make-fself build/psvita/build.ninja

# an unsafe module must NOT carry -s
#   -m should be ABSENT (normal app); an explicit 0x1000..0x12800 means system mode
```

`vita-make-fself --help` reads *"Normal app: 0, System mode app: 0x1000 -
0x12800"*. **0 is the normal case** and the flag should be omitted entirely;
a value outside the system-mode range is rejected by the kernel at load
(`C1-2609-7`), and a value inside it caps the process so tightly that
`sceGxmInitialize` fails with `SCE_GXM_ERROR_OUT_OF_MEMORY` — which the video
driver reports as "Can't change video mode".

Confirm the segment layout is sane (two `LOAD` segments, entry point in the
first):

```sh
arm-vita-eabi-readelf -l build/psvita/mugen
```

### 3. Get the module load error from the kernel

This is the decisive step, and it needs `taiHEN`. With
[`vita-taihen-kuio`](https://github.com/devnoname120/vita-taihen-kuio) or
the `kuio` sample installed, the kernel log shows the actual
`sceKernelLoadStartModule` failure instead of the shell's generic code.

Simpler, and enough for most cases: install **`VitaShell`** and check *both*
`ux0:data/mugen.log` and `ux0:app/MUGEN0001/mugen.log`. If the app started at
all it will have written `mugen.log` next to its eboot; an absent log *usually*
means it never reached `main()`, i.e. a module-load failure rather than a game
crash. Three exceptions are recorded rather than assumed: (1) logging itself
can fail (unwritable directory, logging initialised after the crash site); the
build falls back to `ux0:data/mugen.log` for exactly this reason, so check
there before concluding anything; (2) an immediate hardware exception can kill
the process before the first flush; (3) a crash in `Error_Exit`'s teardown can
destroy evidence of an earlier failure (see "Why Can't change video mode was
really an audio problem" above). So: no log in *either* location *and* no core
dump = likely never reached `main()`; anything else needs the table below
and, for `C2-12828-1`, the dump in step 5.

### 4. Once it runs: read `mugen.log`

`mugen.log` is the single most useful thing this port produces. It is the same
log the Windows build writes, and the engine is verbose — it names the exact
step it died in:

| Last line in `mugen.log` | Meaning |
|---|---|
| `Parsing command line...` then nothing | Crashing in the first few instructions; suspect the toolchain or a bad link. |
| `Error reading data/mugen.cfg` | The game tree is missing. The VPK was built without `MUGEN_VITA_DATA_DIR`, or it was installed to the wrong place. |
| `Initializing graphics...` then a `Video: SDL_SetVideoMode` line | The requested resolution was refused; the log lists each flag combination tried and SDL's reason. Check `GameWidth`/`GameHeight` in the packed `mugen.cfg`. |
| `Initializing graphics...` then nothing at all | Older builds without the Vita NULL-surface check: the same resolution problem, crashing before it could log. |
| `GL: OpenGL version is below 2.0` | vitaGL did not initialise — check that `libvitaGL.a` was the version the SDK expects. |
| `Initializing sound...` then a crash | Audio device problem; run once with `-nosound` to confirm. |
| `Entering gameflow loop` | The engine reached its main loop. Failures here are *often* game-data problems, but not by rule: the standing-preview crash under investigation in `ISSUES.md` reproduces after this line. Treat it as "the port got this far", not as "everything after is data". |

To narrow a crash down, the command line options work as on any other target
(pass them via a launcher, since the LiveArea bubble takes none):
`-nosound`, `-nomusic`, `-p1 kfm -p2 kfm`, `-log mugen-match.log`.

### 5. Get a usable backtrace

On a `C2-12828-1` the Vita writes a core dump to `ux0:data/`, named like
`psp2core-<timestamp>-<addr>-eboot.bin.psp2dmp`. It holds the faulting
registers and the module list.

`STRIPPED` is deliberately not set in this build, so the SELF keeps its
symbols and any address from the dump or a log resolves to a function:

```sh
# copy the dump off the console first (VitaShell: mark it, Triangle -> Copy)
arm-vita-eabi-addr2line -f -C -e build/psvita/mugen 0x81012345
```

`build/psvita/mugen` is the unstripped ARM ELF the SELF was made from — it must
be the one from the same build, or the addresses will not line up. If you
enable `STRIPPED` for a size-conscious release build, keep an unstripped
`mugen` around for exactly this.

The dump is worth keeping: a `mugen.log` tells you which *step* died, the PC in
the dump tells you which *line*.

## Troubleshooting

### Windows converter crash during rebuild

`FAILED ... vita-elf-create.exe` followed by `Exit code 0xc0000374` is a
Windows host-tool heap-corruption failure during ELF-to-VELF conversion. The
engine has already compiled and linked; SELF/VPK creation has not completed.
An older VPK still in the output directory is not evidence of a successful
rebuild. This failure is separate from a Vita runtime crash.

The Vita target now passes `-Wl,--defsym=__sce_headroom=0x10000`, the supported
VitaSDK workaround for an executable LOAD segment ending too close to the
next segment (vita-toolchain issue #303). With Python present, the build also
checks the linked ELF's actual gap before starting the converter. This guards
the layout; it does not establish that the Windows heap failure was caused by
overlapping segments. The exact failing Windows ELF was not supplied, and the
Windows converter crash has not been reproduced here.

To build the OpenGL configuration from MSYS2/Git Bash:

```sh
git pull --ff-only
bash port/psvita/build.sh
```

The renderer and precache are not flags: they are `[Video] RenderMode = OpenGL`
and `[Misc] Precache = 0` in `port/psvita/mugen.psvita.cfg`, and the configure
log prints what it read from that file (`opengl 960x544, game 960x544,
FullScreen 1, Precache 0`). Edit the file for `system` (the software SceGxm
path) or to opt into `Precache = 1`; the separate worker stack fix is not part
of this.

If the converter still fails, keep `build/psvita/mugen` from **that failed
build** and capture a verbose converter run, from the repository root in
MSYS2/Git Bash:

```sh
C:/vitasdk/bin/vita-elf-create.exe -vv build/psvita/mugen build/psvita/diagnostic.velf > build/psvita/vita-elf-create.log 2>&1
```

Those two files are needed to investigate the Windows allocation failure. The
rebuild script preserves a nonzero exit and points to the ELF on failure.

The supplied warning about `BGM_THREAD_STORE` was a separate real overread:
Vita's `pthread_t` is 4 bytes but the reference Windows handle is 8. POSIX
targets now use the native pthread types regardless of pointer width; 32-bit
Windows retains the reference ABI and Android retains SDL threads. The ctype
warnings shown in the log are fixed by converting table arguments to
`unsigned char`, rather than suppressing the warning. Unused Vita locals and
an obsolete extension helper were removed.

Validation: a full ARM ELF/VELF/SELF/VPK cross build on Linux with
`-Werror=char-subscripts -Werror=array-bounds`; the linked `g_bgmThread` is
4 bytes and the metadata gap is checked. Host build-argument, ELF-check,
warning-regression, CNS ABI and Vita-video tests pass. This does not substitute
for running the Windows-hosted converter on the original failing ELF.

### Other diagnostics

| Symptom | Cause and fix |
|---|---|
| **`Error 0x8010113D`** installing the VPK | `ScePromoterUtil` refused the package (`0x801011xx`; the console may show it as `C2-12691-9`). First suspect is `sce_sys/` artwork that is not 8-bit indexed — see "The bubble artwork"; then the ordered checks in "Verify the copy before installing" (copy size/SHA-256, free space on `ux0:`, a leftover `ux0:app/MUGEN0001/`, installing from `ux0:` rather than `uma0:`) |
| `VITASDK is not set` | `eval "$(port/psvita/build.sh --env)"`, or pass `-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake` |
| `arm-vita-eabi/include/SDL/SDL.h not found` | VitaSDK's `sdl` package is missing: `port/psvita/build.sh` reinstalls it with `vdpm`, or by hand `vdpm install sdl` |
| `libvitaGL.a not found` | `vdpm install vitaGL` (not part of a hand-made base SDK) |
| `libvitashark.a / libSceShaccCgExt.a / libmathneon.a not found` | Same package — `vdpm install vitaGL` pulls them in |
| `Could NOT find PkgConfig` | No host `pkg-config` on `PATH`; install one or pass `-DPKG_CONFIG_EXECUTABLE=` |
| `could not find package 'SDL_ttf'` | The generated `libpng.pc` shell was not reached — check the `.pc dirs` line in the configure output |
| **C1-2609-7** on launch, no `mugen.log` | Module load refused. Most likely `MUGEN_VITA_MEMSIZE` is outside `0x1000`–`0x12800` KiB, or the build is "safe". See "Diagnosing a failure on the console" |
| **C2-12828-1** on launch, **no** `mugen.log` | Died before `main`, in C++ static init — almost always the newlib heap not fitting the process budget. Check `MUGEN_VITA_HEAP_MIB` vs `MUGEN_VITA_MEMSIZE` |
| **C2-12828-1** with a `mugen.log` | Runtime crash after `main`; the log ends at the failing step. If it stops at `Initializing graphics`, see the `Video:` lines it logged |
| Installs, then exits at start | The game tree is missing. Check `mugen.log` for `Error reading data/mugen.cfg` |
| Black screen, log says `Could not set video mode` | `rendermode` is not `opengl`, or `libvitaGL.a` failed to initialise — check the GL lines in `mugen.log` |
| `GL: OpenGL version is below 2.0` | vitaGL did not initialise. Version 0.0.0.r1488 is what this port was built against |
| Blended sprites look too bright | The `glBlendColor` loss described above |

---

## Measuring the frame rate

Two independent readouts, because neither is sufficient alone.

### On screen

`[Debug] ShowFPS = 1` in `mugen.cfg` draws the engine's own counter in the top
right corner (`Game_DrawFps`, which shows `Timer_GetFps()`).

The Vita build patches this into the packed config automatically. It had to be
implemented first: the source comment on `Game_DrawFps` named a `[Debug] ShowFPS`
option that **was never read anywhere** — the counter could only be turned on by
the console's `fps` command, and the console opens on a backquote key
(`console.c` keybind table). With no keyboard on the console, the frame rate was
unmeasurable. `src/config.c` now reads the option.

### In `mugen.log`

Every 10 seconds the port appends:

```
fps: 58.4  (tick 59/s, drawn 58/s)
fps: 58.9  (tick 59/s, drawn 58/s)
```

from `port/psvita/src/vita_fps.c`. Three numbers, because they answer different
questions:

| Field | Meaning |
|---|---|
| `fps` | `Timer_GetFps()`, the engine's own figure — the same number shown on screen |
| `tick` | calls to `Screen_EndFrame` per second: how often game logic advanced |
| `drawn` | frames actually presented per second |

`drawn` below `tick` is the engine reacting to being slow, not a port fault:
when it is more than one frame late it sets `skip_draw` and skips presenting
(`Frame_UpdateSkip`). A game running at 30 `tick`/30 `drawn` is CPU-bound; 60
`tick` but 30 `drawn` would mean presenting is the bottleneck.

The first window covers start-up and loading, so it is labelled
`warmup window` — a low number there is expected. Reports stop after 120
windows (20 minutes) so a long session does not bury the rest of the log.

This is the number to quote when reporting a performance problem. The 10-second
window is deliberate: a shorter one is dominated by loading hitches and menu
transitions that say nothing about match performance.

## The OpenGL renderer — native vitaGL display and shader quads

`RenderMode = OpenGL` now uses a display owned by the engine through
`port/psvita/src/vita_video.c`. SDL remains responsible for events, input,
audio and the System renderer. It does not create or swap the OpenGL display.
The default remains System until the new path has been checked on hardware.

The supplied `mugen(2).log` stopped just after the FBO probe. Its matching
core dump (`psp2core-1794169543-0x0001372211-eboot.bin.psp2dmp`) has a data
abort in `SceGxm` with `r0 = 0`, fault address `0x1f0`, and a return address
in the viewport path. SDL had supplied a surface, but vitaGL had never been
initialized. `glGetString` and an FBO completeness result were not proof of a
live GXM context.

### Implementation

| Component | Current path |
|---|---|
| Context | `vglInitWithCustomSizes` before any GL probe or viewport call; no `SDL_SetVideoMode` in the GL path |
| Splash handoff | Start the first scene with `glClear`, then present black once before loading engine resources |
| Memory | 16 MiB RAM and 64 MiB CDRAM pools; no MSAA; existing newlib heap unchanged |
| Display size | Requested config dimensions, limited to 960x544; existing defaults unchanged |
| Present and vsync | `vglSwapBuffers` and `vglWaitVblankStart`, including debug/vsync call sites |
| Shader quads | Explicit `aPosition` and four-component `aTexCoord`, submitted with `glDrawArrays` |
| Transform | `uMvp`, uploaded from the projection and modelview matrices; no optional vitaGL FFP shader built-ins |
| Palette | Ordinary nearest-filtered 2D texture, height 1, for both stored and streamed palettes |
| Projective Q | Preserved by the shared `GlQuad_Build`/`GlQuad_Interleave` helpers and the existing projective fragment sampling |
| Calibration | Desktop legacy-shader calibration is bypassed; pixel offsets are zero and index sampling is Standard |
| Shader errors | Explicit shader/program classification and info logs in `mugen.log`; interface validated after linking |

The programmable draw path matters: vitaGL's `glBegin`/`glEnd` uses its FFP
programs and ignores the engine's palette shader. Calling `vglInit` alone
would avoid the startup fault while still drawing sprites incorrectly.
Fixed-function primitives retain their existing path. The desktop renderer
retains its original calibration and immediate-mode sprite submission.

The native sprite path no longer needs `UNPURE_TEXFORMATS`,
`HAVE_FFP_SHADER_SUPPORT`, or `UNPURE_TEXCOORDS`: it supplies 2D palettes,
an explicit MVP, and programmable vertex attributes. The optional vendored
vitaGL build remains available; the prebuilt SDK library is sufficient for
these interfaces. The runtime shader compiler still needs to be available on
the device. A shader build failure must be diagnosed from the new log rather
than treated as a successful OpenGL initialization.

### Required on the Vita: the runtime shader compiler

The static SDK libraries used to build the VPK do **not** install the device's
shader compiler. vitaGL requires an extracted, decrypted `libshacccg.suprx`;
see [vitaGL's prerequisites](https://github.com/Rinnegatamante/vitaGL#prerequisites).
Check with VitaShell for `ur0:/data/libshacccg.suprx`. The linked library also
tries `ur0:/data/external/libshacccg.suprx` if the default path cannot load.
An encrypted, corrupt or incompatible file can fail even when the path exists.
The [upstream ShaRKBR33D installer](https://github.com/Rinnegatamante/ShaRKBR33D)
can extract/install the compiler. Restart the game after installation.

The supplied `mugen(3).log` confirms successful splash handoff, context creation
and FBO probing, then stops at shader loading. Its crash dump places a NULL
vertex program in `glLinkProgram`'s `sceGxmProgramGetParameterCount` call. No
`SceShaccCg` module is loaded in the dump, consistent with runtime compiler
load failure; the dump does not establish whether the file is absent or bad.

This also exposed an engine integration bug: vitaGL's default
`VGL_MODE_POSTPONED` reports shader compile success before compilation runs.
If the deferred compilation fails, the SDK linker can query a NULL GXM
program. The engine now selects `VGL_MODE_SHADER_PAIR` before each family,
compiles vertex then fragment, checks their actual status and GL errors, and
only creates/attaches/links a program after both succeed. Failed replacement
keeps the previous program. New logs identify the variant and shader stage,
and `GL_INVALID_OPERATION` includes the runtime compiler paths to check.

After native vitaGL takes display ownership, fatal errors are logged and exit
without opening SDL's message box: that box starts a separate GXM presenter.
The System renderer retains its existing error dialog. A compiler failure is
not a successful OpenGL startup and does not fall back to a second presenter.

### The compiler file exists, but startup still reports `0x502`

`mugen(6).log` still fails at the first vertex shader after the user reports
that `ur0:/data/libshacccg.suprx` exists. `0x502` is GL_INVALID_OPERATION;
vitaGL uses it when runtime compiler initialization fails and hides the native
module loader's result. Existence alone does not identify why loading failed.

The port now initializes vitaShaRK explicitly before building shader variants,
using the same vitaGL allocators and the public, idempotent `shark_init` API.
For each attempted path it records the file size, leading header information,
and the full native initialization result, for example:

```text
GL: compiler file ur0:/data/libshacccg.suprx: size=..., header bytes=16, read error=0
GL: compiler SCE header: sdk_type=0x...., header_type=0x....
GL: shark_init(ur0:/data/libshacccg.suprx) -> 0x........
```

If the primary path fails, the external path is also attempted. Both results
are retained in the final error: a missing fallback cannot hide a different
failure at the primary path. Failed initialization stops before any shader is
created, attached or linked. Successful initialization is reused; it does not
unload an existing module or change display ownership. An SCE header is not
proof of decryption or compatibility. A raw ELF header is diagnosed explicitly;
renaming `.elf` to `.suprx` does not create a loadable module.

For this case, retain the new `mugen.log`, particularly the `shark_init` lines.
The earlier generic `0x502` log cannot distinguish invalid contents, a loader
restriction or another native loading failure. This is a diagnostic improvement,
not proof that runtime compilation or on-device gameplay now succeeds.

### Hardware diagnostic: compiler loads, but `mul` conflicts with Cg

The standalone diagnostic log `VGL-DIAG-20261109-R1` from the Vita reports a
successful native load of `ur0:/data/libshacccg.suprx`, loaded module
`SceShaccCg`, successful minimal vertex/fragment compilation and linking, and
triangle readback `(25,229,51,255)`. The MUGEN indexed vertex also compiles.
Its fragment shader fails with `A value with the identifier 'mul' has already
been declared` and a prior builtin declaration. Both test runs reproduce it.
This establishes a shader-source problem in that run, rather than a missing
compiler file. It does not explain the earlier MUGEN executable's vertex
`0x502` failure by itself.

The Vita indexed and RGBA fragment shaders now name the PalFX multiplier
`uPalMul`; the `MUGEN_VITA_GL` uniform lookup uses that name. Desktop/GLES
shaders and uniform names stay as before. Rebuild the executable **and deploy
the updated Vita shaders together**: changing only the shader or only the
binding breaks PalFX. The PalFX arithmetic and original struct field remain
unchanged.

R1 also had a diagnostic bug: a successful kernel framebuffer query returned
an empty descriptor. It reported a false display failure and wrote a 54-byte,
0x0 BMP. R2 queries the owned vitaGL surface dimensions, takes the real buffer
from vitaGL's display callback, validates bounds/pitch before capture, and
tests all 18 indexed/RGBA shader variants with the engine's defines. R2 and
full MUGEN gameplay still require on-device verification; the R1 pixel success
does not qualify all rendering modes or shaders.

### R2 hardware result: perspective swizzles and display submission

The R2 log confirms the `uPalMul` correction: indexed variants 1–3 compile
and link, and the active PalFX variants expose the new uniform. Variant 4
fails with `'q' is not a valid swizzle component`. Cg does not accept that
GLSL texture-coordinate alias. The Vita fragment shaders now use `xyzw`
swizzles (`xy`, `x`, `y`, `w`) with the same coordinate and perspective maths.

R2's valid 640x480 BMP contains the status panel and green triangle, but the
user sees a black screen. Its `display=1` was **not** evidence of successful
scanout: it only observed vitaGL's CPU display callback, which runs before
`sceDisplaySetFrameBuf`, and the library ignored that API's return value.
VitaSDK documents native 960x544 and scaled 720x408, 640x368, 480x272 modes;
640x480 is not a documented scanout mode. A maximum-size check alone does
not validate the output mode.

The separate R3 diagnostic keeps a 640x480 logical canvas within native
960x544 output, logs actual display submission results, checks the active
framebuffer after submission, and probes/restores the old scanout request.
It does not change MUGEN's configured 480x272 output. Its native presentation
and all 18 corrected shader programs still need device verification. The
engine's owned-display path continues to use the configured output size;
arbitrary output sizes require a supported scanout plus a separate logical
render target, not merely passing the size through to vitaGL.

### Applying the R4 device tests to MUGEN

The user's `vita-gl-diag-r4.log` now establishes native compiler loading,
successful compilation/linking of all 18 MUGEN indexed/RGBA programs, valid
active 960x544 scanout, `(25,229,51,255)` triangle readback, continued frame
submission and automatic capture. That qualifies the isolated baseline, not
the full game renderer or gameplay.

The game now retains native load/submission tracing in the separate
`ux0:/data/mugen-vita-gl.log` journal (fallback: `ur0:/data/mugen-vita-gl.log`).
It records a compiled build marker and executing module path before normal
engine initialization, checks actual native presentation during GL creation,
and logs the real game's shader set as it is loaded. Active PalFX programs
must expose `uPalMul`; an incompatible deployed shader is rejected.

Enable the optional startup triangle/pixel test and actual game-frame capture:

```sh
port/psvita/build.sh --gl-selftest
```

Or configure with `-DMUGEN_VITA_BUILD_VITAGL=ON` and
`-DMUGEN_VITA_GL_SELFTEST=ON`. The script selects/fetches pinned vitaGL for this
mode; offline builds require the source to be present. The self-test is OFF
by default and the script resets that option on an ordinary build.

The smoke test runs once before `Gl_Init` establishes engine state. It checks
real eager compilation/linking, the green pixel and accepted native scanout,
then clears/releases its objects. Failure stops startup while preserving the
single GXM owner. The normal engine still owns frame timing and presentation;
there is no second diagnostic loop or overlay during gameplay.

After 60 calls from the real game presentation path, a self-test build saves
`ux0:/data/mugen-vita-frame60.bmp` from the active native framebuffer. Startup
swaps do not count as game frames. Invalid metadata or rejected native
presentation cannot produce a successful capture report. The screenshot
contains actual engine output (possibly still a splash/title screen), not the
smoke-test triangle. Frames 1/60 and periodic
submissions are journaled without synchronizing/logging every game frame.

Collect that BMP, the dedicated journal, and the normal `mugen.log`. The new
host tests inject native submission failure, shader failure and wrong pixels,
and verify that only the 60th game presentation captures the real buffer.
The initial native build/host checks did not establish device operation. The
subsequent supplied hardware results are recorded in the next section.

Integration verification (2026-11-09): the ARM/Vita game VPK built with the
self-test enabled; the diagnostic module also compiled with it disabled.
`test_vita_video.sh`, `test_vita_shader.sh`, `test_vita_diagnostics.sh` and
`test_gl_errors.sh` passed, including missing-active-uniform rejection and
preservation of the previous shader set. Diagnostic-module tests also passed
ASan/UBSan. The packaged SELF contains the integration marker, has the same
UNSAFE authid as the working R4 app, and includes the matching four Vita shaders.

### FPS display

The Vita template explicitly enables `[Debug] ShowFPS = 1`; the packager
writes one canonical key in that group and validates the parsed value.
Lowercase keys, compact spacing, indentation and CRLF are accepted. A
`ShowFPS` key in another group cannot suppress the `[Debug]` entry. `DrawFPS` is not read by this
engine. Use `Debug = 0` for the normal FPS-only overlay. `ShowFPS` also enables
`perf:` log lines, but use the match's `Avg frame rate` and `Skipped frames`
when assessing delivered rendering performance.

### Native output and an optional bundled compiler

The supplied integrated game logs establish successful compiler loading,
`SMOKE RESULT PASS`, all 18 real shader programs, accepted presentation through
frame 1800, a 480x272 frame-60 capture of the mode menu, and successful program
termination after 1896 frames. They establish startup/menu/character-select
and match operation: 726 match frames in 12.12 seconds, reported average game
rate 59.90 and average drawn frame rate 59.74. Native 960x544 output and an
app-local compiler load still require their own device run.

The platform file uses native 960x544 for both game and video dimensions.
Manual CMake users change the sizes in that file (or point `MUGEN_VITA_CFG` at
another one); the configure step validates the values it actually contains, so
an unsupported OpenGL output or a missing `RenderMode` fails the build.

The shader compiler for the OpenGL path is a default input, not something you
have to supply. It is a decrypted Sony module, so it is not tracked here: it is
this project's `lib-for-vita.zip` release asset, and `build.sh` fetches it into
`build/downloads/` (cached; SHA-256 pinned in the script), verifies it and
unpacks `build/downloads/libshacccg.suprx`, which it then packs as
`module/libshacccg.suprx`. An ordinary build therefore bundles it with no flag:

```sh
port/psvita/build.sh            # opengl + bundled compiler, title MUGEN0001
```

`--shader-compiler FILE` packs another file instead (given on each invocation —
a plain build packs the default); `--no-shader-compiler` packs none. With
`--offline` and no cached asset the VPK is built without a compiler and the
script says so.

Manual CMake accepts
`-DMUGEN_VITA_SHADER_COMPILER=/path/to/libshacccg.suprx`. The input must exist
and have an SCE container header; this is not proof that it is decrypted or
loadable. The VPK contains `module/libshacccg.suprx`, loaded from
`app0:/module/libshacccg.suprx`. The native-load wrapper covers vitaGL's early
internal initialization as well as the later public preflight, so placing the
file in the VPK is sufficient to make that path eligible. A missing/rejected
bundle retains the existing `ur0:/data/` and `ur0:/data/external/` fallback.
The dedicated journal records the actual path and native result; verify an
`app0:/module/` load succeeds on the next device run before claiming the bundle
works independently of the system copy.

The module is loaded from `app0:/module/libshacccg.suprx` when the VPK carries
it; a missing or rejected bundle retains the existing `ur0:/data/` and
`ur0:/data/external/` fallback, so a console that already has the system module
keeps working. `--no-shader-compiler` packs none and clears the path CMake had
cached; a missing path handed to `--shader-compiler` is an error rather than a
silent omission.
Files already inside the selected game-data tree remain ordinary assets.
An OpenGL package without a bundled compiler produces a warning identifying
the required system module; a missing cached input file is an error rather
than a silent omission. `libvitashark`, `SceShaccCgExt` and taiHEN are linked dependencies,
not additional `.suprx` files to pack. `.gxp` files are optional generated shader
caches; no `.spr` dependency was found in the pinned vitaGL sources.

Verification (2026-11-10): the ARM/Vita native-resolution VPK built with the
supplied compiler. Package inspection confirmed all four 960x544 keys,
OpenGL/Precache=0, the exact input module bytes and current shaders. The nine
compiler-selection cases, six diagnostic cases (also ASan/UBSan), display/GL
regressions and eleven host build/input tests passed. Native compilation with
the self-test disabled also passed. These checks do not substitute for native
960x544 gameplay or a successful app-local module load on hardware.

### Build and test

```sh
cmake -S port/psvita -B build/psvita-gl \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DMUGEN_VITA_DATA_DIR="$PWD/install"
cmake --build build/psvita-gl
bash tools/test_vita_video.sh
```

Keep `[Misc] Precache = 0` in `port/psvita/mugen.psvita.cfg` for the hardware
test; the precache stack issue is separate. Nothing is rewritten at build time:
check `port/psvita/mugen.psvita.cfg` itself before installing.

A Linux-hosted VitaSDK GCC 15.2.0 cross build, using the pinned SDK's dependency
archives, completed through ELF, SELF and VPK with the splash, shader and
error-path corrections. Actual device verification remains pending. Host tests
check the actual adapter
with mocked calls: normal `GL_FALSE` initialization, allocation failure,
invalid/fallback viewport rejection, splash handoff before presentation,
context ownership, one bootstrap presentation and one swap per subsequent
call, vsync, repeated release, reuse of the same context, matrix upload and
projective texture coordinates. Existing MVP
and Vita shader dialect gates pass. They do not execute GXM or the Vita shader
compiler and do not prove displayed pixels, performance or blending parity.

`bash tools/test_gl_errors.sh` also selects the actual Vita shader-loader
branches against a mock driver with vitaGL's deferred-status behavior. It
checks missing compiler, fragment compile failure, GL error despite positive
status, no program creation/attachment/link on failure, all 18 program
variants, replacement cleanup, and suppression of SDL's dialog only under
native display ownership. Desktop and GLES cases remain in that same suite.

`bash tools/test_vita_shader.sh` exercises the real compiler preflight with
mocked context, file selection and vitaShaRK boundaries: primary success,
fallback success, distinct native load errors, raw ELF and short headers,
denied file access, no context, and reuse after success. The GL error suite
checks that failed preflight creates no shaders/programs and retains previously
working programs. These host tests do not execute the Vita module loader.

Hardware verification remains required: startup, menu/selection, KFM match,
RGBA and indexed sprites, transparency/PalFX, FBO orientation, screenshot,
repeated matches and exit. The supplied test VPK preserves the previous
480x272 config; no resolution change is part of this fix.

### A rotating vitaGL logo that never reaches the game

The logo is the library's startup splash, not an input screen. No button is
required: vitaGL ends its splash thread when the application starts its first
drawing scene. Swaps are ignored while that thread is active.

The first native adapter incorrectly treated the return from
`vglInitWithCustomSizes` as a success status. In the pinned source it returns
`res_fallback`: `GL_FALSE` is normal successful initialization at the requested
resolution, while `GL_TRUE` reports resolution fallback. The old check sent
normal initialization into an error path before any drawing scene. Its test
mock made the same incorrect assumption, so the previous passing tests missed
this defect.

The adapter now accepts normal initialization, clears/presents an initial
black frame to hand off the splash, and checks the initialized viewport against
the requested dimensions. A mismatched viewport is rejected without retrying
GXM initialization or switching to SDL's presenter. This dimension check is
not a general GPU-allocation health check. No resolution setting changes.

Rebuild and reinstall:

```sh
bash port/psvita/build.sh
```

Expect `GL: vitaGL splash handed off to engine` in `mugen.log`.
If the game still fails to reach its menu, retain the new log: shader loading
and actual rendering still require hardware verification.

### Ownership and limitations

The pinned vitaGL exposes no public context teardown (`eglTerminate` is a
no-op). GPU context ownership therefore lasts until process exit. Engine GL
objects and the metadata surface are released, but switching to SDL's separate
GXM System driver after GL initialization is rejected. On a GL startup failure
after ownership, the game logs the failure and exits; select System in the
config and restart to recover. Reusing the same GL display dimensions is
supported; changing dimensions requires restart.

`OpenGLScreen` is not qualified by this change: its 16-bit display request is
rejected before vitaGL ownership and follows the existing System fallback.
The native path here is `RenderMode = OpenGL` (32-bit GL sprite rendering).
Constant-alpha blend support remains unavailable, and shader/geometry changes
do not establish parity for those blend modes. Prior rendering crashes and
calibration differences remain open until their own cases are retested.

### What was tried instead

**The software renderer, at a smaller framebuffer.** The cost is per pixel
(`SoftSprite_Blit`, a palette lookup plus blend on the ARM core), so the render
size is the frame-rate lever. Earlier software builds used 640x480 and then
480x272. The current native-output default is 960x544; the older measurements
below describe the smaller software configurations:

Measured on hardware: **25-30 fps at 640x480**. The blitter cost is per pixel,
so the render size is a direct lever:

| Render size | Pixels/frame | vs 960x544 | Measured / expected |
|---|---|---|---|
| 960x544 | 522,240 | -- | -- |
| 640x480 | 307,200 | -41% | **25-30 fps (measured)** |
| 480x272 (previous software default) | **130,560** | **-75%** | 2.35x fewer pixels than 640x480 |
| 320x240 | 76,800 | -85% | motif's own localcoord |

480x272 is the panel's exact half, so the upscale is a clean 2x integer stretch
with no resampling artefacts. It remains an explicit lower-resolution option,
not the current native-output default.

### Scaling the smaller surface onto the panel

For the System renderer the engine opens a surface of the *game* size
(`Video_SetMode` sets `width = game_width`), while the SDL video surface is
requested at the panel size. The engine already separates the two:
`g_game->screen` is the render target (sized from `game_coord`, 480x272 in
those earlier lower-resolution tests) and
`g_screenSurface`/`g_videoSurface` is what the driver blits, one-to-one, to the
panel. `g_targetWidth`/`g_targetHeight` stay at the game size because every clip
rect and draw coordinate uses them; `g_videoWidth`/`g_videoHeight` are the
surface size.

Three approaches were tried, and only the third is viable:

| Approach | Result |
|---|---|
| `SDL_VITA_SetVideoModeScaling` | **Black screen.** It writes a scale into the driver's video struct that only its software-blit path reads; `VITA_UpdateRects` is an empty stub and `VITA_FlipHWSurface` passes no scale, so the frame was never drawn. |
| `zoomSurface` per frame | **4.4 fps** (down from 60). It allocates a fresh panel-sized surface and runs generic nearest-neighbour code on every call. |
| `Vita_ScaleToVideo` (`src/gfx.c`) | Writes pixels straight into the video surface: an exact-2x `uint16` fast path (the earlier 480x272 -> 960x544 setup), a cached-column-map nearest-neighbour path for other ratios, and `zoomSurface` only as a fallback for mismatched pixel formats. No per-frame allocation. |

`SDL_MUSTLOCK` is false for this surface (its flags are
`FULLSCREEN|DOUBLEBUF|NOFRAME`, with no `HWSURFACE`/`ASYNCBLIT`/`RLEACCEL`), so
writing its pixels directly is legal.

The 2x path is verified by a host test that scales 480x272 to 960x544 with
padded pitches and checks every source pixel appears as a 2x2 block: 0
mismatches over 130,560 source pixels.

Nearest neighbour preserves sharp pixels for the earlier exact-2x configuration,
where smoothing would only cost time and soften the image.



The upscale is nearest-neighbour (`smooth = 0`), which at an exact 2x is both
sharper and cheaper than bilinear.

### If the GL path is wanted anyway

It needs one of:

* **a vitaGL with 1D textures** (or a shim mapping `TEXTURE_1D` onto a 1-pixel-
  high 2D texture, which would also require rewriting `sampler1D` in the
  shader) -- the larger job, and it changes `data/indexed.frag`, which is game
  content;
* a **GPU-side palette path** replacing the indexing shader entirely.

Both are substantial and neither is verified on hardware. What is *not* a
blocker, despite appearances: `ARB_fragment_program` (the shaders are GLSL 1.20,
not ARB programs), the `"OpenGL ES 2.0 VitaGL"` version string, and
`GL_OES_texture_npot` standing in for `ARB_texture_non_power_of_two`. Those
three are misread gates, described above, and would be the first thing to
correct if the 1D texture problem is ever solved.

The build still selects `RenderMode = system`, and `System_Init` falls back to it
automatically if a GL renderer fails.

## Gameplay input: the shipped joystick bindings are for a PC pad

Menus work through the gamepad-to-keyboard bridge described earlier, but
**gameplay** input is configured separately, by button index, in
`g_game->joy_keys`. The shipped `mugen.cfg` binds `[P1 Joystick]` to indices
**9..24**:

```ini
[P1 Joystick]
Jump = 9   Crouch = 10   Left = 11   Right = 12
A = 13     B = 14        C = 20      X = 15   Y = 16   Z = 18   Start = 24
```

That is a 24-button PC pad. SDL's Vita driver reports **12** buttons in a
different order, so those bindings made **Right jump, Left crouch, Start walk
left**, and every attack button pointed at a button that does not exist. The
engine's built-in defaults are no better: `config.c` seeds `Jump=1, Crouch=2,
Left=3, Right=4`, which on this pad are Circle, Cross, Square and L.

`src/config.c` now overwrites both players' bindings with the real indices
(under `MUGEN_VITA_GL`), taken from `SDL_sysjoystick.o` and **confirmed on
hardware** with `build/psvita/input-mapper` (`axes=4 buttons=12 hats=0`, all
indices 0-11 observed):

| Action | Button | Action | Button |
|---|---|---|---|
| Jump | 8 (d-pad up) | A | 3 (Square) |
| Crouch | 6 (d-pad down) | B | 0 (Triangle) |
| Left | 7 | C | 1 (Circle) |
| Right | 9 | X | 2 (Cross) |
| Start | 11 | Y / Z | 4 (L) / 5 (R) |

Two details worth recording:

* The `KEY_*` enum is `JUMP, RIGHT, CROUCH, LEFT, A..Z, START` -- **RIGHT comes
  before CROUCH**, so the assignments are written by name rather than
  positionally; a reading-order initializer silently swaps the directions.
* The override lives in C, not in the packaged config. `string(REGEX REPLACE)`
  on the cfg's `[P1 Joystick]` block matched far too much (`[^\[]*` runs across
  newlines) and truncated 300 lines of the file. A C override is deterministic
  and leaves the shipped text untouched.

`[P1 Keys] Start` moves from `13` (Return) to `283` (F2) in the packed config,
and the pad's Start button emits F2. Return is the engine's default Start/pause
action and Circle emits Return so it can confirm menus -- without this, the
heavy punch would pause the game.

## Debugging input

Two channels feed screens, and it was impossible to tell from outside which one a
button press arrived on:

| Channel | Contents |
|---|---|
| `screen->key` | the keyboard queue — what the gamepad-to-keyboard bridge in `src/events.c` feeds (arrows, Return, Escape, F1, F2). `0x200` is the release bit. |
| `screen->input2` | the joystick queue via `Pads_PopCode()`. `0x100` is the release bit. |

`Screen_ReadInput` logs both for any screen frame where either is non-zero:

```
input: kind=0 key=13(0xD) pad=2(0x2)
```

and the bridge logs each button's mapping once:

```
pad: button 1 -> key 13
```

From a Lua script, `mugen.padstate()` returns the same picture as a string:

```lua
local s = mugen.padstate()
if s then print(s) end      -- pads=1,4, keys=Return,F1
```

### What the traces showed

The bridge was working correctly all along — the log confirmed
`button 1 -> key 13` (Circle → Return), `button 2 -> key 0` (Cross unbound),
`button 5 -> key 0` (R unbound). So a button that "does the wrong thing" is a
*screen* question, not a mapping question:

* **Cross quit the game** because it mapped to `Escape`, and `Title_Update`
  turns Escape into *choose Exit* (`st->item = TITLE_ITEM_EXIT`). M.U.G.E.N has
  no "back" key; cancel lives on Select now.
* **Circle looked like "back"** because `Title_Update` toggled the info box on
  `SDLK_F1 || SDLK_RETURN` — a leftover from when every button mapped to Return.
  Return is confirm only now; F1 (Triangle) toggles the box.

## Reference

* VitaSDK: <https://github.com/vitasdk> — `vdpm`, `share/vita.toolchain.cmake`,
  `share/vita.cmake`
* The sample this CMakeLists is modelled on:
  `$VITASDK/share/gcc-arm-vita-eabi/samples/sdl2/redrectangle/`
* vitaGL: <https://github.com/Rinnegatamante/vitaGL>
* The engine's own notes: `../../AGENTS.md`, `../../docs/ANALYSIS.md`,
  `../../PLANS.md` (see "Port work plan")

### Conservative vitaGL optimizations

The Vita renderer caches the linked program's `aPosition`, `aTexCoord` and
`uMvp` locations, initializes sampler units on first use, skips identical
per-program uniform sets, and skips redundant texture wrap/filter parameters.
These are Vita-only optimizations; `GlProgram` and `GlTex` keep their original
layouts. The caches are bounded, require no heap allocation, and safely rebuild
entries after eviction. Shader deletion/replacement and texture deletion/init
invalidate the relevant entries. Uploads and calibration use the same sampler
setter, so NEAREST/LINEAR and CLAMP/REPEAT changes remain visible.

Draw ordering, projective Q, matrix arithmetic, blend modes, resolution and game
speed are unchanged. A shader reload that fails retains the working programs.
At renderer shutdown, `mugen-vita-gl.log` records `GL CACHE` counts for skipped
uniform sets and texture parameter calls. Counts demonstrate avoided work,
not an FPS improvement.

Draw-state caching defaults ON. Build a baseline and an optimized version with
identical renderer/data/resolution options (the setting persists in CMake):

```sh
port/psvita/build.sh --no-render-cache
port/psvita/build.sh --render-cache
```

Disk shader caching is a separate, **experimental** startup optimization. It
requires the vendored vitaGL target and defaults OFF. `--shader-cache` selects
that target and compiles it with `HAVE_SHADER_CACHE=1`; `--no-shader-cache`
disables the feature. Neither option removes the bundled/runtime compiler
requirement. Normal builds preserve explicit cache settings.

```sh
port/psvita/build.sh --shader-cache
port/psvita/build.sh --no-shader-cache
```

Upstream's cache reader trusts cache contents and does not robustly handle
truncated reads/allocation failure. Do not enable it for a conservative release
until cold/warm/corrupt-cache behavior is qualified on hardware. If an enabled
cache causes startup trouble, rebuild with `--no-shader-cache` before retrying.
No new draw/buffer/math speedhack flags are enabled by these changes.

Vertex-layout caching, VBO streaming, batching and render-target reordering are
not enabled: attribute toggles currently invalidate layout reuse, and those
changes need separate visual and GPU-lifetime qualification. Lower internal
resolution remains an explicit configuration experiment, never an automatic
optimization.

Host verification: `tools/test_gl_errors.sh` tests actual renderer code with
mock GL entry points, including cache hits, exact float bits, transactional
replacement, numeric name reuse, bounded eviction and sampler changes after
uploads. `tools/test_vita_video.sh` checks the actual adapter's quad submission.
Neither test measures Vita GPU speed. Hardware acceptance requires identical
characters, stage, clocks, inputs and duration, fixed-frame screenshots, drawn
FPS/skips and frame-time measurements, with one optimization changed at a time.
