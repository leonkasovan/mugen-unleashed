# Building with GNU Make and GCC (MinGW-w64)

`Makefile` builds `mugen.exe` with GNU Make and GCC as an alternative to the
CMake projects (`CMakeLists.txt`, `CMakePresets.json`) used with MSVC. Both
build the same executable from the same sources.

The instructions below are for the environment this project was verified in:

| Tool | Verified with |
|---|---|
| Compiler | `gcc` (GCC) 16.1.0, target `i686-w64-mingw32` |
| Make | GNU Make 4.4.1 |
| Binutils | `i686-w64-mingw32-{ar,windres,dlltool}` from the same kit |
| Shell | MSYS2 bash (or `sh` from the same kit) |
| Python | any working `python3` / `python` / `py -3` — only for the optional icon |

```sh
make check-toolchain     # verifies the toolchain before anything else
make -j8                 # build/mingw/mugen.exe
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

`mugen.exe` has two kinds of dependencies: the libraries reconstructed from
their source, compiled into the executable, and the DLLs that ship with the
game and are imported at run time.

### Built from source, linked statically

| Directory | Version | Built as | Makefile rule |
|---|---|---|---|
| `third_party/lua-5.1.4/src` | Lua 5.1.4 | `build/mingw/lib/liblua.a` (27 files) | `$(LUA_SRCS)` |
| `third_party/libpng-1.2.38` | libpng 1.2.38 | `build/mingw/lib/libpng.a` (`png*.c` minus `pngtest.c`, 17 files) | `$(PNG_SRCS)` |
| `third_party/glew-1.5.4/src/glew.c` | GLEW 1.5.4 | `glew.o`, compiled with `-DGLEW_STATIC` | `$(GLEW_OBJ)` |
| `third_party/minizip-0.15/unzip.c` | unzip 0.15 | `unzip.o` (reading characters from `.zip`) | `$(MINIZIP_OBJ)` |
| `src/*.c` | the engine | 78 objects, one per module | `$(ENGINE_SRCS)` |
| `src/lookup3.c` | Bob Jenkins lookup3 | part of the engine | — |
| `src/sdlmain.c` | SDL 1.2.15 `SDLmain` | part of the engine (the `WinMain` entry) | — |

`pnggccrd.c` and `pngvcrd.c` compile to nothing unless libpng is configured for
its hand-written MMX/SSE2 paths, so libpng is built on its portable C path, as
in the CMake build.

### DLLs, resolved at run time

The original links dynamically against the DLLs of the private assembly
`Elecbyte.MUGEN.libs` next to the executable. This project does not ship those
DLLs, only the *export lists* (`third_party/sdk/def/*.def`) of the exact
versions the game used. `dlltool -d <def> -l lib<name>.a` turns each list into
an import library (`build/mingw/implib/`), so the executable imports exactly
the same symbols in the same order as the original. The headers in
`third_party/sdk/include/` are the matching import-time headers.

| Import library | DLL | Version | Used for |
|---|---|---|---|
| `libSDL.a` | `SDL.dll` | 1.2.15 | window, events, keyboard, joysticks, audio |
| `libSDL_mixer.a` | `SDL_mixer.dll` | 1.2.11 | sound effects, MIDI/tracker music |
| `libSDL_ttf.a` | `SDL_ttf.dll` | 2.0.11 | TrueType fonts |
| `libSDL_gfx.a` | `SDL_gfx.dll` | — | `zoomSurface` |
| `libzlib1.a` | `zlib1.dll` | 1.2.3 | inflate/deflate (SFF v2, PNG) — **not** the MinGW `-lz` |
| `liblibresample.a` | `libresample.dll` | 0.1.3 | audio resampling |
| `libpthreadVC2.a` | `pthreadVC2.dll` | 2.8.0 | the BGM thread |

At run time `libmpg123-0.dll` is also needed by the built-in `mpg123` BGM
plugin, but it is loaded with `LoadLibrary`, so it is not an import of the
executable.

To run the executable you need a M.U.G.E.N 1.1 game directory containing those
DLLs (they are in the original game's `Elecbyte.MUGEN.libs`), with this
`mugen.exe` copied over the original.

### Windows system libraries

`opengl32`, `glu32`, `winmm` (timers, `timeGetTime`), `shell32`, `user32`,
`gdi32` — the same list as `target_link_libraries()` in `CMakeLists.txt`.

### Tools used by the build

| Tool | Used for | Optional |
|---|---|---|
| `gcc` (`i686-w64-mingw32`) | compiling, linking | no |
| `ar` | `liblua.a`, `libpng.a` | no |
| `dlltool` | import libraries from the `.def` files | no |
| `windres` | `src/mugen.rc` + the assembly manifest into a `.res` object | no |
| `python3` / `python` / `py -3` | `tools/extract_icon.py`, only with `MUGEN_GAME_DIR` (needs `pefile`) | yes |

`python3` on Windows is very often the Microsoft Store alias, which exists but
does nothing; the Makefile therefore runs each candidate once and uses the
first that works.

## What the build does, in order

1. Compile the 78 engine modules into `build/mingw/obj/src/`.
2. Compile Lua, libpng, GLEW, minizip into `build/mingw/obj/` and archive the
   two libraries into `build/mingw/lib/`.
3. Generate the import libraries of the game's DLLs with `dlltool` from
   `third_party/sdk/def/`.
4. Generate `build/mingw/obj/res/mugen_all.rc` — a copy of `src/mugen.rc` with
   the assembly manifest appended as resource `1 24` (RT_MANIFEST) — and compile
   it with `windres`. `src/mugen.rc` itself is left untouched, so the CMake
   build keeps adding `src/mugen.manifest` to the linker directly.
5. Link `build/mingw/mugen.exe` with `-mwindows` (GUI subsystem, `WinMain`
   from `src/sdlmain.c`).

Nothing in `src/` or `third_party/` is modified by the build.

## Portability notes

* Header include order matters for libpng and minizip: they are compiled with
  `third_party/sdk/include` (the original zlib 1.2.3 headers) ahead of anything
  else, so they see the same zlib API they were written against. Do not add
  `-I` paths that shadow `zlib.h`/`zconf.h`.
* The engine is compiled with the SDL 1.2 / pthreads-win32 headers from
  `third_party/sdk/include`, *not* with SDL2/SDL3 or a system pthread, which
  are ABI-incompatible with the DLLs the game loads.
* GCC 14 and later make `-Wincompatible-pointer-types` an error; the Makefile
  turns that specific diagnostic back into a warning (`src/globals.c` has one,
  from the original's BGM subsystem table).
* `-fno-strict-aliasing` is deliberate: the reconstruction aliases the way the
  original machine code did.
