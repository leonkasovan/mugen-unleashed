# Building on Linux

`Makefile` builds the engine on Linux as well as Windows, choosing the target
from `uname` and the compiler's own triple. This page covers the Linux side;
`BUILD-MINGW.md` covers the Windows static build, which is the reference.

```sh
make -j8
make install          # strips and copies into install/mugen-linux-x64
```

→ `build/linux-x64/mugen-linux-x64` (or `build/linux-arm64/…`), installed under
the same name so an ELF binary never ends up called `.exe`.

Checks follow the platform: `make check-toolchain` verifies the compiler's
triple against `MUGEN_TARGET` (`^x86_64-` for `linux-x64`, and so on) and that
`ar`/`strip` are present — no `libSDL.a` or `windres`, those are Windows-only.
`make check-pkgconfig` names every missing `.pc` file; a plain `make` runs it
first, so a missing `-dev` package is reported before compiling rather than as
undefined `Mix_*`/`TTF_*` symbols at the link.

## Target detection

```make
UNAME_S := $(shell uname -s 2>/dev/null)          # OS family
TRIPLE  := $(shell $(CC) -dumpmachine 2>/dev/null) # CPU + ABI
```

| `uname -s` | `-dumpmachine` | Target | Output |
|---|---|---|---|
| `Windows_NT`, `MINGW*`, `MSYS*`, `CYGWIN*` | `i686-w64-mingw32` | `win32` | `mugen-static.exe` |
| `Linux` | `x86_64-*-linux-gnu` | `linux-x64` | `mugen-linux-x64` |
| `Linux` | `aarch64-*-linux-gnu` | `linux-arm64` | `mugen-linux-arm64` |
| `Linux` | `i686-*-linux-gnu` | `linux-x86` | `mugen-linux-x86` |
| anything else | — | hard error naming the triple found |

**`uname -m` never decides the architecture.** Inside MSYS it reports the host
(`x86_64` here) while the compiler targets `i686-w64-mingw32`, so trusting it
would select 64-bit flags for a binary that cannot be linked. The compiler wins
on any disagreement; `uname -m` is only reported by `check-toolchain`.

Override for cross-compiling with `MUGEN_TARGET=linux-arm64`.

## Dependencies (Ubuntu 22.04)

```sh
sudo apt install build-essential pkg-config \
  libsdl1.2-dev libsdl-mixer1.2-dev libsdl-ttf2.0-dev libsdl-gfx1.2-dev \
  zlib1g-dev libpng-dev libmpg123-dev libgl1-mesa-dev libglu1-mesa-dev \
  libfreetype-dev libbz2-dev
```

Verified versions on a stock 22.04:

| Package | Version | Why |
|---|---|---|
| `libsdl1.2-dev` | **1.2.15** | the exact SDL the engine targets |
| `libsdl-mixer1.2-dev` | 1.2.12 | `Mix_*` |
| `libsdl-ttf2.0-dev` | 2.0.11 | `TTF_*` |
| `libsdl-gfx1.2-dev` | 2.0.25 | `zoomSurface` |
| `libpng-dev` | 1.6.37 | SFF v2 / PNG |
| `zlib1g-dev` | 1.2.11 | inflate/deflate |
| `libmpg123-dev` | 1.29.3 | BGM plugin |
| `libgl1-mesa-dev`, `libglu1-mesa-dev` | 23.2.1 / 9.0.2 | `<GL/gl.h>`, `gluOrtho2D` |

Lua 5.1.4, glad 2.1, minizip 0.15 and libresample are built from
`third_party/`, so no distro Lua is needed (and 5.1 is often unavailable).

Unlike the Windows build, Linux links the distro's **shared** SDL. That is the
normal thing to do on Linux and means SDL_mixer's codecs come with it.

## What `src/compat_linux.h` provides

The engine was reconstructed from a 32-bit Windows PE, so `compat_linux.h`
(force-included for the engine only) supplies the names that have a faithful
POSIX equivalent:

| Kind | Detail |
|---|---|
| `__int64` | `__INT64_TYPE__` — must equal `int64_t`, which is `long` on LP64 and `long long` on 32-bit. A fixed spelling conflicts with `int64_t` prototypes in `symbols.h` |
| `__cdecl`, `__stdcall` | empty; System V has one integer calling convention |
| `_stricmp`, `_strnicmp`, `_snprintf`, `_vsnprintf`, `_alloca`, `_mkdir`, `_getcwd`, `_chdir`, `_isnan`, `_strlwr` | MSVC spellings of POSIX/standard calls (154 sites) |
| `BOOL`, `DWORD`, `HWND`, `RECT`, `LARGE_INTEGER`, … | Win32 type names |
| `LoadLibraryA`, `GetProcAddress`, `FreeLibrary` | `dlopen` / `dlsym` / `dlclose` |
| `timeGetTime`, `QueryPerformanceCounter/Frequency` | `clock_gettime(CLOCK_MONOTONIC)`. The frequency is **10,000,000** (100 ns), the original's, so `mugen.log` matches |
| `Sleep`, `InterlockedExchangeAdd` | `nanosleep`, `__sync_fetch_and_add` |
| `_strlwr` | implemented in `compat_linux.c` (glibc has no in-place lowercase) |

