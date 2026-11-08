# Building the static executable with GNU Make and GCC

`Makefile` builds **`mugen-mingw-win32-sdl2.exe`**: a single self-contained 32-bit Windows
executable with no DLL dependencies besides the Windows system ones. GNU Make +
GCC, as an alternative to the CMake/MSVC build (`CMakeLists.txt`,
`CMakePresets.json`), which links the game's original DLLs instead.

Everything third-party is either compiled from source in `third_party/` or
linked statically from `C:\x86devkit`.

## Zero to build: `setup.bat`

The repository root carries `setup.bat`, which installs the whole Windows build
environment from nothing and can then build both targets:

```bat
setup.bat             :: both devkits + their dependency trees, then verify
setup.bat /build      :: ... and build win32 and win64
setup.bat /build32    :: ... only the reference build
setup.bat /check      :: verify what is installed, install nothing
setup.bat /force      :: re-download and re-extract
```

It downloads the two 2.11.0 devkits from `github.com/skeeto/w64devkit` into
`C:\w64devkit` and `C:\x86devkit` (`W64DEVKIT_DIR`/`X86DEVKIT_DIR` override),
then unpacks this project's published dependency trees into
`build\sysroots\win64-sysroot\` and `build\sysroots\win32-sysroot\`
(`toolchains-v1.0` release: `win64-sysroot.zip`, `win32-sysroot.zip`). The
downloads themselves are cached in `build\downloads\`, all four are checked
against a pinned SHA-256, and unpacking happens in `build\tmp\` — the three
folders under `build\` are the project-wide rule (see `AGENTS.md`). A non-empty
kit directory is left alone unless `/force` is given.

That split matters: **a devkit from skeeto ships no SDL, no libpng and not a
single `.pc` file** — it is gcc, binutils, make, sh/sed/awk and its own
pkg-config. The `.pc` files and the static archives they name come from the
sysroot tree, which is why the same kit works on any machine.

Because the tree sits at a fixed path inside the repository, **the `Makefile`
finds it by itself** — no exported variables are needed:

```sh
export PATH=/c/w64devkit/bin:$PATH      # or /c/x86devkit/bin for win32
make -j8                                # the target follows gcc's own triple
make check-toolchain                    # prints the tree it resolved
```

`WINSYSROOT=<dir>` points the build at another tree; `WINSYSROOT=` (empty)
disables the automatic one and falls back to whatever pkg-config the kit
provides, which is what the build did before the trees existed.

What the Makefile does with it, and why each part is needed:

| Variable | Value | Why |
|---|---|---|
| `PKG_CONFIG_LIBDIR` | `<tree>\usr\lib\pkgconfig` | the kit has no `.pc` files of its own |
| `PKG_CONFIG_PATH` | cleared | an inherited one (MSYS2 exports one) is searched *in addition*, and its SDL2 would win |
| `PKG_CONFIG_SYSTEM_*` | the tree's `usr\include` / `usr\lib` | otherwise pkgconf drops `-I`/`-L` as "system" paths |
| `PKG_CONFIG_ALLOW_SYSTEM_*` | `1` | ... and that is exactly what must not happen here |

All of them use the **Windows spelling** (`C:/...`), which is why the Makefile
runs the resolved path through `cygpath`. `sh` — what make runs recipes in —
does not convert POSIX paths for a native `pkg-config.exe`, so
`PKG_CONFIG_LIBDIR=/c/...` silently matches nothing, and the build then stops
with `pkg-config cannot find: SDL2_mixer SDL2_ttf libpng16` even though the tree
is right there.

For SDL2 controllers missing in either Windows architecture, see
[GAMEPAD_DETECTION.md](GAMEPAD_DETECTION.md) for diagnostics and regression
coverage.

The instructions below are for the environment this project was verified in:

| Tool | Verified with |
|---|---|
| Compiler | `gcc` (GCC) 16.1.0, target `i686-w64-mingw32` |
| Make | GNU Make 4.4.1 |
| Binutils | `i686-w64-mingw32-{ar,windres,strip}` from the same kit |
| `pkg-config` | finds `sdl2.pc`, `SDL2_mixer.pc`, `SDL2_ttf.pc`, `zlib.pc`, `libpng16.pc` (the default backend). For `make SDL_BACKEND=1`: `sdl.pc`, `SDL_gfx.pc`, `SDL_mixer.pc`, `SDL_ttf.pc` |
| Shell | MSYS2 bash (or `sh` from the same kit) |
| Python | any working `python3` / `python` / `py -3` — only for the optional icon |

```sh
make check-toolchain     # verifies the 32-bit toolchain and that the static
                         # archives (not just .pc files) are present
