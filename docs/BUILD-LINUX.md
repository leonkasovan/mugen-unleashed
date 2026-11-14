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
| `-log` / stdout redirection | `platform_main_win32.c` (SDL 1.2's WinMain wrapper) is Windows-only; `main.c` is the entry on Linux |
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

**3D stage models are on by default when built through `port/arm64/build.sh`.**
The Makefile's own default is conservative for a GLES build --
`MUGEN_MODEL3D ?= $(if $(filter 1,$(MUGEN_GLES)),0,1)` -- so a bare
`make MUGEN_TARGET=linux-arm64` compiles them out. A stage whose background is
only `[BGdef] model = *.glb` then has no elements to draw and renders black, with
`Need at least one [BG] type bg (3D backgrounds unavailable)` in the match log.
`port/arm64/build.sh` therefore always passes the value explicitly and defaults
it on, like `port/psvita/build.sh`: `--no-model3d` turns them off. The switch is
a make command-line variable rather than an environment one because a make
variable cannot be handed over reliably here -- a native `make.exe` under MSYS2
may see only a whitelisted environment, so `MUGEN_MODEL3D=1 port/arm64/build.sh`
silently built the default.

A model stage still has to fit the device: the decoded RGBA of its textures is
capped by `MODEL_CPU_BUDGET` (256 MiB) and each texture must be no larger than
`GL_MAX_TEXTURE_SIZE` (8192 on Mali-G31). `Stage0_3D_GREEN` is 273 MiB decoded
(12800x4288 + 12800x1312) and fails both; the `KOFXI_*` stages are 256x256
textures and load in under 2 s.

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

#### Debug builds and the batch-rendering profiler

`--debug` builds `-O0 -g3` with `-DMUGEN_DEBUG` into a **separate tree**,
`build/linux-arm64-aarch64-none-linux-gnu-sdl2-debug/`:

```sh
port/arm64/build.sh --debug              # unoptimised, with the profiler
port/arm64/build.sh --debug --upload     # ... and onto the device
```

A separate tree is not tidiness. The Makefile's configuration stamp records the
target, the renderer, the model switch and the sysroot, but **not** `DEBUG=`, so
a debug build sharing a release tree would leave the two optimisation levels'
objects side by side under one stamp — neither would rebuild when you switched.

`MUGEN_DEBUG` additionally compiles in the batch-rendering profiler
(`src/batchprof.c`), which prints one line a second next to the existing `perf:`
line:

```
perf: renderer=opengl fps=13.9 frames=38 logic=0.9ms draw=9.3ms wait=0.0ms present=16.2ms calls=42 tris=2144
batch: calls=42 quads=38 quads/call=1.11 verts=152 stream=4.8KiB prog=2.0 tex=38.0 cpu=310us
```

`quads/call` is the batching ratio. Both backends submit one sprite quad per GL
draw call — and on GLES re-stream the whole four-vertex buffer for each — so it
sits at 1.00 and `tex` tracks `quads` one for one. A frame bound by per-quad
driver overhead shows high `calls` with a ratio near 1 and a `draw` that follows
`cpu`; batching would raise the ratio and collapse `stream`. The profiler is
compiled out entirely in a release build (no symbols, no format string, no
counter reads), and `tools/test_batchprof.sh` asserts its arithmetic on the host.

A second debug line splits the submission, because the first measurement made it
clear that guessing which GL call is expensive gets it wrong:

```
batch:   split up=1460us up_sp=1023us st=263us buf=996us dr=3420us
batch:   split px=68us pal=224us
```

`up` is the texture uploading before the draw (`up_sp` the sprite's share), `st`
program/uniform/attribute setup, `buf` the vertex upload, `dr` the draw call.
`px`/`pal` are `Sprite_GetPixels` and `SpriteSet_GetPaletteConverted`.

#### What that found (RG351MP, `-p1.ai 6 -p2.ai 6`)

`kfm.def`, level stage, ~30 quads/frame, release-build frame rate:

| | before | after |
|---|---|---|
| frame rate | 21.2 / 20.0 fps | **44.8 / 43.8 fps** |
| palette upload | ~3.8 ms/frame | ~0.2 ms |
| program/uniform/attribute setup | ~0.9 ms | ~0.25 ms |
| sprite upload | ~0.7 ms | ~1.0 ms |

The two costs were re-uploading a 256x1 palette on every paletted draw, and
re-sending a program bind and seven uniforms that had not changed. The fixes are
a content-hashed cache of palette textures (one texture per distinct palette,
dropped on context loss) and a redundant-state cache for the program and
uniforms in `GlesDraw_Quad`.

Nothing on screen changed: compared against the pre-change build with a fixed
`-sessionseed`, both the title screen (frames 10/20/40/80) and matches at
`round_ticks` 60/180/400 produce **byte-identical** PNGs.

#### Where the 2D frame goes now

Those fixes took a 2D stage from 21 to ~52 fps. What is left, measured on the
RG351MP with the batch profiler in a `-O2` build (`make ... BUILD_DEFS=-DMUGEN_DEBUG`
gives batchprof without `DEBUG=1`'s `-O0`), `kfm.def` with AI on both sides:

| component | ms/frame | note |
|---|---|---|
| `logic` | 0.7 | |
| `draw` | 7.4 | |
| `present` | 8.6 | **vblank wait, not work** |
| — of `draw`: `dr` | 2.75 | the `glDrawArrays` call itself |
| — `buf` | 0.87 | per-quad vertex upload |
| — `up` | 0.68 | texture uploads |
| — `st` | 0.19 | program/uniform/attribute setup |
| — `px` / `pal` | 0.10 / 0.12 | sprite decode / palette conversion |
| — **unaccounted** | **2.69** | quad building, CPU model-view, bg/HUD drawing |

`present` is the slack in a vsync-locked loop, not optimisable work: the driver
forces vsync (the engine requests 0, SDL reports 1), `logic + draw + present`
stays at one 60 Hz period across scenes whose `draw` differs by 5 ms, and 742 of
5324 frames are skipped when work overruns. Making it smaller would only mean
more idle waiting.

The 2.69 ms residue is where the engine itself could be blamed, so it was
profiled rather than guessed at (see "Sampling profiler" below): **78% of all
CPU samples land in shared libraries** — the Mali driver and libc — against
`mat_mul` 3.5%, `gles_palette_hash` 2.7%, `SysDraw_Find` 1.1%. The draw span is
dominated by CPU time inside the driver, which is also why batching did not pay
(see below). There is nothing left worth optimising in the sprite submission
path short of drawing fewer quads.

#### Sampling profiler (`MUGEN_PROF=1`)

`src/profiler.c` is a 100 Hz `ITIMER_PROF`/`SIGPROF` sampler modelled on Go's
CPU profile: it samples rather than instruments, attributes by stack, and does
the expensive work off the target. The handler only appends program counters to
a fixed table (no allocation, no locks); `tools/prof_symbolize.py` symbolizes
afterwards on the workstation with the cross `addr2line`:

```sh
# on the device
MUGEN_PROF=1 MUGEN_PROF_OUT=mugen.prof ./mugen-linux-arm64 -nosound ...
# on the host
python3 tools/prof_symbolize.py mugen.prof \
    --binary build/linux-arm64-aarch64-none-linux-gnu-sdl2/mugen-linux-arm64-aarch64-none-linux-gnu
```

It reports SELF (leaf frame: where the CPU was) and INCLUSIVE (anywhere on the
stack: who caused it). `ITIMER_PROF` counts CPU time, so a thread parked in a
swap contributes nothing — which is what you want when the question is CPU work.

Limits to know before trusting a number:

- the dump records raw PCs, so anything in a **shared library** symbolizes as
  `??`. On the reference device that was 78% of samples; naming them needs the
  process's `/proc/self/maps` captured alongside the dump, which is not done yet.
- the handler unwinds with `backtrace()`, which needs `.eh_frame` — present in
  the `-O2 -g` build — and glibc's first call may allocate, so the profiler warms
  the unwinder up before arming the timer.

#### GPU timing: not available on this driver

`src/gputime.c` (opt-in, `MUGEN_GPUTIME=1`) wraps the draw span and the present
blit in `EXT_disjoint_timer_query`. The device advertises the extension and the
queries are created, but the elapsed values come back as `0xFFFFFFFF…`-derived
sentinels that never become measurements — with no GL error from any call — so
GPU time cannot be measured this way here. The read path is hardened anyway (GL
errors drained and reported, implausible results rejected rather than averaged, a
not-ready result retried instead of discarded), and the probe is off by default
so it costs nothing.

#### Driver capabilities (reference device)

`Gles_LogCapabilities` (debug builds) prints these at start-up. RG351MP:

```
vendor=ARM renderer=Mali-G31
version=OpenGL ES 3.2 v1.r13p0 ... sl=OpenGL ES GLSL ES 3.20
MAX_TEXTURE_SIZE 8192   MAX_TEXTURE_IMAGE_UNITS 16   MAX_VERTEX_ATTRIBS 16
MAX_SAMPLES 16          MAX_UNIFORM_BLOCK_SIZE 16384
present: EXT_disjoint_timer_query, KHR_debug, EXT_texture_format_BGRA8888,
         EXT_texture_storage, EXT_buffer_storage,
         EXT_multisampled_render_to_texture, KHR_blend_equation_advanced,
         ARM_shader_framebuffer_fetch, EXT_sRGB, OES_EGL_image,
         KHR_texture_compression_astc_ldr/hdr, EXT_discard_framebuffer
absent:  S3TC/DXT, KHR_partial_update, EXT_map_buffer_range,
         anisotropic filtering
```

The driver lists **93** extensions in total; `docs/GLES_EXTENSIONS.md` is the full
catalogue with a line on each. An earlier version of this section said "no texture
compression", which was wrong: the probe looked for
`EXT_texture_compression_astc_ldr` while Mali advertises the `KHR_` spelling, so
ASTC went unnoticed. The curated list in `src/gles_probe.c` now checks both.

The 16 texture units confirm the 7 sprite + 7 palette budget Ikemen-GO's
instanced path assumes.

#### Two approaches measured and rejected

Recorded with their numbers so they are not retried on the same premise.

**Sprite atlases** — packing a `SpriteSet`'s paletted sprites into one texture cut
per-frame texture switches 36.6 -> 19.3 and took that scene from 31.5 to 36.3
fps. Rejected: the engine renders 640x480 into a 640x360 screen, so every draw is
scaled, and under `NEAREST` an atlas quad picks its texel against the atlas
resolution plus an offset instead of against the sprite's own width. 6.3% of
pixels then differ (visually identical, not byte-identical) inside the scaled
sprite, which this project does not accept.

**Instanced sprite batching** — Ikemen's Phase 5 port: one
`glDrawArraysInstanced` per run, corners/texcoords/MVP in an instance buffer, up
to 7 units per batch. It works and is pixel-identical, and it cut draw calls
39 -> 17 (`quads/call` 1.00 -> 2.4-2.6). Rejected: over five alternating pairs the
release frame rate was 52.7 vs 51.9 fps — inside the run-to-run spread. The
planner can only group ~2.4 quads per batch (every sprite is its own texture),
and the driver's CPU cost tracks total attribute/uniform work, so instancing
moved it rather than removed it.

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
in `build/downloads/`). Installing a *different* version replaces the
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
ships a complete glibc and **no SDL**. The missing libraries arrive as a release
asset, `arm64-sysroot.zip`, which `build.sh` downloads into
`build/sysroots/arm64-sysroot/` when the tree is absent; `--sysroot` re-downloads
it.
The archive is cached in `build/downloads/` and verified against a
SHA-256 pinned in `build.sh` on every use, so a truncated or replaced download
cannot become the tree that gets linked:

```
arm64-sysroot.zip   15.8 MB
sha256 16bbbf25b83b770f92ab478e330ea9eca0351c1f5792037ba958f02bece65049
                    39 MB unpacked, 273 files
```

`ARM64_SYSROOT_URL` and `ARM64_SYSROOT_SHA256` override both, for a fork or a
re-published asset. The tree is data, not a build step: it was assembled once
from an aarch64 Ubuntu 22.04 root (SDL2 2.0.10, SDL2_mixer 2.0.4, SDL2_ttf
0.14.1, zlib 1.2.11, libpng16, FreeType 2.10.1 — all shared objects) together
with the headers and hand-written `.pc` files the Makefile asks for, and it is
the same tree for every host that cross-builds this engine. Nothing in this
repository regenerates it; producing it needs an aarch64 root filesystem that
most checkouts do not have.

What is in it, and why — every row was a link failure at some point:

| What | Why |
|---|---|
| Written `.pc` files, not the distribution's | The distribution's `sdl2.pc` lists X11, Wayland, ALSA and Pulse as its dependency set. This engine only calls into SDL itself. |
| `-l:libSDL2-2.0.so.0` rather than `-lSDL2` | The SONAME is named exactly, so the shared object is the one used and there is no archive for `ld` to prefer. |
| `usr/lib/rpath/` | `ld` resolves `NEEDED` entries *only* through `-rpath-link`, never through `-L`. So the closure of the six libraries the engine links (libasound, libpulse, libX11, libFLAC, ...) lives here, the six included. `-rpath-link` only: nothing is linked in and nothing ships. |
| The closure is *computed*, not swept | Walking `NEEDED` from the six libraries gives 59 objects / ~16 MB. The rest of the arm64 userspace — LLVM, ICU, the sanitizers, GTK — is unreachable from SDL2 and can never be asked for; carrying it made the tree 479 MB and produced a byte-identical binary. |
| No `*.a` in the tree | Every `.pc` names its shared object (`-l:libSDL2-2.0.so.0`), so an archive is only reachable through `-lSDL2`, which nothing here asks for. |
| No glibc, no `ld-linux*.so.1` | The arm64 root's `libc.so.6`/`libpthread.so.0`/`libdl.so.2` are `GLIBC_PRIVATE`-incompatible with the toolchain's own and must never enter the link line. The loader is the toolchain's business, not the tree's. |

Everything the tree holds stays inside the repository, under the git-ignored
`build/`. The toolchain directory is only ever read — the sysroot it brings
travels with it.

No absolute path is allowed to survive in it: `build.sh` rewrites every `.pc`
`prefix=` to `${pcfiledir}/../..` on install, so the tree links correctly from
wherever it is unpacked. That rewrite is also why the shipped asset does not
have to be regenerated to move the checkout.

#### Updating the tree

The generator is not in the tree any more (producing the tree needs an aarch64
Ubuntu root filesystem, so it is done once and published rather than re-done per
checkout), but it is in the history — restore it, run it, repackage:

```sh
# 1. the generator, from the commit that removed it
git show 86f64fd~1:port/arm64/tools/make_sysroot.sh > build/tmp/make_sysroot.sh

# 2. rebuild the tree from an arm64 Ubuntu/Debian root. With no --root it probes
#    /home/deck/ubuntu_root_arm64 and, from Windows, the same path under
#    //wsl.localhost/<distro>/; ARM64_ROOT_FS is no longer read, pass --root.
bash build/tmp/make_sysroot.sh --dest build/sysroots/arm64-sysroot \
     --root //wsl.localhost/Ubuntu-22.04/home/deck/ubuntu_root_arm64

# 3. make the .pc files relocatable (the generator bakes an absolute prefix)
sed -i 's|^prefix=.*|prefix=${pcfiledir}/../..|' \
     build/sysroots/arm64-sysroot/usr/lib/pkgconfig/*.pc

# 4. record what is in it — the archives are opaque, this is the only place the
#    versions and the exclusions are written down (copy the wording from
#    tools/make_win_sysroot.sh, which writes the same file for the Windows trees)
$EDITOR build/sysroots/arm64-sysroot/SYSROOT.txt

# 5. package it with the same single top directory. The zip is a release
#    artefact, so it goes to build/tmp (scratch), not next to the tree.
cd build/sysroots && zip -qr ../../tmp/arm64-sysroot.zip arm64-sysroot && cd -
sha256sum build/tmp/arm64-sysroot.zip
```

Then upload the zip to the release and put its hash in `SYSROOT_SHA256` in
`port/arm64/build.sh` — the build refuses any other content on purpose. A fork
that publishes its own copy does not have to edit the script: it can set
`ARM64_SYSROOT_URL` and `ARM64_SYSROOT_SHA256` for one run.

Check the result from a path that is not this checkout, because that is the one
thing a same-path test cannot prove:

```sh
cp -a build/sysroots/arm64-sysroot /some/other/dir/arm64-sysroot
PKG_CONFIG_LIBDIR=/some/other/dir/arm64-sysroot/usr/lib/pkgconfig \
  pkg-config --cflags sdl2     # must name /some/other/dir, not this checkout
```

#### What the Makefile does differently for a cross build

| Variable | Effect |
|---|---|
| `CROSS_COMPILE` | Prefixes `CC`/`AR`/`STRIP`, so the host `gcc` cannot leak in. |
| `XSYSROOT` | The dependency tree. pkg-config's search path is *replaced* with it (`PKG_CONFIG_LIBDIR`, and `PKG_CONFIG_PATH` is cleared — pkgconf searches the two together, and MSYS2 exports one by default). |
| `PKG_CONFIG_SYSTEM_*` | Also replaced: pkgconf qualifies system `-I`/`-L` against the host (`/mingw64/include`), so a target `.pc` otherwise resolves to the host's paths. |
| the `-I` guard | Every include path pkg-config returns must lie inside `XSYSROOT`; otherwise the build stops with one line instead of compiling against host headers. cpp searches *any* `-I` before *any* `-isystem`, so a leaked `.pc` outranks the tree. |
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
   a pinned, checksum-verified release asset rather than something each machine
   has to assemble.
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
| `arm64-sysroot.zip failed its SHA-256` | The download was truncated or the asset was replaced; nothing was extracted and the existing tree was kept. Re-run `--sysroot`. If you re-published the asset yourself, pass the new hash: `ARM64_SYSROOT_SHA256=<hex> port/arm64/build.sh --sysroot`. |
| `unfinished sysroot transaction at …` | An earlier run died between the renames and left `build/sysroots/arm64-sysroot.mugen-new` or `.mugen-old` behind. The tree in place was kept on purpose; delete the leftover and re-run. |
| `… did not produce usr/lib/pkgconfig/sdl2.pc` | The archive unpacked but it is not the dependency tree (wrong asset, or a zip of a zip). Nothing was installed and the existing tree was kept. |
| `cannot find -l:libSDL2-2.0.so.0` | The tree's SONAME-named copies are missing. They are real copies and not symlinks, because a link the Windows-side `ld` cannot follow reads as exactly this error. |
| Undefined `X*` / `pa_*` / `snd_*` / `wl_*` | `ld` reached a **static** `libSDL2.a` (it prefers `.a` over `.so` when both are in one directory), or `-rpath-link` to `usr/lib/rpath/` is missing. |
| `libSDL2_ttf-2.0.so.0: undefined reference to \`FT_Done_Face'` (and its siblings) | The `usr/lib/rpath/` copy of `libfreetype.so.6` is missing. `ld` finds a `NEEDED` entry through `-rpath-link` and **not** through `-L`, so being in `usr/lib/` is not enough for a library SDL2_ttf imports. Only a hand-made tree gets this wrong; the published one carries the six roots in `rpath/` for exactly this reason. |
| `undefined reference to __libc_dlopen_mode@GLIBC_PRIVATE` | The arm64 root's `libc.so.6`/`libpthread.so.0`/`libdl.so.2` got into the link line and shadow the toolchain's own. The published tree ships no glibc at all; a hand-made one must filter it by SONAME. |
| `CROSS_COMPILE=… needs XSYSROOT=<dir>` | Building by hand without a dependency tree. The Makefile refuses on purpose: without one, pkg-config answers from the **host**, the compile picks up host headers and the link mixes architectures. Run `port/arm64/build.sh`, or point `XSYSROOT` at `build/sysroots/arm64-sysroot`. |
| Thousands of errors inside `arm_neon.h` (`expected declaration specifiers before '__extension__'`), mentioning headers under `C:/msys64/mingw64/include/SDL2` | A **host** `.pc` was picked up, so the compile used MSYS2's SDL2 (2.3x) instead of the tree's. MSYS2 exports `PKG_CONFIG_PATH` by default and pkgconf searches it *in addition to* `PKG_CONFIG_LIBDIR`, so clearing only the libdir is not enough; the Makefile now clears both and refuses to build if any `-I` lies outside `XSYSROOT`. Nothing is wrong with the toolchain — read the *first* error, not the avalanche. |
| `version GLIBC_2.38 not found` on the target | The glibc floor — the toolchain is newer than the device. `ARM64_TOOLCHAIN=9.2` for glibc-2.30 devices; see the table above. |

To see what a cross build actually resolved, rather than guess:

```sh
make check-toolchain CROSS_COMPILE=aarch64-none-linux-gnu- XSYSROOT=build/sysroots/arm64-sysroot MUGEN_TARGET=linux-arm64
make -n CROSS_COMPILE=… XSYSROOT=build/sysroots/arm64-sysroot MUGEN_TARGET=linux-arm64 | grep -o '\-l:[^ ]*'
```
