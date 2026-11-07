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
| `Windows_NT`, `MINGW*`, `MSYS*`, `CYGWIN*` | `i686-w64-mingw32` | `win32` | `mugen-mingw-win32-sdl2.exe` |
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
| `libgl1-mesa-dev`, `libglu1-mesa-dev` | 23.2.1 / 9.0.2 | `<GL/gl.h>`, `gluOrtho2D` — **not needed for `linux-arm64`, which defaults to the GLES backend** |

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

The `Makefile` selects `linux-arm64` from the compiler's triple, and it builds,
runs and installs. Note the two things that are **not** the same as x64:

**No x87.** AArch64 has no x87, so `-mfpmath=387` is dropped and
`FLT_EVAL_METHOD` is 0. Float results drift from the reference by an ULP, so
`mugen.log` will not match the Windows build exactly and the numeric lines have
to be audited by hand. That is a property of the architecture, not of this port.

**OpenGL ES, not desktop GL.** `linux-arm64` defaults to `MUGEN_GLES=1`: the
handhelds this target exists for (Mali-G31 class under ArkOS) expose OpenGL ES
only, and their `libGL.so.1` is the GLX/glvnd stub, so the desktop loader
resolves nothing against the EGL context SDL creates. See `PROBLEMS.md` (P1) for
the measurement and `RENDERING_PLAN.md` §12 for the backend.

```sh
make MUGEN_TARGET=linux-arm64                 # MUGEN_GLES=1 (GLES 3.0 backend)
make MUGEN_TARGET=linux-arm64 MUGEN_GLES=0    # desktop GL, as on x64
```

With `MUGEN_GLES=1` the build:

- compiles `third_party/glad-es-3.0` instead of `glad-2.1` and defines
  `-DMUGEN_GLES`;
- **excludes** `src/glrender.c` and `src/gldetect.c`; `src/gles_render.c`
  provides the same `Gl_*`/`Gfx_Gl*` entry points against GLES;
- does not link `-lGL -lGLU` — EGL comes in through SDL2;
- `make install` copies `res/shaders/gles/*` into `install/data/gles/`, which is
  where `src/gles_render.c` loads its GLSL ES programs from.

Dependencies are the same minus the desktop GL ones: `libgl1-mesa-dev` and
`libglu1-mesa-dev` are not needed (`libGLU` is only used by the desktop
`OpenGLScreen` projection reset, which is compiled out). SDL2, SDL2_mixer,
SDL2_ttf, zlib and libpng are needed as before.

If the driver cannot provide an ES 3.0 context, or a shader fails to compile, the
engine falls back to the System renderer and says why in `mugen.log`; set
`MUGEN_STRICT_RENDERER=1` to make that fallback fatal instead.

### Cross-building arm64 from x86-64

An arm64 rootfs plus `qemu-aarch64` (via binfmt) builds without a chroot: point
`CC` at the rootfs compiler with `--sysroot`, and give pkg-config the sysroot too.
Two things need care — the rootfs's `lib*.so` symlinks are absolute and must be
re-pointed for the host-side linker, and `PKG_CONFIG_SYSROOT_DIR` is what makes
`-I`/`-L` from the `.pc` files point inside the rootfs. The exact commands are in
`PROBLEMS.md` ("Reproducing").

### Cross-building arm64 from Windows with the Arm GNU Toolchain

This is the build that needs no arm64 machine and no chroot: a Windows host, the
free Arm GNU Toolchain, and an arm64 Ubuntu root filesystem to take the
dependency libraries from. It lives in **`port/arm64/`**, like the Android and
Vita ports; its `README.md` has the flag reference.

```sh
port/arm64/build.sh --setup     # once: download the toolchain (~330 MB)
port/arm64/build.sh             # build
```

→ `build/linux-arm64-aarch64-none-linux-gnu-sdl2/mugen-linux-arm64-aarch64-none-linux-gnu`

To build and copy it to the device in one step:

```sh
DEVICE_PASS=ark port/arm64/build.sh --upload     # -> ark@192.168.7.2:~/mugen/
```

That is all. The rest of this section explains what those commands do and what
can go wrong, for when they do.