make -j8                 # build/mingw-sdl2/mugen-mingw-win32-sdl2.exe (SDL2 backend)
make install             # strip it and copy it into install/
```

Targets and variables:

| Command | Effect |
|---|---|
| `make` | build `$(TARGET)`; the SDL2 backend by default, so `build/mingw-sdl2/mugen-mingw-win32-sdl2.exe` |
| `make SDL_BACKEND=1` | the SDL 1.2 backend instead: `build/mingw-sdl1/mugen-mingw-win32-sdl1.exe` |
| `make -j8` | parallel build, from scratch in ~30 s on a recent machine |
| `make V=1` | print the full command lines |
| `make DEBUG=1` | `-O0 -g3` build, in its own directory with `BUILD=build/mingw-debug` |
| `make MUGEN_GAME_DIR=C:/mugen-1.1b1` | also embed the original's window icon |
| `make clean` / `make distclean` | remove `$(BUILD)` objects / the whole `$(BUILD)` |
| `make help` | the summary at the top of the Makefile |

Overridable: `CC`, `AR`, `WINDRES`, `DLLTOOL`, `PYTHON3`, `BUILD`, `DEBUG`, `V`.

## Checking compiler warnings

The reported w64devkit 32-bit SDL2 build contained 117 warnings:

| Diagnostics | Count | Cause and correction |
|---|---:|---|
| `-Wcomment` | 89 | A shared header and three source comments contained slash-star wildcard text. Reword the comments; no compiler flag is disabled. |
| `-Waggressive-loop-optimizations` | 1 | AI key 13 read past `keys`. Read and write the adjacent `read` member explicitly to retain the reference behavior. |
| `-Waddress` | 9 | Comparing a computed element address against NULL does not validate the backing storage. Check the storage pointer, and validate select-grid coordinates before indexing. |
| `-Wformat-overflow`, `-Wstringop-overflow`, `-Wrestrict` | 6 | File/QuickVS paths, stage labels, scene headers and the startup directory used incorrect string bounds. Check complete paths, size display buffers correctly, and copy within destination capacity. Scene headers are checked before removing brackets. |
| `-Wmaybe-uninitialized` | 8 | GCC could not establish the loop's previous-scene initialization. Explicitly initialize the baseline; regression tests check actual field inheritance. |
| `-Wincompatible-pointer-types` | 1 | The SDL_mixer deinit slot is `void(void)`, while the old stub returned int. Use a correctly typed no-op and remove the Makefile's error downgrade. |
| `-Warray-bounds` | 1 | An empty counted-string key allocated four bytes for a declared eight-byte object. Both allocation paths now include the declared object and check size arithmetic. |
| `-Wunused-function` | 1 | The direction-source writer is only called by the Android controller backend. Compile that private function only there. |
| `-Wuse-after-free` | 1 | `Resampler_Close` freed a stateless session twice. Free it once. This intentionally fixes the unsafe original behavior. |

The accompanying cross-platform check also corrected SDL1 joystick-name API
usage, public SDL_mixer/SDL_ttf opaque types, missing declarations, timer
pointer types, and victory-quote pointers on 64-bit hosts. Quote selection now
wraps at index 99 rather than reading the following count field as a pointer.
Long generated helper names are bounded. Linux selects Lua's existing
`mkstemp` implementation to avoid its `tmpnam` linker warning.

For a fresh warning check (the compiler flags are not part of the config
stamp, so use a new build directory):

```sh
make -j8 BUILD=build/warnings-sdl2 SDL_BACKEND=2 \
  ENGINE_WARN='-Wall -Werror -Wno-unused-parameter -Wno-unused-result -Wno-stringop-truncation'
