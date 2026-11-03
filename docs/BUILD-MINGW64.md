# MinGW Win64 build (experimental)

`MUGEN_TARGET=win64` builds `mugen-mingw-win64-sdl2.exe`: the engine as a
64-bit static binary against the 64-bit SDL2 kit in `C:/w64devkit`.

## Toolchain

The 64-bit kit needs its own `bin` dir first on `PATH` (its `gcc.exe` is a
wrapper that finds `cc1` there) plus `PKG_CONFIG_PATH` at its pkgconfig dir.
`make check-toolchain` verifies the triple, the SDL2/zlib/libpng16 packages
and the static archives:

```sh
export PATH=/c/w64devkit/bin:$PATH
export PKG_CONFIG_PATH=/c/w64devkit/lib/pkgconfig
make MUGEN_TARGET=win64 check-toolchain
make MUGEN_TARGET=win64 BUILD=build/win64-sdl2 -j8
```

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
timeout 60 ../build/win64-sdl2/mugen-mingw-win64-sdl2.exe -nosound -p1 kfm -p2 kfm -rounds 2
cp mugen.log ../build/win64-quickvs.log
cd ..
python tools/compare_logs.py build/win32-quickvs.log build/win64-quickvs.log
```

`MATCH: logs are identical after numeric normalisation` is the gate.
`install/` is the pristine game tree: remove the copied binary and both logs
when done (see AGENTS.md).
