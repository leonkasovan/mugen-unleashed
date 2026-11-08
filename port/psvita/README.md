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
| `--titleid ID` | nine-character title ID (default `MSDK00001`) |
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
`GameWidth/GameHeight = 640x480` (the render size, not the 960x544 panel),
`FullScreen = 1`, and `[P1 Joystick]` bound to the Vita driver's engine codes
(Triangle 9, Circle 10, Cross 11, Square 12, L 13, R 14, Down 15, Left 16,
Up 17, Right 18, Select 19, Start 20). `MUGEN_VITA_CFG` overrides the template;
with it empty, the data directory's `data/mugen.cfg` is used as before.

### The packed `mugen.cfg` is patched

A desktop `data/mugen.cfg` does not work on the Vita, so four keys are rewritten
in a copy that goes into the VPK:

```ini
[Config]
GameWidth  = 480        ; was 1280
GameHeight = 272        ; was 720

[Video]
RenderMode = system    ; the shipped default; see "The OpenGL renderer" below
FullScreen = 1          ; was 0
```

The values above are what the build actually packs: `MUGEN_VITA_VIDEO_WIDTH`
(480), `MUGEN_VITA_VIDEO_HEIGHT` (272), `MUGEN_VITA_FULLSCREEN` (ON) and
`MUGEN_VITA_RENDERMODE` (`system`). `RenderMode = OpenGL` now selects the native vitaGL display and
programmable sprite path described below; SDL's separate video presenter is
bypassed. System remains the default pending hardware qualification.

`GameWidth`/`GameHeight` matter most. vitaGL has exactly **one** framebuffer —
the Vita's 960x544 — and the engine asks for a GL surface of `GameWidth x
GameHeight`. Ask for 1280x720 and `SDL_SetVideoMode` fails; the engine's own
quirk (`Video_SetMode`'s comment: *“with OpenGL a failed SDL_SetVideoMode is not
noticed here”*) means it then calls `Gl_Init` with **no GL context** and dies
with `C2-12828-1` before drawing a frame. The build patch below prevents that; the same symptom also comes from an oversized newlib heap, which is a separate failure — see "Diagnosing a failure on the console".

**Your game tree is never modified.** The patch is written to
`build/psvita/vita-mugen.cfg` and substituted while the VPK is packed, so
`install/` stays the pristine reference. To keep a config as-is, pass
`-DMUGEN_VITA_RENDERMODE=` (empty) or edit the values above.

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
| `MUGEN_VITA_TITLEID` | `MSDK00001` | 9-character title ID |
| `MUGEN_VITA_VERSION` | `01.00` | `APP_VER`, format `##.##` |
| `MUGEN_VITA_MEMSIZE` | `0` | `vita-make-fself -m`: `0` for a normal app (recommended), `0x1000`–`0x12800` KiB for system mode |
| `MUGEN_VITA_HEAP_MIB` | `64` | newlib heap; SDL's Vita video driver needs process memory for GXM too, so an oversized heap starves `sceGxmInitialize` |
| `MUGEN_VITA_VIDEO_WIDTH` | `480` | `GameWidth` (render size; the panel is 960) |
| `MUGEN_VITA_VIDEO_HEIGHT` | `272` | `GameHeight` (render size; the panel is 544) |
| `MUGEN_VITA_FULLSCREEN` | `ON` | Write `FullScreen = 1` into the packed `mugen.cfg` |
| `MUGEN_VITA_RENDERMODE` | `system` | `RenderMode` in the packed `mugen.cfg` (`opengl` needs an SDL built with vitaGL) |
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

Installs to `ux0:app/MSDK00001/`. Start it from the LiveArea bubble.

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
   leave `ux0:app/MSDK00001/` behind, and the promoter then refuses the reinstall.
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
ux0:app/MSDK00001/eboot.bin -gamedir ux0:data/mugen
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