make -j8 BUILD=build/warnings-sdl1 SDL_BACKEND=1 \
  ENGINE_WARN='-Wall -Werror -Wno-unused-parameter -Wno-unused-result -Wno-stringop-truncation'
bash tools/test_build_warnings.sh
bash tools/test_cns_abi.sh
```

`WARN01` in `tools/test_qualify.sh` runs the new warning regression suite.
The standalone scripts accept `CC`, `CFLAGS`, `LDFLAGS`, `OUT`, and `RUNNER`;
`SANITIZE=1` enables AddressSanitizer and UndefinedBehaviorSanitizer on a host
that provides them. `MUGEN_TEST_TARGET=win32|win64` uses the shared target
selection and asserts the harness pointer width.

Validation for this change: all 88 source units compiled with the flags above
for i686 MinGW GCC 13.2 (SDL1 and SDL2) and Linux x86-64 GCC 13.3 (SDL2).
The Linux engine linked, and Linux runtime regressions passed with ASan/UBSan.
These are cross-compilation results for Windows, not execution of w64devkit's
Windows-hosted GCC 16.1 or of the Windows game. MSVC, Android/Vita builds and
gameplay log comparisons remain unverified for this change. The Windows-only
`test_zoomdiff.sh` could not run on this Linux host (`-mconsole` and the Windows
SDL/SDL_gfx static kit are required); the scaler change only adds its missing
standard `memcpy` declaration.

## Why a 32-bit toolchain is required

`mugen.exe` is a reconstruction of a 32-bit PE (i386) binary; every struct in
`src/` has the original's field offsets, checked with `MUGEN_ASSERT_SIZE`
(see `docs/ANALYSIS.md`). Those layouts are only correct with 4-byte pointers
and MSVC's 32-bit type sizes, so the build is `-m32` and must use an
`i686-w64-mingw32` toolchain. A 64-bit MinGW (`x86_64-w64-mingw32`) cannot
produce this program; the Makefile sees that triple and builds the experimental
`win64` target instead of this one, and `make check-toolchain` reports which
target the compiler on `PATH` selected (`docs/BUILD-MINGW64.md`).

`make check-toolchain` also requires `pkg-config` to resolve every package in
`STATIC_PKGS` and names the ones it cannot (that check, `check-pkgconfig`, is a
prerequisite of `make`, so a missing `-dev`/`.pc` file fails before compiling
rather than as undefined `Mix_*`/`TTF_*` symbols at the link). The archive check
(`check-libs`) is Windows-only: the whole target branches on the platform being
built, so a Linux host is not asked for `libSDL.a` or `windres`.

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

The SDL2 backend is the default. `make SDL_BACKEND=1` swaps the four SDL rows
for their SDL 1.2 counterparts and drops the rest; nothing else changes.

| Library | Version | How it gets in |
|---|---|---|
| **SDL2** | — | `libSDL2.a` from the kit, via `sdl2.pc` |
| **SDL2_mixer** | 2.8.2 | `libSDL2_mixer.a` via `SDL2_mixer.pc`, plus its own static deps |
| **SDL2_ttf** | — | `libSDL2_ttf.a` via `SDL2_ttf.pc` + freetype, bz2 |
| *(SDL 1.2 only)* | | `libSDL.a` `libSDL_gfx.a` (`zoomSurface`) `libSDL_mixer.a` `libSDL_ttf.a` |
| **zlib** | — | `libz.a`, via `zlib.pc` |
| **pthreads-win32** | — | `libpthread.a` (the kit's static build) |
| **mpg123** | — | `libmpg123.a`, via `SDL2_mixer.pc` / `SDL_mixer.pc` |
| Lua | 5.1.4 | built from `third_party/lua-5.1.4/src` → `liblua.a` |
| libpng | 1.2.38 | built from `third_party/libpng-1.2.38` → `libpng.a` |
| **glad** | 2.1 | `third_party/glad-2.1/glad.c`, generated for this engine (GL 2.1 compatibility profile + 4 extensions). Replaced GLEW 1.5.4 |
| minizip | 0.15 | `third_party/minizip-0.15/unzip.c` |
| **libresample** | 0.1.3 | vendored in `third_party/libresample`, ported to multi-channel |

The kit libraries are not hand-listed: the Makefile asks pkg-config for
`--libs --static` on `sdl2 SDL2_mixer SDL2_ttf zlib libpng16`, which expands to
the whole transitive set (freetype, bz2, ogg, vorbis, opus, FLAC, wavpack, gme,
mpg123, zlib, pthread, stdc++). Hand-writing `-lSDL2_mixer -lSDL2_ttf` would
miss about a dozen archives and fail at link.

The devkit ships its own `pkg-config`, and under MSYS `sh` a POSIX
`PKG_CONFIG_PATH`/`PKG_CONFIG_LIBDIR` is not converted for it, so it matches
nothing — use `C:/...` spellings in make variables. `tools/make_win_sysroot.sh`
turns either kit's dependency set into a relocatable tree
(`win32-sysroot.zip`, `win64-sysroot.zip`, published beside
`arm64-sysroot.zip`) for a machine whose kit lacks the SDL add-ons; both traps
are in `docs/BUILD-MINGW64.md`.

Because `SDL2_mixer` brings mpg123 and vorbisfile with it, **WAV, MP3 and Ogg
Vorbis need no external decoder**: the `sdlmix` plugin decodes them through
SDL_mixer (`MUGEN_SDLMIX_NATIVE_CODECS` in `src/mugen.h`). Neither
`libmpg123-0.dll`, `vorbisplug.dll` nor `waveplug.dll` is required at run time,
and `mugen.cfg` may keep listing them -- a plugin whose DLL is absent is now
skipped instead of aborting BGM start-up, so the built-in `sdlmix` still plays
the music.

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
objdump -p build/mingw-sdl2/mugen-mingw-win32-sdl2.exe | grep "DLL Name"
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

The window icon comes from `res/icons/`, the artwork this repository ships. It
is stored there as one single-size `.ico` per size — `1.ico` 64x64, `2.ico` 48,
`8.ico` 32, `3.ico` 24, `4.ico` 16, all 32-bit with alpha (5, 6, 7 and 9 are the
same drawing as 8-bit palette images). Windows wants all of them in one file: it
picks 16 for the title bar, 32 for the task bar, 48 for Alt+Tab, and scales
whatever is missing, so a single-size icon is blurry everywhere else.
`tools/iconpack.py` merges them into `build/mingw-sdl2/icon/mugen.ico`, which
`windres` embeds as icon group 101. Without a working `python3` the first icon is
copied instead — the build still works, it just carries one size.

Two other sources are possible:

```sh
make MUGEN_ICONS="res/icons/2.ico res/icons/4.ico"   # a different set, merged
make MUGEN_GAME_DIR=C:/mugen-1.1b1                   # the original mugen.exe's icon
```

`windres` needs `-DMUGEN_HAS_ICON` on its own command line: it runs its own
preprocessor, and passing the define to the C compiler alone left icon 101 out
of the executable entirely.

The assembly manifest is embedded as resource `1 24` (RT_MANIFEST); it still
names `Elecbyte.MUGEN.libs`, which is harmless now that nothing loads from it.

## What the build does, in order

1. Compile the engine modules in `src/` into `build/mingw-sdl2/obj/src/`
   (78 original modules plus `src/sdlmixercompat.c`).
2. Compile Lua, libpng, libresample, glad, minizip into `build/mingw-sdl2/obj/` and
   archive the three libraries into `build/mingw-sdl2/lib/`.
3. Generate `build/mingw-sdl2/obj/res/mugen_all.rc` — a copy of `src/mugen.rc` with
   the assembly manifest appended as resource `1 24` — and compile it with
   `windres`. `src/mugen.rc` itself is left untouched, so the CMake build keeps
   adding `src/mugen.manifest` to the linker directly.
4. Link `build/mingw-sdl2/mugen-mingw-win32-sdl2.exe` with `-mwindows` (GUI subsystem,
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
* An `i686-w64-mingw32` toolchain is mandatory for this target. `x86_64-w64-mingw32`
  will compile `-m32` objects but cannot link them (no 32-bit CRT); with that
  compiler on `PATH` the Makefile builds the experimental win64 target instead.
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
