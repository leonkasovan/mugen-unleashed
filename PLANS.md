# PLANS

Porting mugen-unleashed from Windows/MSVC to Linux/GCC. This file records the
research (what the code actually depends on), the decisions, and the ordered
work plan. Update the status column as work lands; keep the research sections
true to the findings they were based on.

## Goal

Build `mugen` on Linux with GCC and a standard Makefile, with every library the
game links either vendored as source under `third_party/` (built by the
Makefile, like lua and libpng already are) or coming from distro packages.
Replace the Windows DLL mechanism entirely: no import libraries, no
`Elecbyte.MUGEN.libs`, no manifests.

Two build targets are in scope:

| Target | Toolchain | Fidelity |
|---|---|---|
| linux-32 | `gcc -m32` on a multilib distro | faithful: struct layouts and x87 float semantics preserved |
| linux-64 | `gcc` (default x86-64) | functional: layout fixes accepted, float drift accepted and tested |

## Research findings

### How much Windows is actually in `src/`

11 files include `windows.h`, but the real Win32 surface is small. Grep for
types and APIs, not includes:

| File | Real Win32 usage | Linux replacement |
|---|---|---|
| `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `matchload.c`, `mugen.h` | none — the include only pulls in `GL/gl.h` transitively | delete `#include <windows.h>`, include `<GL/gl.h>` (Mesa) |
| `timer.c` | `timeGetTime`, `QueryPerformanceCounter`, `timeBeginPeriod` | `clock_gettime(CLOCK_MONOTONIC)` shim |
| `video.c` | `Video_SetFullscreen` pokes `HWND` styles via `SDL_SysWMinfo` | `SDL_WM_ToggleFullScreen`, or an X11 path |
| `system.c` | window icon (`LoadIconA` + resource 101), `SHGetFolderPathA(CSIDL_FONTS)` | icon as SDL surface; fonts dir `/usr/share/fonts` |
| `error.c` | RichEdit error dialog (`Riched20.dll`, `DialogBoxParam`, the .rc dialog 200) | SDL message box or terminal output |
| `sdlmain.c` | `WinMain`, the `DDRAW.DLL` preload quirk, stdout redirection | plain `main()`; SDLmain is not needed on Linux |
| `bgm.c`, `bgmplugins.c` | `LoadLibraryA` / `GetProcAddress` for BGM plugins | `dlopen` / `dlsym`, near line-for-line |
| `crt.h` | `<direct.h>` (`_mkdir`) | `<sys/stat.h>` `mkdir()` |

CRT shims needed across `src/` (counts from grep): `_stricmp` 92,
`_snprintf` 44, `_vsnprintf` 9, `strlwr` 6, `_alloca` 3 → one compat header
maps them to `strcasecmp`, `strncasecmp`, `snprintf`, `vsnprintf`, a small
helper, `alloca`. No `_findfirst`, `_access`, `_itoa` uses.

Threads are already POSIX: 17 `pthread_*` symbols, no semaphores, no sched.
On Linux glibc provides them natively, so `pthreadVC2.dll` simply disappears.

### Library sources, DLL by DLL

| DLL | Linux source | Feasibility |
|---|---|---|
| `SDL.dll` 1.2.15 | libsdl 1.2.15 source; or `sdl12-compat` over SDL2 (what modern distros ship) | straightforward |
| `SDL_mixer.dll` 1.2.11 | libsdl-mixer 1.2.12 source | straightforward |
| `SDL_ttf.dll` 2.0.11 | libsdl-ttf 2.0.11 source + freetype (vendor freetype too) | straightforward |
| `SDL_gfx.dll` | sdl-gfx 2.0.26 source; the game calls only `zoomSurface` (2 sites) — alternatively reimplement those two calls with SDL | trivial |
| `zlib1.dll` 1.2.3 | zlib source (~30 files) | trivial |
| `libresample.dll` 0.1.3 | libresample source — **Elecbyte's multichannel variant**: vanilla 0.1.3 is mono and its API differs; the extra exports (`resample_dup`, `resample_get_filter_width`, `resample_get_in/outbuffer_size`) and the interleaved-buffer `resample_process` signature confirm a modified build. Needs a patch or a faithful re-derivation | needs patch |
| `pthreadVC2.dll` | glibc pthreads | delete |
| `libmpg123-0.dll` | libmpg123 source or distro package | straightforward |
| `Riched20.dll`, `DDRAW.DLL` | not needed; the code paths they serve get replaced | n/a |

