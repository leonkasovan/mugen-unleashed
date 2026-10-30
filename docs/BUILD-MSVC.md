# Building with MSVC 2022 (Visual Studio)

`CMakeLists.txt` + `CMakePresets.json` build `mugen_unleashed.exe` with
Visual Studio 2022's C compiler. This is the primary Windows build; there is
also a [GNU Make + MinGW build](BUILD-MINGW.md).

## Requirements

| Component | Needed for | Notes |
|---|---|---|
| Visual Studio 2022, **Desktop development with C++** | `cl.exe`, `link.exe`, `rc.exe`, `mt.exe` | the workload must include the **x86** tools |
| MSVC v143 x86/x64 build tools | the compiler | verified with `14.44.35207` (cl 19.44) |
| Windows 10/11 SDK | system headers, `rc.exe`, `mt.exe` | verified with `10.0.26100` |
| CMake ≥ 3.20 | the build system | the copy bundled with VS is enough |
| Ninja | the generator the presets use | bundled with VS |
| Python 3 + `pefile` | **optional** — only to embed the game's window icon | `pip install pefile` |

The presets set `CMAKE_C_COMPILER=cl`, so the MSVC environment must already be
set up when CMake runs. `tools\msvc_env.bat` does that.

## Toolchain discovery

`tools\msvc_env.bat` locates any VS 2022 install with the x86 C++ tools and
runs `vcvarsall.bat x86`:

```bat
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if not defined VSINSTALLDIR (
  for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
)
if not defined VSINSTALLDIR call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
if "%~1"=="" exit /b 0
%*
```

Because it uses `vswhere`, it works regardless of the edition or install
path. With VS 2022 Professional in the default location that resolves to:

```
C:\Program Files\Microsoft Visual Studio\2022\Professional
C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat
```

If `%VSINSTALLDIR%` is already defined (for example inside a *Developer
Command Prompt*), the batch file uses that instead and skips `vswhere`.

To check it works, run it with no arguments and then:

```bat
tools\msvc_env.bat && cl
```

which must print a banner mentioning **x86** (not x64).

## Building

From a *regular* command prompt — no need to open a Developer prompt, the
batch file sets up the environment for the command it runs:

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

Output:

```
build\x86-release\mugen_unleashed.exe
build\x86-release\mugen_unleashed.pdb      (debug symbols, RelWithDebInfo)
```

The `x86-debug` preset builds an unoptimised executable in its own directory:

```bat
tools\msvc_env.bat cmake --preset x86-debug
tools\msvc_env.bat cmake --build --preset x86-debug
```

→ `build\x86-debug\mugen_unleashed.exe`

### Presets

| Preset | Build type | Notes |
|---|---|---|
| `x86-release` | `RelWithDebInfo` | optimised, keeps a PDB — the release artefact |
| `x86-debug` | `Debug` | unoptimised, `/MTd` static debug CRT |

Both generate Ninja into `build\<preset>`.

### Embedding the window icon

`src/mugen.rc` includes icon 101 only when `MUGEN_HAS_ICON` is defined, which
the build sets when it can extract the icon from your own game:

```bat
tools\msvc_env.bat cmake --preset x86-release -DMUGEN_GAME_DIR=C:/path/to/mugen-1.1b1
```

It needs `MUGEN_GAME_DIR\mugen.exe` to exist and Python 3 with `pefile`;
otherwise configure prints `mugen.exe gets no icon` and builds without one.
Reconfigure when you change `MUGEN_GAME_DIR` — it is a cache variable.

## Why x86 only

The target is a reconstruction of a 32-bit PE (i386). Every struct in `src/`
has the original's field offsets and is checked at compile time with
`MUGEN_ASSERT_SIZE` / `MUGEN_ASSERT_OFFSET`; those layouts only hold with
4-byte pointers. `CMakeLists.txt` therefore refuses to configure otherwise:

```cmake
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build with MSVC for x86 (32-bit), ...")
endif()
```

Configure a 64-bit generator and it stops there rather than producing a
subtly wrong binary.

## Compiler flags and why

