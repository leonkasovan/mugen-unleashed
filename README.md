# mugen-unleashed

A source-level reconstruction of **M.U.G.E.N 1.1.0 Beta 1 P1** (Elecbyte, built
2013-08-11): the original 32-bit Windows binary, rewritten as C and rebuilt from
those sources.

The project aims for *behavioural* fidelity with the original — gameplay, timing,
file formats, log output — not merely a similar-looking game. Where the
reconstruction knowingly deviates, the deviation is marked in the source as a
port change and recorded in the notes under `docs/`.

## What you get

Two Windows builds are supported, and they differ in how they ship:

| Build | Executable | Runtime requirements |
|---|---|---|
| **GNU Make + MinGW (static)** | `mugen-mingw-win32-sdl2.exe` | Nothing but Windows system DLLs |
| **MSVC + CMake** | `mugen-msvc-win32-sdl1.exe` | The game's `Elecbyte.MUGEN.libs` folder (SDL, SDL_mixer, SDL_ttf, SDL_gfx, zlib, libresample, pthreads) |

The static build is self-contained: SDL2, SDL2_mixer, SDL2_ttf, zlib, pthreads,
Lua 5.1.4, libpng, glad, minizip and libresample are all linked in. Because
SDL2_mixer carries mpg123 and vorbisfile internally, WAV, MP3 and Ogg Vorbis
play without any external decoder DLL — including without Elecbyte's
`libmpg123-0.dll`, `vorbisplug.dll` or `waveplug.dll`.

The engine also builds on Linux from the same `Makefile` (x86, x64, arm64), and
there are Android and PS Vita ports under `port/`.

## Building

### Static Windows build (recommended)

Requires `i686-w64-mingw32` GCC, GNU Make, and the static SDL2 stack discoverable
through `pkg-config` (`sdl2`, `SDL2_mixer`, `SDL2_ttf`, `zlib`, `libpng16`).

```sh
make check-toolchain     # verifies the compiler and the static archives
make -j8                 # build/mingw-sdl2/mugen-mingw-win32-sdl2.exe
make install             # strip it and copy it into install/
```

`install/` is a M.U.G.E.N 1.1b1 tree; drop the executable in and run it. No
`Elecbyte.MUGEN.libs` folder is needed.

The SDL2 backend is the default. `make SDL_BACKEND=1` builds the older SDL 1.2
backend instead (`build/mingw-sdl1/mugen-mingw-win32-sdl1.exe`), which exists for the
differential comparisons against the original binary, not as the normal target.

See **[docs/BUILD-MINGW.md](docs/BUILD-MINGW.md)** for the dependency list,
toolchain versions and the variables `make` accepts.

### MSVC 2022

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

Produces `build/x86-release/mugen-msvc-win32-sdl1.exe`. This build links the game's
original DLLs rather than static copies. See
**[docs/BUILD-MSVC.md](docs/BUILD-MSVC.md)**.

### Linux

```sh
make -j8
```

The target (`linux-x64`, `linux-x86`, `linux-arm64`) is chosen from the host and
the compiler's own triple. See **[docs/BUILD-LINUX.md](docs/BUILD-LINUX.md)**.

### MinGW Win64 (experimental)

```sh
export PATH=/c/w64devkit/bin:$PATH
export PKG_CONFIG_PATH=/c/w64devkit/lib/pkgconfig
make MUGEN_TARGET=win64 BUILD=build/win64-sdl2 -j8
```

Produces `build/win64-sdl2/mugen-mingw-win64-sdl2.exe`. See
**[docs/BUILD-MINGW64.md](docs/BUILD-MINGW64.md)**.

### Other platforms

- **Android** — `port/android/`, native SDL2, arm64.
- **PS Vita** — `port/psvita/`.

## Requirements on the machine

The engine is a faithful reconstruction of a 2013 binary, so it inherits its
platform expectations:

- **32-bit is the reference.** The struct layouts, offsets and assertions in
  `src/` describe the original 32-bit binary. Win64 builds link and run (see
  `docs/BUILD-MINGW64.md`): a Win64-vs-Win32 log comparison on the same
  scenario is identical after numeric normalisation. Linux ARM64/x64 link
  but are not yet qualified the same way.
- **x87 float semantics on x86.** The original ran with `/arch:IA32` at 53-bit
  precision; the MinGW builds pass `-mfpmath=387 -fexcess-precision=standard`
  so float rounding matches. Changing this alters gameplay-visible arithmetic.
- **A game directory.** The engine reads `mugen.cfg`, `data/system.def`, fonts,
  stages and characters from the working directory.

## Documentation

| Document | Contents |
|---|---|
| [docs/ANALYSIS.md](docs/ANALYSIS.md) | The original binary: module map, addresses, calling conventions |
| [docs/BUILD-MINGW.md](docs/BUILD-MINGW.md) | The static Windows build in detail |
| [docs/BUILD-MSVC.md](docs/BUILD-MSVC.md) | The MSVC/CMake build in detail |
| [docs/BUILD-LINUX.md](docs/BUILD-LINUX.md) | The Linux build |
| [docs/LUA.md](docs/LUA.md) | The embedded Lua 5.1.4 layer |
| [SDL2_PLAN.md](SDL2_PLAN.md) | The SDL 1.2 → SDL2 migration |
| [PLANS.md](PLANS.md) | Working plan and open items |
| [AGENTS.md](AGENTS.md) | Conventions for working on the code |
| [third_party/README.md](third_party/README.md) | Third-party code, versions and licences |

## Testing

```sh
make check-toolchain         # toolchain and static archives present
bash tools/test_zoomdiff.sh  # SDL2 scaler vs SDL_gfx, byte for byte
```

`tools/test_zoomdiff.sh` builds the same 21 scaling cases against the SDL 1.2
SDL_gfx and the SDL2 replacement and requires every result to be identical. It is
the guard against silent divergence in software rendering.

## Legal

M.U.G.E.N is © Elecbyte. This is an unofficial, non-commercial reconstruction
made for study and preservation. It ships no copyrighted game assets: the
characters, stages, fonts and sound files under `install/` belong to their
respective authors. Third-party libraries keep their own licences, listed in
[third_party/README.md](third_party/README.md).