Headers: the import-time headers in `third_party/sdk/include` stay as the
reference for what the code expects; on Linux the vendored sources carry their
own headers, so `third_party/sdk/include/SDL` becomes unnecessary once the SDL
stack is vendored. `libresample.h` (written for this project from call sites)
remains useful as the contract for the multichannel build.

Planned `third_party/` additions:
`SDL-1.2.15/`, `SDL_mixer-1.2.12/`, `SDL_ttf-2.0.11/`, `freetype-<ver>/`,
`SDL_gfx-2.0.26/`, `zlib-1.2.3/`, `libresample-0.1.3-multich/`.
Each built by the Makefile like lua/libpng, or satisfied by distro packages
(`libsdl1.2-dev`, `libsdl-mixer1.2-dev`, `libsdl-ttf2.0-dev`, `libsdl-gfx1.2-dev`,
`zlib1g-dev`, `libmpg123-dev`) — both routes legitimate; distro packages are
less work and apt-pinned, vendoring is self-contained and version-pinned.

### The two portability landmines

**Struct layouts.** 205 `MUGEN_ASSERT_SIZE` checks pin field offsets
(`src/mugen.h`). On LP64, `long` (in `bgm.h` callback typedefs) and pointers
grow to 8 bytes; some asserts will fail. Options:
- build `-m32` (faithful, needs multilib dev packages), or
- fix the affected layouts with `int32_t` / explicit padding and accept a
  64-bit build that is functionally equivalent but not binary-faithful.
The asserts identify exactly which structs fight back; no guessing.

**x87 float semantics.** The engine assumes `FLT_EVAL_METHOD == 2`
(x87 53-bit intermediates, rounding on every store) — the Win build pins
`-mfpmath=387 -fexcess-precision=standard` for this. On x86-64, SSE2 math gives
`FLT_EVAL_METHOD == 0`; trigger maths, sprite scaling and parallax will drift.
`-m32` sidesteps it; a 64-bit build must accept the drift and diff-test against
the original (`mugen.log` scenarios in README) rather than expect bit equality.

## Work plan

Ordered; each step leaves the tree buildable on Windows (the existing MSVC and
MinGW builds must keep working — nothing in `src/` or `third_party/` existing
today is modified incompatibly).

| # | Step | What it involves | Status |
|---|---|---|---|
| 1 | Compat shim layer | `src/compat_linux.h` / `compat_linux.c`: CRT shims, `dlopen` typedefs for `HMODULE`/`GetProcAddress`, monotonic clock. Included from `crt.h`/`mugen.h` behind a platform check. Unblocks ~10 files with zero game-logic edits | pending |
| 2 | Vendor zlib + libresample | smallest risk first; removes the two oddest DLLs; wire into Makefile like lua/libpng; patch libresample to the multichannel API of `third_party/sdk/include/libresample.h` | pending |
| 3 | Remove pure-`windows.h` includes | `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `matchload.c`, `mugen.h` — mechanical; include `<GL/gl.h>` directly | pending |
| 4 | Port the 4 real Win32 spots | timer (monotonic clock), fullscreen (SDL), window icon + fonts dir, error dialog (SDL message box). ~200 lines total | pending |
| 5 | SDL stack | vendor `SDL-1.2.15` + `SDL_mixer` + `SDL_ttf` + `freetype` + `SDL_gfx`, or use distro packages; `sdlmain.c` gets a plain `main()` path on Linux; `mugen.rc`/manifest skipped (not a PE) | pending |
| 6 | Layout + float audit | attempt the 64-bit build, let the `MUGEN_ASSERT_SIZE` asserts enumerate the layouts to fix; decide `-m32` vs 64-bit target policy; diff-test `mugen.log` against the original's scenarios | pending |

Effort estimate: steps 1–3 about a day (mechanical); step 4 another day or two
(behavioural); step 6 is where the real time goes for a 64-bit build.
