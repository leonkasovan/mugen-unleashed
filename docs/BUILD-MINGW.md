# Building the static executable with GNU Make and GCC

`Makefile` builds **`mugen-static.exe`**: a single self-contained 32-bit Windows
executable with no DLL dependencies besides the Windows system ones. GNU Make +
GCC, as an alternative to the CMake/MSVC build (`CMakeLists.txt`,
`CMakePresets.json`), which links the game's original DLLs instead.

Everything third-party is either compiled from source in `third_party/` or
linked statically from `C:\x86devkit`.

The instructions below are for the environment this project was verified in:

| Tool | Verified with |
|---|---|
| Compiler | `gcc` (GCC) 16.1.0, target `i686-w64-mingw32` |
| Make | GNU Make 4.4.1 |
| Binutils | `i686-w64-mingw32-{ar,windres,strip}` from the same kit |
| `pkg-config` | finds `sdl.pc`, `SDL_gfx.pc`, `SDL_mixer.pc`, `SDL_ttf.pc` |
| Shell | MSYS2 bash (or `sh` from the same kit) |
| Python | any working `python3` / `python` / `py -3` — only for the optional icon |

```sh
make check-toolchain     # verifies the 32-bit toolchain and that the static
                         # archives (not just .pc files) are present
make -j8                 # build/mingw/mugen-static.exe
make install             # strip it and copy it into install/
```

Targets and variables:

| Command | Effect |
|---|---|
| `make` | build `$(BUILD)/mugen.exe` (`BUILD` defaults to `build/mingw`) |
| `make -j8` | parallel build, from scratch in ~30 s on a recent machine |
| `make V=1` | print the full command lines |
| `make DEBUG=1` | `-O0 -g3` build, in its own directory with `BUILD=build/mingw-debug` |
| `make MUGEN_GAME_DIR=C:/mugen-1.1b1` | also embed the original's window icon |
| `make clean` / `make distclean` | remove `$(BUILD)` objects / the whole `$(BUILD)` |
| `make help` | the summary at the top of the Makefile |

Overridable: `CC`, `AR`, `WINDRES`, `DLLTOOL`, `PYTHON3`, `BUILD`, `DEBUG`, `V`.

## Why a 32-bit toolchain is required

`mugen.exe` is a reconstruction of a 32-bit PE (i386) binary; every struct in
`src/` has the original's field offsets, checked with `MUGEN_ASSERT_SIZE`
(see `docs/ANALYSIS.md`). Those layouts are only correct with 4-byte pointers
and MSVC's 32-bit type sizes, so the build is `-m32` and must use an
`i686-w64-mingw32` toolchain. A 64-bit MinGW (`x86_64-w64-mingw32`) cannot
produce this program. `make check-toolchain` refuses anything else.

Two other MSVC behaviours are reproduced with flags:

| MSVC (original build) | GCC here | Why |
|---|---|---|
| `/arch:IA32` (x87, no SSE arithmetic) | `-m32 -mfpmath=387` (the i686 default) | the reconstructed float maths rounds like the original |
| x87 kept at 53-bit precision per expression | `-fexcess-precision=standard` | rounding happens on assignment to `float`/`double`, which the code assumes |
| `/W3 /wd4100 /utf-8` | `-Wall` minus a few, `-fno-strict-aliasing` | plain C from the binary |
| `/W0` for third-party code | `-w` | their warnings are not ours to fix |
| `/MT` static CRT (VC9) | MinGW's static CRT (default) | `msvcrt.dll` instead of `MSVCR90.dll`; the manifest drops the VC90 dependency |

## Dependencies

Every dependency is resolved at build time. The executable needs **no** DLL
from the game.

### Linked statically

