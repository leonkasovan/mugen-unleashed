# mugen-unleashed — PlayStation Vita port

Cross-build of the reconstructed M.U.G.E.N 1.1 Beta 1 engine for the
**PS Vita**, using [VitaSDK](https://github.com/vitasdk) (ARMv7-A, newlib).

The engine itself (`../../src`) is the Windows reconstruction, unchanged in
behaviour. This directory holds only what the Vita needs: a `CMakeLists.txt`,
the LiveArea assets, and the OpenGL adaptation.

Output: a `mugen.vpk` you can copy to the console and install with VitaShell.

---

## What this build actually is

| | |
|---|---|
| Target | ARMv7-A, Arm® Cortex®-A9, VFPv3/NEON |
| Toolchain | VitaSDK `arm-vita-eabi-gcc` (GCC 15.2.0) |
| libc | newlib |
| Graphics | **vitaGL** — GL 1.x/2.1 compatibility layer over SceGxm |
| SDL | **SDL 1.2.16** from VitaSDK (`libSDL.a`), not SDL2 |
| Output | `mugen.self` (runnable) and `mugen.vpk` (installable) |

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

**VitaSDK.** The setup script this project's PS Vita environment uses is
`C:\Projects\setup_psvita_dev.bat`, which sets

```bat
set VITASDK=C:\vitasdk
set PATH=%VITASDK%\bin;C:\w64devkit\bin;C:\Python314;%PATH%
set CMAKE_TOOLCHAIN_FILE=%VITASDK%\share\vita.toolchain.cmake
```

Any equivalent shell works; the build only needs `VITASDK` and the toolchain
file.

**VitaSDK packages.** These are separate installs, not part of the base SDK:

```sh
vdpm install sdl sdl_gfx sdl_mixer sdl_ttf zlib libpng      # already in the base SDK
vdpm install vitaGL                                        # REQUIRED, not installed by default
```

`vitaGL` pulls in `vitaShaRK`, `SceShaccCgExt` and `libmathneon`. SDL 1.2's
OpenGL video driver cannot initialise without it, and the CMake configure fails
with an explicit message if it is missing.

**A host `pkg-config`.** VitaSDK's `arm-vita-eabi-pkg-config` is a bash wrapper
around the host `pkg-config`, so one has to be on `PATH` — w64devkit's is what
the setup script provides.

**CMake ≥ 3.16 and Ninja** (or Make). CMake 4.x is fine.

---

## Building

From the repository root, in a shell with `VITASDK` set:

```sh
export VITASDK=/c/vitasdk        # C:\vitasdk

cmake -S port/psvita -B build/psvita -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build/psvita
```

Result (verified, clean build directory, nothing else set):

```
build/psvita/mugen       4264 KB   ARM EABI ELF (intermediate)
build/psvita/mugen.self  2203 KB   SELF, compressed
build/psvita/mugen.vpk   2198 KB   installable homebrew
```

### Building without Ninja

```sh
cmake -S port/psvita -B build/psvita -G "MinGW Makefiles" ...
cmake --build build/psvita -j8
```

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
`MUGEN_VITA_RENDERMODE` (`system`). Do not set `RenderMode = OpenGL` in a
normal build: VitaSDK's SDL 1.2 has no GL support — its Vita video driver
(`SDL_vitavideo.o`) is a SceGxm 2D blitter with no context API — and
`src/system.c` falls back to `RENDER_SYSTEM` when the GL surface has no
context. An `OpenGL` value in the packed config only costs a failed attempt on
every launch. See "The OpenGL renderer" below for what `opengl` actually
requires and what is still unimplemented.

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

---

## Installing

```sh
# Copy to the console (with VitaShell in USB/FTP mode, or a memory card reader)
cp build/psvita/mugen.vpk /path/to/ux0:/

# Then in VitaShell: select mugen.vpk -> "Install"
```

Installs to `ux0:app/MSDK00001/`. Start it from the LiveArea bubble.

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
missing DLL on Windows. The built-in SDL_mixer BGM subsystem —
WAV, OGG and MP3 through `Mix_LoadMUS` — is what works. `SDL_mixer` on the Vita
links libmikmod/FLAC/vorbis/mpg123, so that covers the usual formats.

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

### Where the port's code lives

The engine sources are guarded with `MUGEN_VITA_GL` and are otherwise
unchanged. The port's own code is `port/psvita/src/`; `src/mugen.h` includes
`vita_gl_shim.h` in the Vita branch so `src/symbols.h` can map the original's
`g_glew*` names onto it. The Windows and Linux builds never see any of it.

---

## Layout

```
port/psvita/
  CMakeLists.txt          the build
  README.md               this file
  src/vita_gl_shim.c      GL capability probe + the entry points vitaGL lacks
  src/vita_gl_shim.h
  sce_sys/                LiveArea assets (from the VitaSDK sample; MIT/zlib)
    icon0.png
    livearea/contents/bg.png
    livearea/contents/startup.png
    livearea/contents/template.xml
  ../../src/              the engine (shared with every other target)
  ../../third_party/      lua-5.1.4, minizip-0.15, libresample (vendored)
```

`sce_sys/` is the VitaSDK sample's artwork. Replace it with your own before
distributing a custom bubble — it is not this project's art.

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
own test executables get copied, so a stale `mugen_unleashed.exe` (a 1.7 MB
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

Simpler, and enough for most cases: install **`VitaShell`** and look at
`ux0:data/` — if the app started at all it will have written `mugen.log` next
to its eboot (`ux0:app/MSDK00001/mugen.log`). No log means it never reached
`main()`, which confirms a module-load failure rather than a game crash.

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
| `Entering gameflow loop` | The engine is running. Anything after this is a game-data problem, not a port problem. |

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

| Symptom | Cause and fix |
|---|---|
| `VITASDK is not set` | Export it, or pass `-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake` |
| `arm-vita-eabi/include/SDL/SDL.h not found` | VitaSDK's `sdl` package is missing: `vdpm install sdl` |
| `libvitaGL.a not found` | `vdpm install vitaGL` (not installed by default) |
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

## The OpenGL renderer: why it is off, and what turning it on would take

The Vita build runs the **System (software)** renderer at 640x480. That is a
per-pixel palette lookup and blend into an RGB565 surface on the ARM core
(`SoftSprite_Blit` in `src/softrender.c`), which is the frame-rate ceiling on
this hardware. The OpenGL renderer in `src/glrender.c` is fully implemented and
already in the tree -- it just cannot get a context from VitaSDK's SDL 1.2.

Findings from reading the relevant sources, recorded because the obvious
conclusion ("use the SDL-Vita `sdl12_gl` fork") is wrong:

### VitaSDK's SDL 1.2 has no OpenGL

`SDL_vitavideo.o` in `libSDL.a` references no GL context API and never calls
`vglInit`; `SDL_config_vita.h` defines no `SDL_VIDEO_OPENGL`, so
`SDL_SetVideoMode(..., SDL_OPENGL)` cannot succeed. This is stock SDL 1.2 with a
2D-only Vita backend.

### The SDL-Vita `sdl12_gl` fork does not help either

[Rinnegatamante/SDL-Vita](https://github.com/Rinnegatamante/SDL-Vita) branch
`sdl12_gl` is SDL 1.2.15, and `sdl12_gl` is its default branch -- but the name
means "2D blitter rewritten on top of vitaGL", not "SDL_OPENGL supported". The
Vita driver is `src/video/psp2/SDL_psp2video.c`, which calls
`vglInitExtended(...)` for its own blitting and contains **no** reference to
`SDL_OPENGL`, `GL_SwapBuffers`, `GL_SetAttribute` or `gl_config`.
`include/SDL_config_psp2.h` defines no `SDL_VIDEO_OPENGL`, so SDL's own
`SDL_SetVideoMode` returns NULL with "OpenGL not available" before the driver is
reached. (The `src/video/vgl/` directory there is the FreeBSD libVGL console
driver, unrelated to vitaGL.)

### What vitaGL itself provides

`libvitaGL.a` has the fixed-function path this engine uses --
`glBegin`/`glVertex2f`/`glTexSubImage2D`/`glUseProgram` -- plus a GLSL
translator (`glsl_translator_process`) and framebuffer objects. Against the
engine's four `Gl_Init` gates:

| Gate | vitaGL | Verdict |
|---|---|---|
| GL >= 2.0 | `GL_VERSION` = `"OpenGL ES 2.0 VitaGL"` | string is not "N.M", so the probe's parse reads 0 |
| `ARB_texture_non_power_of_two` | not advertised; `GL_OES_texture_npot` is | different name, same capability |
| `EXT_framebuffer_object` | advertised | pass |
| `ARB_fragment_program` | **not implemented** | but the engine never uses it |

That last row is the important one: the engine checks for
`ARB_fragment_program` because GLEW-era code did, while
`data/indexed.frag` and `data/rgba.frag` are plain **GLSL `#version 120`**. The
gate does not describe what the engine actually uses. `ARB_fragment_program`
would only be needed by a driver without fragment shaders.

Also absent from vitaGL and already shimmed in `port/psvita/src/vita_gl_shim.c`:
`glBlendColor` (so constant-alpha blending is lost -- sprites using it blend
additively), `glMultiTexCoord4f`, `glIsShader` and `glLogicOp`. Getting a context
also needs `vglInitExtended` called directly, since SDL will not do it.

### The 1D texture blocker, and the vitaGL build flags that fix it

The engine draws every sprite through an **indexing shader**: SFF v1 sprites are
8-bit palettised, so `data/indexed.frag` looks the pixel index up in a palette
texture. `src/glrender.c` binds that palette as a **1D** texture:

```c
glBindTexture(GL_TEXTURE_1D, tex->id);
glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, width, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
```

In VitaSDK's **stock** `libvitaGL.a` both are empty stubs:

```
00002aa4 <glTexImage1D>:
    2aa4:	bx	lr          <- returns immediately
```

and vitaGL itself says why, in `source/textures.c`:

```c
#ifndef HAVE_UNPURE_TEXFORMATS
	vgl_log("GL_TEXTURE_1D support is disabled. Compile vitaGL with "
	        "UNPURE_TEXFORMATS=1 to enable it.\n");
```

So this is a **build-time** switch, not a missing feature. Rebuilding vitaGL
with `UNPURE_TEXFORMATS=1` makes the call real:

```c
glTexImage2D(GL_TEXTURE_1D, level, internalFormat, width, 1, border, format, type, data);
```

(a 1D texture becomes a 1-pixel-high 2D texture), and `glTexImage1D` then
compiles to an actual implementation instead of `bx lr`. **Verified by building
it here:** `make UNPURE_TEXFORMATS=1 TEXTURES_SPEEDHACK=1` produces
`libvitaGL.a`, and `glTexImage1D` in it is ~200 bytes of real code.

### Every vitaGL build flag, against what this engine needs

The engine's GL feature set, read from `src/glrender.c`/`gldetect.c`/`prim.c`:
two texture units (`glActiveTexture` + `glBindTexture` to `GL_TEXTURE0` and
`GL_TEXTURE1`), GLSL 1.20 shaders, immediate mode (`glBegin`/`glVertex2f`),
`glMatrixMode`/`glOrtho`, `glTexEnvf(GL_REPLACE)`, `GL_CONSTANT_ALPHA`
blending, `GL_LINES`/`GL_POINTS`, one `glReadPixels`, and 1D textures for
palettes.

**Required**

| Flag | Why |
|---|---|
| `UNPURE_TEXFORMATS=1` | Enables `GL_TEXTURE_1D` -- the palette texture. Without it `glTexImage1D` is an empty stub. |
| `HAVE_FFP_SHADER_SUPPORT=1` | Provides the GLSL 1.20 matrix built-ins. Confirmed in `custom_shaders.c`: `ffp_bind_names` = `gl_ModelViewProjectionMatrix`, `gl_ModelViewMatrix`, `gl_NormalMatrix`. Without it those are undefined identifiers. Costs a little shader-pipeline speed. |
| `UNPURE_TEXCOORDS=1` | Makes multitexture legal with unit 0's `GL_TEXTURE_2D` **disabled**. `Gl_DrawQuadIndexed` binds unit 0, binds unit 1, and draws without ever calling `glEnable(GL_TEXTURE_2D)` -- the shader samples, the fixed-function enable state is never set. This is exactly the case the flag legalises. |

**Useful**

| Flag | Why |
|---|---|
| `HAVE_HIGH_FFP_TEXUNITS=1` | More than 2 texunits in the fixed-function path. Not needed (the engine uses exactly 2), harmless. |
| `TEXTURES_SPEEDHACK=1` | Slightly faster texture binds. Verified compatible; must not be combined with `HAVE_TEXTURE_CACHE=1`. |
| `HAVE_WVP_ON_GPU=1` | Moves the WVP matrix to the GPU: less CPU work, more GPU. Relevant because this port is CPU-bound. |
| `LOG_ERRORS=1`, `HAVE_SHARK_LOG=1`, `DEBUG_GLSL_TRANSLATOR=1` | For bringing the shaders up. `DEBUG_GLSL_TRANSLATOR` prints translator input and output, which is the tool for the `gl_TexCoord` problem below. |

**Deliberately avoided**

| Flag | Why not |
|---|---|
| `NO_TEX_COMBINER=1` | Faster FFP, but the engine sets `glTexEnvf(GL_TEXTURE_ENV_MODE, GL_REPLACE)` on the screen-texture path. |
| `PRIMITIVES_SPEEDHACK=1` | Documented to glitch `GL_LINES`/`GL_POINTS`, and the engine draws both (debug lines, the console cursor). |
| `SAMPLERS_SPEEDHACK=1` | "May cause glitches" in sampler resolution -- the palette sampler is the one that must be exact. |
| `MATH_SPEEDHACK=1`, `DRAW_SPEEDHACK=*`, `BUFFERS_SPEEDHACK=1`, `INDICES_*_SPEEDHACK`, `TEXTURE_UPLOADS_SPEEDHACK=1`, `READBACKS_SPEEDHACK=1` | All documented as potentially crashing or glitching. This port needs correctness of the palette lookup more than it needs a few percent. |
| `ENABLE_LEGACY_PIPELINE=1`, `HAVE_FIXED_ATTRIBUTES=1`, `STORE_DEPTH_STENCIL=1`, `SAFE_ETC1=1`, `HAVE_DISPLAY_LISTS=1` | `ENABLE_LEGACY_PIPELINE` is not in the `Makefile` at all; the rest are irrelevant to this engine. |

A working build line for the flags that are needed:

```sh
make -f Makefile UNPURE_TEXFORMATS=1 HAVE_FFP_SHADER_SUPPORT=1 \
     UNPURE_TEXCOORDS=1 TEXTURES_SPEEDHACK=1 HAVE_WVP_ON_GPU=1
```

(`UNPURE_TEXFORMATS=1 TEXTURES_SPEEDHACK=1` was built and verified here: the
resulting `glTexImage1D` is real code, not `bx lr`.)

### What still blocks the GL renderer, after the flags

`UNPURE_TEXFORMATS` fixes the *texture*, not the *shader*. The README is explicit
that "tex2D is still required in shader code", and vitaGL's GLSL compatibility
header (`source/shaders/glsl_translator_hdr.h`) provides `texture2D`,
`texture2DProj`, `texture2DLod`, `textureCube`, `textureCubeLod` -- and **no
`texture1D` and no `sampler1D`**. The engine's shader uses both:

```glsl
uniform sampler1D texunit1;
...
vec4 color = texture1D(texunit1, index_correction(idx));
```

So `data/indexed.frag` needs two mechanical edits:

1. `sampler1D texunit1` -> `sampler2D texunit1`
2. `texture1D(texunit1, x)` -> `texture2D(texunit1, vec2(x, 0.5))` (sample the
   single row's centre)

plus `vglOverrideTexFormat(GL_TEXTURE_1D)` called before compiling, so vitaGL
routes 1D-bound textures to the 2D slot (`tex2d_override` in
`custom_shaders.c`; public API, declared in `vitaGL.h`).

### What has been built: the vendored vitaGL

`third_party/vitagl/` holds a **pinned** copy of vitaGL
(`9bf3e3f`, 2026-10-30, LGPL), and `port/psvita/cmake/VitaGl.cmake` builds it
through CMake with the three required flags plus `TEXTURES_SPEEDHACK`.

Restore the source (it is not committed — ~2.4 MB) and build the library:

```sh
tools/psvita/fetch_vitagl.sh
cmake -S port/psvita -B build/psvita-gl -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DMUGEN_VITA_BUILD_VITAGL=ON
cmake --build build/psvita-gl --target vitagl_vita
```

**Verified:** the build succeeds, and `glTexImage1D` in the result is real code
— against `bx lr` in VitaSDK's prebuilt library. So `UNPURE_TEXFORMATS` does what
its description claims.

`MUGEN_VITA_BUILD_VITAGL` is **off by default**. The software renderer remains
the shipping path: this library is the prerequisite for the OpenGL work, not a
working renderer by itself, for the reason in the next section.

### The shaders have been rewritten

`port/psvita/shaders/` holds vitaGL-compatible versions of all four renderer
shaders, substituted into the VPK in place of `data/indexed.{vert,frag}` and
`data/rgba.{vert,frag}`. `install/` stays pristine and the Windows/Linux builds
keep the originals.

The **PalFX maths is byte-for-byte unchanged** -- add/mul/invertall/colorbal
order, the `PREMUL` variants and the `INDEX_*` corrections are exactly as
shipped. Only what vitaGL cannot process was changed:

| Original | Rewritten | Why |
|---|---|---|
| `gl_TexCoord[0]` | `varying vec4 vTexCoord` | vitaGL's translator has no handling for `gl_TexCoord` |
| `gl_MultiTexCoord0` | `attribute vec4 aTexCoord` | same, for `gl_MultiTexCoordN` |
| `gl_Vertex` | `attribute vec4 aPosition` | same, for `gl_Vertex` |
| `uniform sampler1D texunit1` | `uniform sampler2D texunit1` | vitaGL knows only 2D and cube samplers |
| `texture1D(t, x)` | `texture2D(t, vec2(x, 0.5))` | the single row `glTexImage1D` creates under `UNPURE_TEXFORMATS`; `0.5` samples its centre |
| `#version 120` | dropped | vitaGL comments the directive out itself |
| `////*EB_DEFINES*////` | **kept** | `Gl_InsertDefines` replaces it with the engine's `#define` block; every variant depends on it |

Verified: the four rewritten files are what the VPK now carries
(`unzip -p .../mugen.vpk data/indexed.frag | grep "vitaGL build"`), and the diff
against the originals is only the lines above.

### The remaining blocker is not the shaders: it is the draw path

Rewriting the shaders is necessary but **not sufficient**, and the reason is in
vitaGL's source rather than in the shaders:

* `glEnd` (`source/ffp.c`) calls `sceGxmDraw` directly against the legacy vertex
  pool, using vitaGL's own fixed-function shaders. It contains **no reference to
  a bound program.**
* `_glDrawArrays_FixedFunctionIMPL` begins with `reload_ffp_shaders(...)`.

So for `glBegin`/`glEnd` -- which is how `src/glrender.c` draws every sprite --
vitaGL **ignores `glUseProgram` entirely**. The engine's palette and PalFX
shaders cannot take effect on that path no matter what the shader text says.

Using them requires the renderer to draw through the *programmable* path instead:
vertex buffers plus `glVertexAttribPointer`/`glDrawArrays`, so that `aPosition`
and `aTexCoord` are supplied as named attributes rather than by immediate mode.
That is a rewrite of `Gl_DrawQuadIndexed`/`Gl_DrawQuadRgba` and the callers in
`Gl_DrawSprite`, not a shader edit -- and it has to keep the x87 float ordering
of the quad maths intact, as `PLANS.md` requires.



Checked against `source/shaders/glsl_translator_hdr.h` and
`source/utils/glsl_utils.c`, vitaGL's translator handles `gl_VertexID`,
`gl_FragCoord`, `gl_FragDepth`, `gl_PointCoord`, `gl_PointSize`,
`gl_FrontColor`, `gl_FrontFacing` and -- with `HAVE_FFP_SHADER_SUPPORT` -- the
three matrices. It has **no handling at all** for `gl_Vertex`,
`gl_MultiTexCoord0..n` or `gl_TexCoord[0]`.

The engine's shaders depend on all three:

```glsl
// indexed.vert
gl_TexCoord[0] = gl_MultiTexCoord0;
gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
// indexed.frag
vec4 idxv = texture2D(texunit0, gl_TexCoord[0].st);
```

The version directive is not an issue -- the translator comments `#version`
out on purpose. The built-in *names* are the issue. Rewriting these shaders for
vitaGL therefore means converting them to modern GLSL (`in`/`out` varyings,
explicit `attribute`/`varying` declarations), not a two-line edit. That is a real
rewrite of `indexed.vert`, `indexed.frag`, `rgba.vert` and `rgba.frag`, which
are **game content** shipping with M.U.G.E.N 1.1b1.

### Summary of what the GL path needs

| Item | Effort | State |
|---|---|---|
| Custom vitaGL build (`UNPURE_TEXFORMATS`, `HAVE_FFP_SHADER_SUPPORT`, `UNPURE_TEXCOORDS`) | small | flags identified; build verified to work |
| `vglInitExtended` instead of `SDL_OPENGL`, `vglSwapBuffers` to present | small | not written |
| Probe: accept `"OpenGL ES 2.0 VitaGL"`, `GL_OES_texture_npot`, drop `ARB_fragment_program` | small | not written |
| `sampler1D`/`texture1D` -> 2D in `indexed.frag` | small | not written |
| Rewrite all four shaders for `in`/`out` instead of `gl_TexCoord`/`gl_MultiTexCoord` | **substantial** | not written |
| `glBlendColor` absent -> constant-alpha blending becomes additive | accepted loss | documented |

The last two are why this is not a quick switch. The shader rewrite is the real
cost, and it changes shipped game content rather than port code.

Point 1 and 2 change **game content**, not port code -- `data/indexed.frag` ships
with M.U.G.E.N, so editing it means the VPK carries a modified shader. That is
acceptable for a port (the file is part of the runtime, not the original binary)
but it should be stated in the build, and it must not alter the Windows build.

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