Verified with **Arm GNU Toolchain 9.2-2019.12** (gcc 9.2.1, glibc 2.30) on
Windows and an Ubuntu 20.04 arm64 root, and checked against an RG351MP running
ArkOS; see "the glibc floor" below.

#### The toolchain

`--setup` downloads Arm's official Mingw-w64 host build for the requested
`ARM64_TOOLCHAIN` and verifies it against the checksum Arm publishes. The two
sources differ in shape:

| | 9.2 (default) | 15.3 |
|---|---|---|
| host | `developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel/` | `gitlab.arm.com/.../packages/generic/gnu-toolchain/15.3.rel2/` |
| archive | `.tar.xz`, top-level `gcc-arm-<ver>-.../` | `.zip`, no top-level directory |
| checksum | **MD5** (in a file named `.sha256asc`) | SHA-256 |

The GitLab URL needs `%2F` in the project path: the same URL with a literal `/`
returns **404**, which is easy to mistake for the file having moved.

Either way it is installed into `TOOLCHAIN_DIR` (default
`C:/arm-gnu-toolchain`), which ends up as

```
C:\arm-gnu-toolchain\bin\aarch64-none-linux-gnu-gcc.exe
C:\arm-gnu-toolchain\aarch64-none-linux-gnu\libc\      <- its glibc sysroot
```

The script flattens the 9.2 archive's version directory, so both versions leave
`bin/` at the top. A `.mugen-toolchain-<version>` marker records that the install
finished, so a re-run neither re-extracts nor re-downloads (the archive is cached
in `build/tmp/arm64-downloads/`). Installing a *different* version replaces the
tree rather than overlaying it — mixing two glibc sysroots is not something to
discover at link time.

It is a **Mingw-w64 host binary**, not a Cygwin or MSYS one: no DLLs to install,
and no `--sysroot` for its own libc — it was configured `--with-sysroot` and
finds `aarch64-none-linux-gnu/libc` relative to itself. It also accepts MSYS-style
absolute source paths (`/c/Projects/...`), so the Makefile's own paths work
unmodified. This toolchain is not `w64devkit` or `x86devkit`: it targets
Linux/ARM and builds this project's `linux-arm64` target only.

```sh
port/arm64/build.sh --check-tools   # is everything in place?
```

#### Uploading to the device

`--upload` copies the finished binary to `DEVICE` (default
`ark@192.168.7.2`) into `DEVICE_DIR` (default `mugen`, i.e. `~/mugen`),
overwriting `mugen-linux-arm64` there and keeping the old one as
`mugen-linux-arm64.prev`. Only the binary is sent; game data is not touched.

Two things about this device are worth knowing, because the obvious approach
does not work:

- **`scp` and `sftp` are broken on it.** Its sshd points the `sftp` subsystem at
  `/usr/local/crosware/.../dropbear/current/libexec/sftp-server`, a leftover
  from a removed package, so both fail with
  `bash: /usr/local/crosware/...: No such file or directory` while plain `ssh`
  is fine. `build.sh` therefore streams `tar` over the ssh exec channel.
- **Its clock is behind this machine's**, so a tar made here makes GNU tar warn
  `time stamp ... is ... s in the future` for every member. Harmless, and
  silenced with `--warning=no-timestamp`.

The transfer is unpacked as `.mugen-linux-arm64.new` and only renamed into place
after its md5 matches the local file, so a dropped connection cannot leave a
truncated binary where a working one was.

#### The dependency tree

The toolchain is a bare `--target=aarch64-none-linux-gnu` cross toolchain: it
ships a complete glibc and **no SDL**. `port/arm64/tools/make_sysroot.sh` copies
the missing libraries out of an existing arm64 Ubuntu root into
`build/tmp/arm64-sysroot/`. `build.sh` runs it when the tree is absent;
`--sysroot` forces it.

Any aarch64 Ubuntu/Debian root filesystem (`usr/lib/aarch64-linux-gnu/`,
`usr/include/{SDL2,...}`) works: a WSL distro's root, a Docker
`--platform linux/arm64` export, or a handheld's `/`. It is only read from. Set
`ARM64_ROOT_FS` when it lives somewhere unusual; otherwise the script probes the
default and prints what it picked:

```
root: //wsl.localhost/Ubuntu-22.04/home/deck/ubuntu_root_arm64
      (found through the WSL share; pass --root to use another)
```

Running from **MSYS** is the more fragile case, and the script compensates: the
root's headers include links such as `usr/include/png.h -> libpng16/png.h` and
`…/SDL2/SDL_platform.h`, which the Windows side cannot read through the share at
all (`Input/output error`, even for `[ -e ]`). Those are resolved inside the
already-copied tree instead, and the SONAME names are **copied** rather than
symlinked, because a link the Windows-side `ld` cannot follow reads as
`cannot find -l:libSDL2-2.0.so.0`.

`port/arm64/tools/make_sysroot.sh` copies from the root, it does not build: SDL2,
SDL2_mixer, SDL2_ttf, zlib, libpng and FreeType as **shared** objects, plus the
headers and hand-written `.pc` files the Makefile asks for. It is worth reading
before using it, because three of its choices are not obvious:

| What | Why |
|---|---|
| Written `.pc` files, not the root's | The distribution's `sdl2.pc` lists X11, Wayland, ALSA and Pulse as its dependency set. This engine only calls into SDL itself. |
| `-l:libSDL2-2.0.so.0` rather than `-lSDL2` | The tree carries the static archives too, and `ld` prefers a `.a` over a `.so` when both are present — the static links then fail on the Wayland/FLAC symbols nothing provides. |
| `usr/lib/rpath/` | ld resolves the *transitive* `NEEDED` libraries of `libSDL2-2.0.so.0` (libasound, libpulse, libX11, libFLAC, ...) while linking. They are `-rpath-link` only: nothing is linked in and nothing ships. |

Everything the script copies stays inside the repository. The toolchain directory
is only ever read — the sysroot it brings travels with it.

#### What the Makefile does differently for a cross build

| Variable | Effect |
|---|---|
| `CROSS_COMPILE` | Prefixes `CC`/`AR`/`STRIP`, so the host `gcc` cannot leak in. |
| `XSYSROOT` | The dependency tree. pkg-config's search path is *replaced* with it (`PKG_CONFIG_LIBDIR`) — the host's `.pc` files have the same names. |
| `PKG_CONFIG_SYSTEM_*` | Also replaced: pkgconf qualifies system `-I`/`-L` against the host (`/mingw64/include`), so a target `.pc` otherwise resolves to the host's paths. |
| `-nostdinc` | The target's headers are the only headers; `-idirafter` puts the toolchain's own back. |
| `--sysroot` | Only when the compiler was not configured with one. Arm's `aarch64-none-linux-gnu` carries its glibc sysroot; a bare cross gcc on Linux does not. |
| `--static` | Asked of pkg-config, so SDL2's own `-lm -ldl -lpthread` are carried along. The libraries in the tree are still shared objects. |

#### The glibc floor

A binary cannot require a newer glibc than the target has. The failure is
immediate and unambiguous:

```
./mugen: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.38' not found
```

Which glibc a build needs is decided by the **toolchain**, not by this project,
so `port/arm64/build.sh` pins one per target. The rule, measured rather than
assumed:

| Toolchain | glibc it targets | Binary wants | Runs on |
|---|---|---|---|
| `ARM64_TOOLCHAIN=9.2` (gcc 9.2.1, 2019.12) | 2.30 | up to `GLIBC_2.29` | ArkOS/RG351MP — **verified on hardware** |
| `ARM64_TOOLCHAIN=15.3` (gcc 15.3.1) | 2.42 | up to `GLIBC_2.38` | a 2024+ distribution only |

So: **ask the device first**, then pick.

```sh
ssh ark@<device> 'ldd --version | head -1'      # e.g. "GLIBC 2.30-0ubuntu2.2"
port/arm64/build.sh                             # 9.2 -> needs 2.29, fine
```

On an RG351MP (ArkOS, Ubuntu 19.10 userspace, kernel 4.4.189, Mali-G31) the 9.2
build initialises to `renderer: requested=opengl effective=opengl` on `GLES 3.2`
and plays a full match with no errors, while the 15.3 build is rejected by the
loader with the message above. That is the whole reason 9.2 is the default.