`640x480` is the default: it is the engine's standard-definition target, and on
the software renderer the render size is the frame-rate lever (see "The OpenGL
renderer" below). The engine scales it up to the panel, which is what
`keepaspect` controls. `960x544` is the panel's own resolution and would be the
right choice only if a GPU renderer were in use.

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
Vita that is `ux0:app/MSDK00001/` for a LiveArea launch. That is the single most
useful debugging aid this port has: the same log the Windows build produces,
reachable through VitaShell.

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
`ux0:data/mugen.log` and `ux0:app/MSDK00001/mugen.log`. If the app started at
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

To explicitly build the OpenGL test configuration from MSYS2/Git Bash:

```sh
git pull --ff-only
bash port/psvita/build.sh --renderer opengl --precache 0
```

`--renderer` writes `MUGEN_VITA_RENDERMODE`; without it, the existing CMake
cache/default controls the mode (a fresh configuration defaults to System).
`--precache` writes `MUGEN_VITA_PRECACHE`, whose Vita default is now 0 so that
packing does not undo the confirmed precache workaround. The configure log
prints both values. `--precache 1` is an explicit opt-in; the separate worker
stack fix is not part of this change.

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
| **`Error 0x8010113D`** installing the VPK | `ScePromoterUtil` refused the package (`0x801011xx`; the console may show it as `C2-12691-9`). First suspect is `sce_sys/` artwork that is not 8-bit indexed — see "The bubble artwork"; then the ordered checks in "Verify the copy before installing" (copy size/SHA-256, free space on `ux0:`, a leftover `ux0:app/MSDK00001/`, installing from `ux0:` rather than `uma0:`) |
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

### Build and test

```sh
cmake -S port/psvita -B build/psvita-gl \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DMUGEN_VITA_RENDERMODE=opengl \
  -DMUGEN_VITA_DATA_DIR="$PWD/install"
cmake --build build/psvita-gl
bash tools/test_vita_video.sh
```

Keep `[Misc] Precache = 0` in the configuration used for the hardware test.
The precache stack issue is separate. The build patches a copy of the selected
configuration; check `build/psvita-gl/vita-mugen.cfg` before installing.

A Linux VitaSDK cross build completed through ELF, SELF and VPK. Host tests
check the actual adapter with mocked calls: creation and allocation failures,
context ownership, one swap per call, vsync, repeated release, reuse of the
same context, matrix upload and projective texture coordinates. Existing MVP
and Vita shader dialect gates pass. They do not execute GXM or the Vita shader
compiler and do not prove displayed pixels, performance or blending parity.

Hardware verification remains required: startup, menu/selection, KFM match,
RGBA and indexed sprites, transparency/PalFX, FBO orientation, screenshot,
repeated matches and exit. The supplied test VPK preserves the previous
480x272 config; no resolution change is part of this fix.

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
size is the frame-rate lever. The packed `mugen.cfg` now defaults to
**640x480**, the engine's own standard-definition target and the resolution the
M.U.G.E.N motif is designed around:

Measured on hardware: **25-30 fps at 640x480**. The blitter cost is per pixel,
so the render size is a direct lever:

| Render size | Pixels/frame | vs 960x544 | Measured / expected |
|---|---|---|---|
| 960x544 | 522,240 | -- | -- |
| 640x480 | 307,200 | -41% | **25-30 fps (measured)** |
| **480x272 (default)** | **130,560** | **-75%** | 2.35x fewer pixels than 640x480 |
| 320x240 | 76,800 | -85% | motif's own localcoord |

480x272 is the panel's exact half, so the upscale is a clean 2x integer stretch
with no resampling artefacts. That is why it is the default rather than some
other small size.

### Scaling the smaller surface onto the panel

For the System renderer the engine opens a surface of the *game* size
(`Video_SetMode` sets `width = game_width`), while the SDL video surface is
requested at the panel size. The engine already separates the two:
`g_game->screen` is the render target (sized from `game_coord`, 480x272) and
`g_screenSurface`/`g_videoSurface` is what the driver blits, one-to-one, to the
panel. `g_targetWidth`/`g_targetHeight` stay at the game size because every clip
rect and draw coordinate uses them; `g_videoWidth`/`g_videoHeight` are the
surface size.

Three approaches were tried, and only the third is viable:

| Approach | Result |
|---|---|
| `SDL_VITA_SetVideoModeScaling` | **Black screen.** It writes a scale into the driver's video struct that only its software-blit path reads; `VITA_UpdateRects` is an empty stub and `VITA_FlipHWSurface` passes no scale, so the frame was never drawn. |
| `zoomSurface` per frame | **4.4 fps** (down from 60). It allocates a fresh panel-sized surface and runs generic nearest-neighbour code on every call. |
| `Vita_ScaleToVideo` (`src/gfx.c`) | Writes pixels straight into the video surface: an exact-2x `uint16` fast path (the default 480x272 -> 960x544), a cached-column-map nearest-neighbour path for other ratios, and `zoomSurface` only as a fallback for mismatched pixel formats. No per-frame allocation. |

`SDL_MUSTLOCK` is false for this surface (its flags are
`FULLSCREEN|DOUBLEBUF|NOFRAME`, with no `HWSURFACE`/`ASYNCBLIT`/`RLEACCEL`), so
writing its pixels directly is legal.

The 2x path is verified by a host test that scales 480x272 to 960x544 with
padded pitches and checks every source pixel appears as a 2x2 block: 0
mismatches over 130,560 source pixels.

Nearest neighbour is the correct filter: the default scale is an exact integer,
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