Not emulated at all: window styles, the RichEdit error dialog and the window
icon are Windows-specific, and are handled with `#ifdef` at their call sites
instead of pretending a POSIX equivalent exists.

## Deliberate differences from the original

Each is marked in the code with a comment. None affect Windows.

| Area | Difference |
|---|---|
| Error dialog | writes the message to `stderr` instead of a modal RichEdit dialog built from PE resource 200 |
| Window icon | not set; the icon lives in a PE resource the Linux build has no access to |
| Full screen | `SDL_WM_ToggleFullScreen` instead of poking `HWND` styles and the primary monitor's rectangle |
| `-log` / stdout redirection | `sdlmain.c` (SDL 1.2's WinMain wrapper) is Windows-only; `main.c` is the entry on Linux |
| `g_triggersV11` loop | bounded by the array length. The original's table is the only one with no `-1` terminator and relied on reading into `g_sctrlsV10` to stop |

## Struct layouts on 64-bit

`MUGEN_ASSERT_SIZE`/`MUGEN_ASSERT_OFFSET` are wrapped in `MUGEN_LAYOUT_32BIT`,
which is 1 when `sizeof(void *) == 4`. On 64-bit they become
`_Static_assert`s that only check the type is complete and the field exists, so
a typo still fails.

Of the 200 asserted structs, 105 contain pointers and necessarily move. That is
safe because nothing in `src/` indexes a struct by absolute offset — access is
always by member name — and there are no truncating pointer↔int casts.

## 64-bit bugs found and fixed

All were found by running under AddressSanitizer, not by inspection. Each is
marked `PORTABILITY FIX` in the source, and each is a no-op on 32-bit because it
replaces a hardcoded byte count with the matching `sizeof`.

| Site | Bug |
|---|---|
| `ini.c` `Ini_Parse` | `lines_bytes += 0x400` per 0x100 new entries assumes 4-byte pointers; `line_map` (int) shared the same byte count as `lines` (char\*) |
| `ini.c` src lines | `Mem_Calloc(1, 0x400)` and a `+ 0x400` growth assume 4-byte pointers |
| `system.c` `GameVars_Init` | `calloc(1, 0xC)` is `sizeof(PalGroups)` at 32-bit; the struct is 16 bytes on LP64, so `Match_FreeAssets` wrote past it |
| `select.h`, `syselem.h` | `cutscene`, `key_table`, `text_ptr` are declared `int` but store pointers. Now `intptr_t`: same 4 bytes and offset on 32-bit, 8 on LP64 |
| `gameloop.c`, `storyboard.c`, `title.c`, `versus.c` | the matching stores cast through `(int)`, which re-truncated the pointer |
| `trigger.c` | bounded the `g_triggersV11` loop (see above) |

The one remaining ASan finding is **not** fixed on purpose: `Hash_Lookup3` reads
up to 3 bytes past the key, which is Bob Jenkins' documented design in the
aligned fast path and behaves identically on Windows. Suppressing it in the
sanitizer (rather than "fixing" vendored code) is the right call.

To reproduce the hunt:

```sh
make SANITIZE=address BUILD=build/asan-x64
```

`SANITIZE` adds `-fsanitize=` to both the compile and link lines.

## Verification

| Check | Result |
|---|---|
| `make` on a clean tree | 117 compile/archive/link steps, no errors |
| Startup → mode select → match → shutdown | runs to completion, no crash |
| `mugen.log` vs the Windows static build, quick VS | **identical** on the shared 95 lines (93 fragments each) |
| `mugen.log` vs the original `mugen.exe` (Windows) | still identical, so the port did not disturb the reference build |

The Linux run reaches a clean `Successful program termination.` where the
Windows run under a timeout does not — which is why the two logs differ in
length and why only the common prefix is compared.

## arm64

Not built yet. Structurally the `Makefile` selects it and the code compiles the
same way, but **AArch64 has no x87**, so `-mfpmath=387` is dropped and
`FLT_EVAL_METHOD` is 0. Float results are expected to drift from the reference
by an ULP, so `mugen.log` will not match exactly and the numeric lines have to
be audited by hand. That is a property of the architecture, not of this port.