Check any binary before copying it across:

```sh
readelf -V <binary> | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1
```

If the target is older than 2.30 there is no Arm release old enough — build
natively on it, or in an arm64 chroot, with its own gcc.

Older releases are **not** on GitLab (whose package registry starts at 11.2).
They live on `developer.arm.com/downloads`, under
`/-/media/Files/downloads/gnu-a/<version>/binrel/`, which redirects to
`armkeil.blob.core.windows.net`. Two traps there: the archives are `.tar.xz`
with a top-level `gcc-arm-<version>-...` directory (so they need flattening,
unlike the newer `.zip`), and they publish **MD5** — a 32-hex value in a file
still named `.sha256asc`.

#### Verifying a cross build without an arm64 machine

```sh
# WSL, with qemu-user installed
cd /mnt/c/Projects/<repo>/install
LD_LIBRARY_PATH=$PWD/../build/tmp/runroot \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  qemu-aarch64 -L $PWD/../build/tmp/runroot ./mugen-linux-arm64-... -nosound -p1 kfm -p2 kfm
```

Set `RenderMode = System` in `install/data/mugen.cfg` first: the dummy video
driver has no OpenGL, so the default `OpenGL` render stops at
`SDL_CreateWindow failed: OpenGL support is either not configured...`. The System
renderer needs nothing from the graphics stack and is enough to prove that the
ARM code paths, the file formats, Lua, the audio resampler and libpng all work.
Restore the config and remove the copied binary afterwards — `install/` is the
pristine game tree.

### "Just put SDL2 in the toolchain's sysroot"

The obvious alternative to `XSYSROOT` is to copy the libraries into the
toolchain's own glibc sysroot
(`C:/arm-gnu-toolchain/aarch64-none-linux-gnu/libc/usr/`) and build with no
extra flags. **This works for compiling and does not work for linking.**
Measured on Arm GNU Toolchain 15.3.Rel2 with SDL2 2.0.10 from Ubuntu 20.04 arm64:

| Step | Result |
|---|---|
| headers → `<sysroot>/usr/include/SDL2/` | `#include <SDL.h>` still fails: the sysroot only puts `include/` on the path, so `-I<sysroot>/usr/include/SDL2` is needed anyway |
| `libSDL2-2.0.so.0` → `<sysroot>/usr/lib64/` | compiles and links a `-lSDL2`-only program |
| the same link for this engine | **fails**: hundreds of undefined `X*`, `pa_*`, `snd_*`, `wl_*`, `xkb_*` references |

Why it fails: the distribution's `libSDL2-2.0.so.0` was built against X11, ALSA,
PulseAudio and Wayland, and those are its `DT_NEEDED` entries. Linking a shared
library against them is normally fine — but only if they are reachable through
`-rpath-link`. When they are, their own dependencies are not, and the linker
wants the whole closure. In the `XSYSROOT` tree that closure lives in
`usr/lib/rpath/` and is passed explicitly; a toolchain sysroot merge would have
to reproduce the same thing, at which point the merge buys nothing over
`XSYSROOT` and has moved files into the toolchain directory that this repository
does not own.

The options that do work, in order of preference:

1. **`XSYSROOT` (this page).** No writes outside the repository, and the tree is
   reproducible from `port/arm64/tools/make_sysroot.sh`.
2. **Build a static SDL stack into the toolchain.** Configure SDL2 with
   `-DSDL_SHARED=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_ALSA=OFF
   -DSDL_PULSEAUDIO=OFF -DSDL_OPENGLES=ON`, install into
   `<sysroot>/usr/`, then link statically. That is the build the *Windows*
   targets use, and it closes the dependency question entirely — but it means
   cross-building SDL2, SDL2_mixer (with ogg/vorbis/FLAC/mpg123/opus) and
   SDL2_ttf (with FreeType) from source, and it links SDL2 into the binary.
   This is what to do if the whole thing has to be one self-contained file.