| Library | Version | How it gets in |
|---|---|---|
| **SDL** | 1.2.15 | `libSDL.a` from the kit, via `sdl.pc` |
| **SDL_gfx** | — | `libSDL_gfx.a` via `SDL_gfx.pc` (`zoomSurface`) |
| **SDL_mixer** | — | `libSDL_mixer.a` via `SDL_mixer.pc`, plus its own static deps |
| **SDL_ttf** | — | `libSDL_ttf.a` via `SDL_ttf.pc` + freetype, bz2 |
| **zlib** | — | `libz.a`, pulled in through `SDL_image`/`SDL_ttf`'s `.pc` |
| **pthreads-win32** | — | `libpthread.a` (the kit's static build) |
| **mpg123** | — | `libmpg123.a`, via `SDL_mixer.pc` |
| Lua | 5.1.4 | built from `third_party/lua-5.1.4/src` → `liblua.a` |
| libpng | 1.2.38 | built from `third_party/libpng-1.2.38` → `libpng.a` |
| GLEW | 1.5.4 | `third_party/glew-1.5.4/src/glew.c` with `-DGLEW_STATIC` |
| minizip | 0.15 | `third_party/minizip-0.15/unzip.c` |
| **libresample** | 0.1.3 | vendored in `third_party/libresample`, ported to multi-channel |

The kit libraries are not hand-listed: the Makefile asks pkg-config for
`--libs --static` on `sdl SDL_gfx SDL_mixer SDL_ttf`, which expands to the whole
transitive set (freetype, bz2, ogg, vorbis, opus, FLAC, wavpack, gme, mpg123,
zlib, pthread, stdc++). Hand-writing `-lSDL_mixer -lSDL_ttf` would miss about a
dozen archives and fail at link.

Two headers need a define to stop declaring `dllimport`:

| Define | Header | Why |
|---|---|---|
| `PTW32_STATIC_LIB` | `third_party/sdk/include/pthread.h` | its own `PTW32_DLLPORT` logic already honours it |
| `LIBRESAMPLE_STATIC` | `third_party/libresample/include/libresample.h` | our vendored header |

`third_party/sdk/include/libresample.h` (the original DLL's header) declares
`dllimport` unconditionally, so `third_party/libresample/include` **must come
first** on the include path. That is why `CPPFLAGS` orders it before
`$(SDK_DIR)/include`.

### libresample is vendored, not upstream

Elecbyte's `libresample.dll` is a private multi-channel fork: its
`resample_open` takes a channel count and its buffers are interleaved frames,
neither of which upstream 0.1.3 supports. `third_party/libresample` is upstream
with the channel parameter restored. `third_party/libresample/VENDORED.md`
documents every change; `tools/check_libresample.c` verifies that one channel is
bit-identical to upstream and that N channels equal N independent mono runs.

### Mix_QuerySpecEx

`src/sound.c` calls `Mix_QuerySpecEx`, an Elecbyte addition to SDL_mixer that
stock SDL_mixer 1.2 does not have (it has `Mix_QuerySpec`, without the chunk
size). The static build therefore compiles `src/sdlmixercompat.c`, which wraps
`Mix_QuerySpec` and reports the chunk size the engine requested. The CMake/MSVC
build does not compile that file — it links the game's real `SDL_mixer.dll`.

### What the executable still imports

```
ADVAPI32.dll  GDI32.dll  GLU32.DLL  KERNEL32.dll  OPENGL32.DLL
SHELL32.dll   SHLWAPI.dll  USER32.dll  WINMM.DLL  msvcrt.dll
```

Windows system DLLs only, plus `msvcrt.dll` (MinGW's CRT, present on every
Windows since XP). Verified with:

```sh
objdump -p build/mingw/mugen-static.exe | grep "DLL Name"
```

Note the executable *exports* a few hundred SDL/Mixer/TTF names. Those come
from `.drectve` sections inside `libSDL.a` and cannot be removed with a command
line `--exclude-all-symbols`. Only library names are exported, never engine
symbols, so it is harmless.

### Windows system libraries

`opengl32`, `glu32`, `winmm` (timers, `timeGetTime`), `shell32`, `user32`,
`gdi32` — the same list as `target_link_libraries()` in `CMakeLists.txt`.

### Tools used by the build

| Tool | Used for | Optional |
|---|---|---|
| `gcc` (`i686-w64-mingw32`) | compiling, linking | no |
| `ar` | `liblua.a`, `libpng.a`, `libresample.a` | no |
| `pkg-config` | locating the static SDL stack and its dependencies | no |
| `windres` | `src/mugen.rc` + the assembly manifest into a `.res` object | no |
| `strip` | `make install` | no |
| `python3` / `python` / `py -3` | `tools/extract_icon.py`, only with `MUGEN_GAME_DIR` (needs `pefile`) | yes |

`python3` on Windows is very often the Microsoft Store alias, which exists but
does nothing; the Makefile therefore runs each candidate once and uses the
first that works.

## Icon and manifest

`src/mugen.rc` includes icon 101 only when `MUGEN_HAS_ICON` is defined, which
the Makefile sets when `MUGEN_GAME_DIR` holds a `mugen.exe` the extraction
script can read. The assembly manifest is embedded as resource `1 24`
(RT_MANIFEST); it still names `Elecbyte.MUGEN.libs`, which is harmless now that
nothing loads from it.

## What the build does, in order

1. Compile the engine modules in `src/` into `build/mingw/obj/src/`
   (78 original modules plus `src/sdlmixercompat.c`).
2. Compile Lua, libpng, libresample, GLEW, minizip into `build/mingw/obj/` and
   archive the three libraries into `build/mingw/lib/`.
3. Generate `build/mingw/obj/res/mugen_all.rc` — a copy of `src/mugen.rc` with
   the assembly manifest appended as resource `1 24` — and compile it with
   `windres`. `src/mugen.rc` itself is left untouched, so the CMake build keeps
   adding `src/mugen.manifest` to the linker directly.
4. Link `build/mingw/mugen-static.exe` with `-mwindows` (GUI subsystem,
   `WinMain` from `src/sdlmain.c`) and `-Wl,--gc-sections` so unreferenced
   archive members are dropped.
5. `make install` strips a copy and puts it in `install/`.

Nothing in `third_party/` is modified by the build. `src/sdlmixercompat.c` is
new and static-build only; it is not an original module and is not in the CMake
build.

## Portability notes

* **SDL headers now come from the kit**, not `third_party/sdk/include/SDL`.
  The kit's are the same 1.2.15 API and match the archives we link. The
  `third_party/sdk/include/SDL` copy stays for the MSVC build, which still uses
  the game's DLLs.
* **Include order is load-bearing.** `third_party/libresample/include` must
  precede `third_party/sdk/include`, because the latter's `libresample.h`
  declares `dllimport` unconditionally. Likewise `third_party/sdk/include`
  stays ahead of anything that would shadow `zlib.h`/`zconf.h` for libpng and
  minizip.
* An `i686-w64-mingw32` toolchain is mandatory. `x86_64-w64-mingw32` will
  compile `-m32` objects but cannot link them (no 32-bit CRT).
* GCC 14 and later make `-Wincompatible-pointer-types` an error; the Makefile
  turns that specific diagnostic back into a warning (`src/globals.c` has one,
  from the original's BGM subsystem table).
* `-fno-strict-aliasing` is deliberate: the reconstruction aliases the way the
  original machine code did.
* The static link is **large** (~10 MB unstripped). `make install` strips it;
  expect roughly 3-4 MB after stripping.

## Validating the build

Behaviour is checked by running both binaries on the same scenario and diffing
`mugen.log` — see `AGENTS.md`, "Testing", and `tools/compare_logs.py`.

The static build was verified this way against the original `mugen.exe`:

| Scenario | Result |
|---|---|
| startup, mode select | `mugen.log` identical after numeric normalisation |
| quick VS (`-p1 kfm -p2 kfm`) | `mugen.log` identical |

Both binaries must be given the **same timeout**: the original runs until
closed, so a shorter run on one side truncates its log and produces a
difference that is an artefact of the test, not the build.
