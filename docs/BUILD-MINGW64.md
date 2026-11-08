# MinGW Win64 build (experimental)

A 64-bit toolchain (`x86_64-w64-mingw32` -- the w64devkit here) builds
`mugen-mingw-win64-sdl2.exe`: the engine as a 64-bit static binary against the
64-bit SDL2 kit. Nothing has to be asked for: the Makefile reads the compiler's
own target triple, so a plain `make` builds this target whenever the 64-bit
kit's `gcc` is the one on `PATH` -- and says so, because it is not the reference
build. `MUGEN_TARGET=win64` still works and is what pins the choice when the
compiler is not the one first on `PATH`.

## Toolchain

The 64-bit kit needs its own `bin` dir first on `PATH` (its `gcc.exe` is a
wrapper that finds `cc1` there) plus `PKG_CONFIG_PATH` at its pkgconfig dir.
`make check-toolchain` verifies the triple, the SDL2/zlib/libpng16 packages
and the static archives, and prints the target and output path it resolved:

```sh
export PATH=/c/w64devkit/bin:$PATH
export PKG_CONFIG_PATH=/c/w64devkit/lib/pkgconfig
make check-toolchain      # target: win64 (64-bit) -> build/mingw64-sdl2/...
make -j8                  # build/mingw64-sdl2/mugen-mingw-win64-sdl2.exe

# explicit form, e.g. with a directory of your choice
make MUGEN_TARGET=win64 BUILD=build/win64-sdl2 -j8
```

The 32-bit reference build is then just the other kit: put `x86devkit` first on
`PATH` (its gcc's triple is `i686-w64-mingw32`, which selects win32) — see
`docs/BUILD-MINGW.md`.

### Two traps in that `PKG_CONFIG_PATH`

**The kit ships its own `pkg-config`** (0.34 at the time of writing). With the
kit's `bin` first on `PATH` that one wins over MSYS2's pkgconf, and it is a
native Windows binary — MSYS `/bin/sh`, which is what make runs recipes in, does
**not** convert POSIX paths for native executables (`/c/msys64/...` even fails to
exec: `env: can't execute ...: No such file or directory`). So a POSIX value
reaches pkg-config unconverted and silently matches nothing:

```sh
# in a make recipe (sh), with the kit's pkg-config in front:
env PKG_CONFIG_LIBDIR=/c/proj/build/sysroots/win64-sysroot/usr/lib/pkgconfig pkg-config --exists sdl2   # rc=1
env PKG_CONFIG_LIBDIR=C:/proj/build/sysroots/win64-sysroot/usr/lib/pkgconfig pkg-config --exists sdl2   # rc=0
```

The same value exported from an interactive bash works, which makes this look
like a missing package. Use the Windows spelling (`C:/...`) for anything that
ends up in a make variable or a recipe, or keep MSYS2's `pkg-config` first on
`PATH`.

**A kit without the SDL add-ons fails `check-pkgconfig`.** `tools/make_win_sysroot.sh`
packages one kit's dependency set as a relocatable tree the same shape as
`arm64-sysroot.zip` — headers, static archives, and `.pc` files with
`prefix=${pcfiledir}/../..`:

```sh
tools/make_win_sysroot.sh C:/w64devkit win64    # -> build/sysroots/win64-sysroot
tools/make_win_sysroot.sh C:/x86devkit win32    # -> build/sysroots/win32-sysroot
```

The tree lands under `build/sysroots/` (and the zip to publish in `build/tmp/`),
which is exactly where `setup.bat` puts the downloaded ones, so the `Makefile`
consumes it with no variables at all: it points `PKG_CONFIG_LIBDIR` at
`<tree>/usr/lib/pkgconfig` in Windows spelling (see above), sets
`PKG_CONFIG_SYSTEM_INCLUDE_PATH`/`_LIBRARY_PATH` at the tree's
`usr/include`/`usr/lib` and the `PKG_CONFIG_ALLOW_SYSTEM_*` flags. `WINSYSROOT=`
turns that off if a kit is prepared by hand instead.

Verified by building both targets with nothing but the unpacked tree for their
dependencies (a clean 2.11.0 kit has no SDL at all); the result imports only
Windows system DLLs, so SDL2 is linked statically as before.

`SDL_BACKEND=1` is rejected for this target (SDL 1.2 is win32-only, see the
Makefile guard).

## What differs from the win32 build

- Pointer-sized struct fields move: `Bg.legacy` 0x64→0x7c,
  `Projectile.afterimage` 0x504→0x548. The engine reads members, never
  hardcoded offsets, at these sites (`src/camera.c`, `src/ai.c`,
  `src/char.c`). `tools/test_abi_layout.sh` prints both layouts and the
  expected divergences.
- `System_SetWindowIcon` uses `SetClassLongPtrA`/`GCLP_HICON`: `SetClassLong`
  truncates the `HICON` on 64-bit Windows.
- The BGM thread uses native pthread storage (see `BGM_NATIVE_PTHREAD` in
  `src/mugen.h`): the 32-bit pthreads-win32 shapes do not fit 8-byte
  pointers.

## Validation

Run the same scenario under Win32 and Win64 with the same timeout and
compare with `tools/compare_logs.py`:

```sh
cd install
rm -f mugen.log
timeout 60 ./mugen-mingw-win32-sdl2.exe -nosound -p1 kfm -p2 kfm -rounds 2
cp mugen.log ../build/win32-quickvs.log
rm -f mugen.log
timeout 60 ../build/mingw64-sdl2/mugen-mingw-win64-sdl2.exe -nosound -p1 kfm -p2 kfm -rounds 2
cp mugen.log ../build/win64-quickvs.log
cd ..
python tools/compare_logs.py build/win32-quickvs.log build/win64-quickvs.log
```

`MATCH: logs are identical after numeric normalisation` is the gate.
`install/` is the pristine game tree: remove the copied binary and both logs
when done (see AGENTS.md).