3. **A compiler from a root whose glibc matches the target** (see below), with
   the libraries next to it — this is just option 1 with a different compiler.

### Cross-build troubleshooting

Every entry below was hit while bringing this up; they are in the order they
appear in a first attempt.

| Symptom | Cause and fix |
|---|---|
| `run this from Windows … not from inside WSL` | `port/arm64/build.sh` drives the **Mingw-w64** toolchain, which WSL cannot execute. Inside WSL build natively: `make MUGEN_TARGET=linux-arm64`. |
| `download failed its vendor SHA-256` | The zip was truncated or altered in transit; nothing was extracted. Re-run `--setup` (the bad file is deleted first). |
| `the archive did not produce …/gcc` | A half-extracted toolchain. Remove `TOOLCHAIN_DIR` and re-run `--setup`. |
| `fatal error: math.h: No such file or directory` | `-nostdinc` is on, and the toolchain's own include directories were not detected. The Makefile asks `$(CC) -E -v` for them and re-adds each with `-idirafter`; if you build by hand, do the same. Re-run `make check-toolchain` — it prints the resolved tree and sysroot flags. |
| `fatal error: SDL2/_real_SDL_config.h` | Debian/Ubuntu splits SDL2's configuration: `include/SDL2/SDL_config.h` is an indirection to `include/<triple>/SDL2/_real_SDL_config.h`. `make_sysroot.sh` merges the two; a manual copy must too. |
| `… SDL2/SDL_platform.h: Invalid argument` | The distribution ships some SDL2 headers as **symlinks into themselves**. Copy them with `cp -L` (dereference); `cp -a` produces links that dangle. |
| `cp: cannot stat …/SDL_platform.h: Input/output error` | The root's headers contain links (`png.h -> libpng16/png.h`, `…/SDL2/SDL_platform.h`) that the Windows side cannot read through `//wsl.localhost` at all — `[ -e ]` and `[ -L ]` both fail on them, so a test before the copy skips the header silently. `make_sysroot.sh` copies optimistically and falls back to the already-copied target; a hand-written copy must do the same, or run the script inside WSL. |
| `fatal error: png.h: No such file or directory` | As above, one step further on: `png.h`/`pngconf.h`/`pnglibconf.h` are links into `libpng16/`. They must be resolved inside the copy, not read from the share. |
| `cannot find -l:libSDL2-2.0.so.0` | The SONAME link is missing or is a WSL symlink on a DrvFs mount, which the Windows-side `ld` cannot resolve. `make_sysroot.sh` copies instead of linking for exactly this reason. |
| Undefined `X*` / `pa_*` / `snd_*` / `wl_*` | `ld` reached the **static** `libSDL2.a` (it prefers `.a` over `.so` when both are in one directory), or `-rpath-link` to `usr/lib/rpath/` is missing. |
| `undefined reference to __libc_dlopen_mode@GLIBC_PRIVATE` | The arm64 root's `libc.so.6`/`libpthread.so.0`/`libdl.so.2` got into the link line and shadow the toolchain's own. They must never be copied into `usr/lib/rpath/`; `make_sysroot.sh` filters them by SONAME. A tree built by an older version of the script can still hold them — `--sysroot` rebuilds it. |
| `CROSS_COMPILE=… needs XSYSROOT=<dir>` | Building by hand without a dependency tree. The Makefile refuses on purpose: without one, pkg-config answers from the **host**, the compile picks up host headers and the link mixes architectures. Run `port/arm64/build.sh`, or point `XSYSROOT` at `build/tmp/arm64-sysroot`. |
| `version GLIBC_2.38 not found` on the target | The glibc floor — the toolchain is newer than the device. `ARM64_TOOLCHAIN=9.2` for glibc-2.30 devices; see the table above. |

To see what a cross build actually resolved, rather than guess:

```sh
make check-toolchain CROSS_COMPILE=aarch64-none-linux-gnu- XSYSROOT=build/tmp/arm64-sysroot MUGEN_TARGET=linux-arm64
make -n CROSS_COMPILE=… XSYSROOT=build/tmp/arm64-sysroot MUGEN_TARGET=linux-arm64 | grep -o '\-l:[^ ]*'
```