| Flag | Reason |
|---|---|
| `/arch:IA32` | keeps floating point on the x87 FPU, as the original (VS2008, no SSE) — the reconstruction's rounding depends on it |
| `/W3 /wd4100` | the reconstructed functions keep every original parameter, so unreferenced-parameter warnings are noise |
| `/utf-8` | sources contain non-ASCII in comments and strings |
| `_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_DEPRECATE` | the code uses the classic CRT names deliberately |
| `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (`/MT`) | static CRT, so no `MSVCR90.dll` / VC redistributable is needed |
| third-party targets: `/W0` | Lua/libpng/GLEW/minizip warnings are not ours to fix |

`/std:c11` comes from `CMAKE_C_STANDARD 11`.

## What the build does

1. **Import libraries.** `cmake/MugenSdk.cmake` generates `implib\<dll>.lib`
   for each of SDL, SDL_mixer, SDL_ttf, SDL_gfx, zlib1, libresample,
   pthreadVC2 from the `.def` export lists in `third_party/sdk/def/`, using
   `lib /def: ... /machine:x86`. The exports then match the DLLs the game
   ships, exactly as the original imported them.
2. **Static libraries** from source (`cmake/ThirdParty.cmake`): `lua51`,
   `png12`, `glew_static`, `minizip`.
3. **The engine**: one object per module in `src/`, plus `src/mugen.rc` and
   `src/mugen.manifest` as resources.
4. **Link** with `/SUBSYSTEM:WINDOWS`, so entry is `WinMain` from
   `src/sdlmain.c`.

139 build steps in total for a clean tree.

## Output and runtime dependencies

`mugen_unleashed.exe` is a 32-bit GUI executable that imports:

```
SDL.dll  SDL_mixer.dll  SDL_ttf.dll  SDL_gfx.dll  zlib1.dll
libresample.dll  pthreadVC2.dll
OPENGL32.dll  GLU32.dll  WINMM.dll  SHELL32.dll  USER32.dll  GDI32.dll
KERNEL32.dll
```

No `MSVCR*`/`VCRUNTIME*`: the CRT is static. The DLLs are not shipped here;
they come from the game's `Elecbyte.MUGEN.libs` folder, which the embedded
manifest names. `libmpg123-0.dll` is loaded with `LoadLibrary` only when the
`mpg123` BGM plugin is used.

To run: copy `mugen_unleashed.exe` into a M.U.G.E.N 1.1 Beta 1 directory. It
does not shadow the original `mugen.exe`.

## Troubleshooting

### `cl : Command line error D8003 : missing source filename`

`cl` was run without `vcvarsall`, i.e. CMake found no compiler. Always go
through `tools\msvc_env.bat`, or use a Developer Command Prompt.

### `error: Cannot open compiler generated file ...` / C1041 on `vc140.pdb`

Parallel builds racing on one PDB. `CMakeLists.txt` pins a single
`COMPILE_PDB_NAME mugen_unleashed` for the target so every `cl` shares one PDB
correctly (with `/FS`); before that fix CMake pointed `TARGET_COMPILE_PDB` at
the object *directory*, and builds failed. If you see it again, check that
property survived your edits.

### `LINK : fatal error LNK1168: cannot open mugen_unleashed.exe for writing`

The previous executable is still held open — it is running, or an interrupted
link left a handle. Close it (or wait a moment) and rebuild; it is transient
and not a build-system fault.

### `CMake Error ... "Build with MSVC for x86 (32-bit)"`

The generator is 64-bit, or the compiler is not MSVC. Use the presets (they
pin `CMAKE_C_COMPILER=cl` and VS generates x86), or pass
`-A Win32` with a Visual Studio generator.

### Configure says `mugen.exe gets no icon`

Expected without `-DMUGEN_GAME_DIR`. Harmless. If you did pass it, check that
the directory holds `mugen.exe` and that `python -c "import pefile"` works.

### Stale configuration after switching VS versions

Delete the preset directory and reconfigure:

```bat
rmdir /s /q build\x86-release
tools\msvc_env.bat cmake --preset x86-release
```

## Continuous integration

`.github/workflows/build.yml` does the same on `windows-2022`: configure with
`x86-release`, build, then package `mugen_unleashed.exe` and
`mugen_unleashed.pdb` into `mugen-unleashed-x86.zip`. Tags (`v*`) attach the
zip to a GitHub release.

## Relationship to the other build

| | MSVC (`CMakeLists.txt`) | [GNU Make](BUILD-MINGW.md) |
|---|---|---|
| compiler | `cl` (x86) | `i686-w64-mingw32-gcc` |
| output | `build\x86-release\mugen_unleashed.exe` | `build/mingw/mugen_unleashed.exe` |
| CRT | static `/MT` | static MinGW CRT (`msvcrt.dll`) |
| fidelity | the reference build — matches the original's code generation | equivalent, with explicit x87/excess-precision flags |
| verification | the build the `mugen.log` differential tests were run against | build-verified |

Neither output is byte-identical to the original `mugen.exe` (different
compiler and linker), which is why behaviour is validated through `mugen.log`
comparisons rather than binary diffing.
