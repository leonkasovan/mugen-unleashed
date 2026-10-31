# PLANS

Porting mugen-unleashed from Windows/MSVC to Linux/GCC, and optimising the
renderer. This file records the research (what the code actually depends on),
the decisions, and the ordered work plans. Update the status column as work
lands; keep the research sections true to the findings they were based on.

## Status at a glance

| Area | State |
|---|---|
| Windows build (MSVC/CMake) | **done, verified** — outputs `mugen_unleashed.exe`; `docs/BUILD-MSVC.md` |
| Windows static build (GNU Make + MinGW-x86) | **done, verified** — outputs `mugen-static.exe`, no game DLLs needed; `docs/BUILD-MINGW.md` |
| libresample | **vendored and ported** to multi-channel; `third_party/libresample/VENDORED.md` |
| Lua | **done** — `lmode`/`lexec` implemented, full API documented in `docs/LUA.md` |
| Renderer optimization | steps 1–3 **done and runtime-verified**; 4–6 pending |
| OpenGL loader | **GLEW 1.5.4 → glad 2.1 done** — smaller, faster to build, exactly generated |
| Modern GL (3.3/4.6) / Vulkan | researched, **not recommended** — reasons below |
| Linux port | **linux-x64 done and verified** — builds and runs, `mugen.log` identical to the Windows build; `docs/BUILD-LINUX.md`. arm64 selected but untested |

## Codebase knowledge graph (codebase-memory-mcp)

Indexed as project `C-Projects-mugen-unleashed` (`mode: full`, status `ready`).

| | fast | full |
|---|---|---|
| Nodes | 5,777 | **21,452** |
| Edges | 148,328 | **186,904** |
| Scope | `src/` + root files | + `docs/`, `tools/`, `third_party/` |
| `parse_unusable` | 1 (`src/symbols.h` whole file) | **0** |

Notes, all verified by running the CLI:

- **`full` mode indexes far more than `src/`** — `third_party/` (Lua, libpng,
  GLEW, minizip, sdk headers) dominates the node count. Queries about the
  engine should filter on `src/` (e.g. `--file-pattern 'src/'`).
- **`src/symbols.h` parses cleanly in `full` mode** (only `parse_partial`
  ranges 7-75 and 152-1936 remain), closing the gap `fast` mode had. Use it as
  a source of truth for prototypes/globals again.
- **`parse_partial` = 43 files**, nearly all vendored third-party headers
  (`SDL_opengl.h`, `png.h`, `glew.h`, …). Engine-side ranges worth knowing:
  `src/cnsparse.c:127`, `src/font.c:17`, `src/sound.c:20`, `src/ini.c:896`,
  `src/luautil.c:83,91`, `Makefile:260`.
- **The `HOME` trap** (was blocking, now **resolved**): the server resolves its
  config and index DBs through `HOME`. This environment briefly exported
  `HOME=/c/Projects` (a projects dir, not the profile), which made *every* call
  fail with *"could not load saved runtime settings"* — the server exits while
  loading settings, before it even parses the tool name. `HOME` is now
  `/c/Users/dhani`, so **no workaround is needed**: the CLI and the in-session
  MCP tools both work bare. If it recurs (e.g. after re-running the
  `setup_*_terminal_for_agentic_ai.bat` scripts in `/c/Projects`), the fixes are
  `export HOME="$USERPROFILE"` or a per-process `env -u HOME <bin> cli …`
  (the server then falls back to `USERPROFILE`). No extension patch is in place
  or needed: `~/.pi/agent/extensions/cbmem.ts` is unmodified, kept identical to
  its original by `cbmem.ts.bak`.
- **Semantic (vector) search does not work in this build**: `semantic_query`
  returns meaningless hits (e.g. `Makefile.clean` for "sprite scale blend
  render target RGB565") with scores ~0.04–0.14, and setting
  `CBM_SEMANTIC_ENABLED=1` changes nothing (the index step never runs a
  semantic pass; reindex finishes in ~2 s). **The stored relations are good**
  though — `SEMANTICALLY_RELATED` (348) and `SIMILAR_TO` (85) hold sensible
  scores 0.75–0.94 (`BgmThread_Prebuffer` ↔ `BgmThread_Start` = 0.935), so use
  those edges via `query_graph` for semantic navigation.
- **Use BM25 (`--query`) for text lookup** — it is precise: `--query "palette
  conversion rgb565"` ranks `Pal_ToRGB565`, `PalFx_ToRGB565`, `Pal_Convert`
  first (`search_mode: bm25`).
- **Path to the binary**:
  `C:/Users/dhani/AppData/Local/Programs/codebase-memory-mcp/codebase-memory-mcp.exe`.
  Shape of a CLI call: `<bin> cli [--json] <tool> [flags]`; `--json` prints the
  raw MCP envelope, without it you get the human summary.
- `watch: false` for CLI sessions; re-run `index_repository` after edits.

## Build environment notes (verified)

The Windows Makefile build is verified with GCC 16.1.0 + GNU Make 4.4.1 in
MSYS2 bash, using the **x86 kit** at `/c/x86devkit` (`i686-w64-mingw32`).
The x64 kit (`/c/w64devkit`, `x86_64-w64-mingw32`) **cannot link** the target —
it has no 32-bit runtime libraries, so `-m32` compiles but `ld` fails on
`-lkernel32`. `make check-toolchain` catches this before any work is done and
is the first thing to run after switching shells:

```sh
export PATH=/c/x86devkit/bin:$PATH   # 32-bit kit first
make check-toolchain && make -j8
```

On Windows there is no choice: the target is i386 only. On Linux the 32-bit
build is optional, and the plan targets **x86-64 and arm64 natively** instead —
which is where the struct-layout guard and the float-precision question come
from (see "Goal" and "Float precision per architecture").

## Static build (done)

`make` on the `static` branch produces **`mugen-static.exe`**: one
self-contained 32-bit Windows executable. Its entire import table is

```
ADVAPI32  GDI32  GLU32  KERNEL32  OPENGL32  SHELL32  SHLWAPI  USER32  WINMM  msvcrt
```

i.e. Windows system DLLs plus MinGW's CRT. **No `Elecbyte.MUGEN.libs`.** SDL
1.2, SDL_gfx, SDL_mixer, SDL_ttf, zlib, pthread and mpg123 come from
`C:\x86devkit` as archives via `pkg-config --libs --static`; Lua, GLEW and
minizip are built from `third_party/`; libresample is vendored.

Verified against the original `mugen.exe` by log diff: startup/mode-select and
quick VS both produce an identical `mugen.log` after numeric normalisation.
Stripped size ~3.5 MB. `make install` strips and copies it to `install/`.

### Two ABI gaps the static link exposed

Both were Elecbyte-private forks, not upstream libraries:

| Symbol | Problem | Resolution |
|---|---|---|
| `resample_*` | Elecbyte's `libresample.dll` is multi-channel; upstream 0.1.3 is mono. `resample_open` has 4 args, not 3, and buffers are interleaved frames. | Vendored upstream and ported: `third_party/libresample`, documented in `VENDORED.md`, verified by `tools/check_libresample.c` |
| `Mix_QuerySpecEx` | Only exists in Elecbyte's SDL_mixer; stock 1.2 has `Mix_QuerySpec` without the chunk size. | `src/sdlmixercompat.c`, static build only |

The libresample port turned up a real bug worth recording, because it would
have been inaudible and permanent: `lrsFilterUp`/`lrsFilterUD` tested
**`Inc == 1`** to mean "right wing", valid only while the step was always one
sample. With interleaving the right-wing step is the channel count, so the test
silently failed and dropped the phase-zero coefficient skip — about 1e-4 of
error on every frame whose phase was exactly zero (for ratio 0.75, every third
frame). Both now test `Inc > 0`. The check program asserts **bit-exact**
per-channel equality across ratios covering both `SrcUp` and `SrcUD`.

### libpng: vendored 1.2.38 → system libpng16 (GNU Make build only)

The static build originally compiled `third_party/libpng-1.2.38`, which
collided with the kit's `libpng16` (`multiple definition of png_set_mem_fn`,
pulled in transitively). Resolution:

* `STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng16` — libpng and
  zlib are now asked for explicitly instead of arriving as somebody else's
  transitive dependency.
* **`SDL_image` is deliberately not in the list.** The engine loads PNG itself
  (`src/pngio.c`) and never calls `IMG_*`; SDL_image was only there because
  your original list named it, and `SDL_image.pc` is what dragged in the
  conflicting `libpng16`.
* `src/symbols.h` no longer re-declares the libpng functions. They were always
  redundant (`mugen.h` includes `<png.h>`) and only compiled because 1.2.38
  left `png_struct`/`png_info` opaque and used non-const `png_byte *`. libpng
  1.6 exposes the structs and takes `png_const_bytep`, which made all 21
  declarations "conflicting types". The original's addresses are preserved as a
  comment block so nothing is lost.

The engine only uses stable 1.x API (`png_create_read_struct`,
`png_set_read_fn`, `png_read_image`, `png_read_update_info`, `png_set_IHDR`,
`png_write_image`, …) and passes `PNG_LIBPNG_VER_STRING` — a macro from
whichever header it compiles against — so it is version-agnostic.

**The MSVC/CMake build still links the vendored 1.2.38**; only the GNU Make
static build switched. That keeps the reference build unchanged.

### SDL export fix (from the kit side)

Rebuilding SDL 1.2 without `.drectve` export directives removed the ~612
exported SDL/Mixer/TTF names the static exe used to carry — the executable now
exports **nothing**, which is what a game exe should do. This was the one
cosmetic wart of the static link and it is gone.

Kit-side notes encountered while doing this, worth knowing because they look
like project bugs and are not:

* `libbz2.a` shipped without an archive index (`archive has no index; run
  ranlib`). Fixed in the kit.
* A partially-installed SDL 1.2 (archives present, `.pc` files or the archives
  missing at different times) makes `pkg-config` silently return an **empty**
  library list rather than failing. Symptom: a link error about a symbol that
  clearly exists, e.g. `undefined reference to inflateInit2_`. When a link
  fails oddly, run `pkg-config --libs --static $STATIC_PKGS` on its own first.

## GLEW → glad (done)

The OpenGL loader is now **glad 2.1**, generated for exactly this engine:
`--profile=compatibility --api=gl=2.1` plus `GL_ARB_fragment_program`,
`GL_ARB_texture_non_power_of_two`, `GL_EXT_blend_logic_op` and
`GL_EXT_framebuffer_object`. `third_party/glew-1.5.4` is no longer built.

| | GLEW 1.5.4 | glad 2.1 |
|---|---|---|
| Vendored source | 1.6 MB | **236 KB** |
| Object file | 379,804 B | **57,493 B** |
| Compile time (`-O2`, i686) | 5838 ms | **716 ms** |
| Windows exe | 10,367,680 B | **9,676,001 B** |
| Linux exe | 6,166,648 B | **5,351,952 B** |

### Why it was not a drop-in

The first copies of `glad-2.1` in the tree could not be used, and each reason is
worth recording because the failure modes were quiet:

1. **Generated with `--profile="core"`.** Core deletes the fixed-function
   pipeline, which is all this renderer uses (`glBegin`/`glEnd` in 13 places,
   `glVertex2f` 34, `glMatrixMode`, `glOrtho`, `glTexCoord2f`, `glColor3f`,
   `glLoadIdentity`). Compiling a six-line sample failed on `glBegin`.
2. **Generated with `--extensions=""`.** Not one `GLAD_GL_ARB_*`/`GLAD_GL_EXT_*`
   symbol, so none of the capability flags `Gl_Init` requires existed. The
   renderer would have fallen back to software rendering rather than crashing —
   the dangerous kind of failure.
3. **A fifth extension was needed that the plan had missed.** `src/prim.c`
   gates `Gfx_GlInvertPixel` on `EXT_blend_logic_op`. The "four requirements"
   quoted from `Gl_Init` were not the complete list.

All three are now correct, verified by compiling `glrender.c`, `prim.c` and
`gldetect.c` against the generated header and by checking that every one of the
74 GL functions and all 5 capability flags the engine references exist.

### What changed

| Area | Change |
|---|---|
| `src/symbols.h` | the 38 `g_glew*` macros now point at glad's symbols (`glActiveTexture`, `GLAD_GL_VERSION_2_0`, …) instead of GLEW's `__glew*`/`__GLEW_*`. The original's `.data` addresses and pointer typedefs are kept as trailing comments |
| `src/mugen.h` | `#include <glad.h>` **before** `<SDL.h>` |
| `src/glrender.c` | `glewInit()` → `gladLoadGL()` |
| `src/gfx.c`, `gldetect.c`, `glrender.c`, `prim.c` | dropped their own `<GL/gl.h>`; `video.c` keeps `<GL/glu.h>` but includes it *after* `mugen.h` |
| 11 other `src/*.c` | dropped their own `<SDL.h>`; `mugen.h` supplies it |
| `Makefile`, `cmake/ThirdParty.cmake`, `CMakeLists.txt` | `glew_static` → `glad`, `-DGLEW_STATIC` gone |

### The include-order trap, and why the SDL includes had to go

`glad.h` hard-errors if a GL header was included first (it checks `__gl_h_`), and
`SDL_opengl.h` includes `<GL/gl.h>`. So glad must be first, and it works by
defining `__gl_h_` itself so the system header then self-suppresses. That is
fine inside `mugen.h`, but **not** if a source file includes `<SDL.h>` or
`<GL/gl.h>` on its own beforehand — which twelve files did. Those includes are
now removed; `<GL/glu.h>` (still needed for `gluOrtho2D`) is included after
`mugen.h` for the same reason.

This is a real constraint for future edits: do not add a `<SDL.h>` or GL include
above `#include "mugen.h"` in a source file.

### Fidelity note

The original linked GLEW 1.5.4 **statically** — it does not import
`glew32.dll`, and the copy shipped in `Elecbyte.MUGEN.libs` was never used. So
this is a deliberate, marked departure from how the original was built, not a
behavioural change. Verified: `mugen.log` is still identical to the original on
Windows, and identical between the Windows and Linux builds, with
`render mode 2_20 … success` in all three toolchains — i.e. `Gl_Init(1)` passed,
which requires every capability flag to be set correctly.

## Goal

**One `Makefile` that builds the same source tree for Windows and Linux, on
x86-64 and arm64**, selected automatically from `uname` and the compiler's own
target triple. No second build system, no per-platform fork of the source.

Three targets, all from the existing `Makefile`:

| Target | Detection | Output | Fidelity |
|---|---|---|---|
| Windows i686 | `uname -s` = `Windows_NT`/`MINGW*`/`MSYS*` **and** triple `i686-w64-mingw32` | `mugen-static.exe` | **reference**: layouts and x87 bit-identical to the original |
| Linux x86-64 | `uname -s` = `Linux`, triple `x86_64-*-linux-gnu` | `mugen-linux-x64` | **faithful**: x87 reproducible via `-mfpmath=387` (verified, see below) |
| Linux arm64 | `uname -s` = `Linux`, triple `aarch64-*-linux-gnu` | `mugen-linux-arm64` | **functional**: no x87 exists, float drift accepted and log-diffed |

The Windows build stays the reference and must keep working unchanged. Linux is
additive: same sources, same `Makefile`, different flags.

### Platform detection: `uname` for the OS, GCC for the target

Both are needed, and the division of labour is not arbitrary:

```make
UNAME_S := $(shell uname -s 2>/dev/null)   # decides the OS family
TRIPLE  := $(shell $(CC) -dumpmachine 2>/dev/null)   # decides CPU + ABI
UNAME_M := $(shell uname -m 2>/dev/null)   # reported by check-toolchain only
```

**`uname -m` must not decide the architecture.** Inside MSYS on this machine
`uname -m` reports `x86_64` — the *host* — while the compiler being used is
`i686-w64-mingw32`. Trusting `uname -m` there would select the 64-bit flag set
and produce a binary the project cannot build. Verified:

```
$ uname -m            → x86_64
$ gcc -dumpmachine    → i686-w64-mingw32      (x86 kit)
$ gcc -dumpmachine    → x86_64-w64-mingw32    (x64 kit, cannot link the target)
```

So: `uname -s` picks the OS family, `$(CC) -dumpmachine` picks the CPU and the
ABI, and the compiler wins on any disagreement. The selection:

| `uname -s` | `-dumpmachine` | Target |
|---|---|---|
| `Windows_NT`, `MINGW*`, `MSYS*`, `CYGWIN*` | `i686-w64-mingw32` | win32 (default on Windows) |
| `Linux` | `x86_64-*-linux-gnu` | linux-x64 |
| `Linux` | `aarch64-*-linux-gnu` | linux-arm64 |
| anything else | — | **error**, naming the triple found and the supported list |

`check-toolchain` gains the same logic, so an unsupported combination fails with
a message instead of a wall of link errors. Every detection is overridable
(`MUGEN_TARGET=linux-arm64`) for cross-compiling, where `-dumpmachine` describes
the target anyway.

### What each target changes in the Makefile

Only these differ; the file lists and the engine are shared.

| Variable | win32 | linux-x64 | linux-arm64 |
|---|---|---|---|
| `TARGET` | `mugen-static.exe` | `mugen-linux-x64` | `mugen-linux-arm64` |
| `-m32` / `-m64` | `-m32` | none (native) | none (native) |
| float flags | `-mfpmath=387 -fexcess-precision=standard` | **same** — x87 still exists on x86-64 | **none** — no x87; SSE/NEON gives `FLT_EVAL_METHOD == 0` |
| `-mwindows` | yes | no | no |
| resources | `windres` on `mugen.rc` + manifest | skipped | skipped |
| SDL stack | kit archives via `pkg-config --libs --static` | distro `pkg-config` | distro `pkg-config` |
| `PTW32_STATIC_LIB` | yes (pthreadVC2 header) | no — glibc pthread | no |
| `__int64` / `__cdecl` | provided by MinGW `_mingw.h` | **shim needed** (both) | **shim needed** (both) |
| `MUGEN_LAYOUT_32BIT` | 1 | 0 | 0 |

`MUGEN_LAYOUT_32BIT` is new and is what makes the 64-bit build possible at all;
see "The struct layout question" below.

### Verified on WSL2 / Ubuntu 22.04

WSL2 is available on this machine (`Ubuntu-22.04`, `Linux 6.6 x86_64`), so the
claims below were **executed, not assumed**. Toolchain: gcc 11.4.0, GNU Make
4.3, pkg-config 0.29.2.

**Every dependency is one `apt install` away — including SDL 1.2.** `apt-cache
policy` on a stock Ubuntu 22.04:

| Package | Candidate |
|---|---|
| `libsdl1.2-dev` | **1.2.15**+dfsg2-6 — the exact version the engine targets |
| `libsdl-mixer1.2-dev` | 1.2.12 |
| `libsdl-ttf2.0-dev` | 2.0.11 |
| `libsdl-gfx1.2-dev` | 2.0.25 |
| `libpng-dev` | 1.6.37 (matches the 1.6 the Windows static build now uses) |
| `zlib1g-dev` | 1.2.11 |
| `libmpg123-dev` | 1.29.3 |
| `libgl1-mesa-dev` | 23.2.1 |
| `liblua5.1-0-dev` | **5.1.5** |
| `libfreetype-dev`, `libbz2-dev` | present |

This **retires the biggest risk in the plan**. The earlier worry was that modern
distros ship SDL2 only; Ubuntu 22.04 carries 1.2.15, so no `sdl12-compat`
shim and no vendoring is needed. Lua 5.1.5 is also packaged, so even Lua need
not be vendored there (`third_party/lua-5.1.4` stays as the fallback for distros
that dropped 5.1, and to keep the Windows build self-contained).

**The two known compile blockers, confirmed by compiling:**

```
$ gcc -c portcheck.c
error: unknown type name '__int64'; did you mean '__int64_t'?
error: expected '=', ',', ';', 'asm' or '__attribute__' before 'fn'   ← __cdecl
```

`__cdecl` failing on **x86-64 Linux** is a correction to the earlier research,
which implied it was only absent on arm64. It is a MinGW/Windows attribute, and
Linux GCC rejects it on every architecture. Both are fixed by two `#define`s:

```c
#define __int64 long long
#define __cdecl
```

after which the same file compiles clean.

**`-mfpmath=387` confirmed on Linux GCC, not just MinGW.** A `_Static_assert`
on `__FLT_EVAL_METHOD__` fails under default (SSE) flags and passes with
`-mfpmath=387 -fexcess-precision=standard`. So the claim that **linux-x64 is a
fidelity target** holds on real Linux GCC 11.4, the compiler Ubuntu 22.04 ships.
Worth re-checking on the distro actually used, since the flag's effect is a
back-end property.

**Struct growth matches the prediction.** A representative
`{ int x; void *p; int y; }` is 12 bytes at 32-bit and **24 bytes** at 64-bit,
with `p` moving from 4 to 8 and `y` from 8 to 16 — exactly the cascade the
`MUGEN_LAYOUT_32BIT` guard is designed to absorb.

**The detection block was prototyped and verified on all four paths**, with the
prototype kept out of the build tree:

| Input | Result |
|---|---|
| MSYS + `i686-w64-mingw32` | `win32` |
| MSYS + `x86_64-w64-mingw32` | **error**: "cannot link this 32-bit target; use the i686 kit" |
| Linux + `x86_64-linux-gnu` | `linux-x64` |
| Linux + `aarch64-linux-gnu` (stub compiler) | `linux-arm64` |
| anything else | **error**, naming the triple found |

The Windows/x64-kit case is the one that matters most in practice: it turns the
current confusing `skipping incompatible libkernel32.a` link failure into one
sentence naming the fix.

**Not done:** a full Linux build. `apt install` needs `sudo`, which needs a
password this session does not have. Everything above is compiler-level
verification; the first real build will still surface the Win32/CRT source work
in steps 1–5.

### The struct layout question, measured

The 32-bit build is checked by **205 `MUGEN_ASSERT_SIZE` and 669
`MUGEN_ASSERT_OFFSET`** `_Static_assert`s. On 64-bit, pointers double and every
assertion downstream of one fails. Measured across `src/*.h`:

| | Count |
|---|---|
| Structs with a size assertion | 200 |
| …**containing at least one pointer**  → break on 64-bit | **105** |
| …containing no pointers → survive untouched | 95 |
| Pointer members in all headers | 184 |
| `Player` / `GameVars` pointers (sizes `0x2490` / `0x13108`) | 25 / 33 |

So this is not "fix a handful of layouts" — slightly more than half the asserted
structs move. The saving grace is **how** the code reads them:

| Check | Result |
|---|---|
| Raw byte-offset arithmetic into structs (`(char *)p + 0x…`) | **0 occurrences** |
| Access is by named member everywhere | yes |
| `memcpy`/`memset` with `sizeof(Type)` (scales automatically) | 116 |
| `int`↔pointer casts that would truncate | **0** |

Because nothing indexes structs by absolute offset, a 64-bit relayout is
**functionally safe**: the structs get bigger, offsets shift, and the code —
which only ever uses member names — does not care. The offsets exist for
fidelity diff-testing against the original, not for correctness.

Therefore: wrap the assertions in `#if MUGEN_LAYOUT_32BIT` (defined when
`sizeof(void *) == 4`). They stay fully active on the reference build, where
they are the guard that keeps the reconstruction honest, and become no-ops on
64-bit, where the original's offsets are meaningless by definition. Nothing else
in the structs changes.

One serialized format exists — the `.ai` character AI files (`src/ai.c`,
`fread`/`fwrite` on `AiFileHeader`, `AiMove`, `AiMoveStep`, `AiStat`). All of
those are plain data with **no pointers**, and carry a signature + version
header, so a 64-bit build reads the same files a 32-bit build wrote. No format
fork.

### Float precision per architecture

The engine reproduces x87 semantics: intermediates in `double`, stores rounded
to `float`. Both Windows builds pin `-mfpmath=387
-fexcess-precision=standard`.

**x86-64 can do the same.** x87 still exists in 64-bit mode; GCC just prefers
SSE by default. Forcing it back gives the identical `FLT_EVAL_METHOD`. Verified
on this machine, same source:

| Build | `FLT_EVAL_METHOD` |
|---|---|
| `i686` + `-mfpmath=387 -fexcess-precision=standard` (shipping) | **2** |
| `x86_64` default (SSE) | 0 |
| `x86_64` + `-mfpmath=387 -fexcess-precision=standard` | **2** |

So **linux-x64 is a fidelity target, not a functional compromise** — same flags
as the reference build. Expect it to be slower on float-heavy code (x87 is
serial), which is an acceptable and measurable trade.

**arm64 cannot reproduce it at all.** AArch64 has no x87; scalar float/double go
through NEON/FP registers with `FLT_EVAL_METHOD == 0` and no excess precision.
There is no compiler flag that changes this. Consequences:

- Trigger maths, sprite scaling, parallax and `Ftol` conversions can differ by an
  ULP from the reference.
- `mugen.log` will **not** be identical to the original's; the target is
  "same behaviour, values within tolerance", audited by hand on the numeric
  lines rather than by exact diff.
- Anything that depends on exact truncation at a boundary (a state change on
  `time >= 10`, a sub-pixel sprite offset) is the place to look when arm64
  misbehaves. This is the one target where the fidelity rule has to be relaxed,
  and the relaxation must be written down in the code that needs it.


## Research findings

### How much Windows is actually in `src/`

10 files include `windows.h` (verified: `error.c`, `gfx.c`, `gldetect.c`,
`glrender.c`, `matchload.c`, `mugen.h`, `prim.c`, `system.c`, `timer.c`,
`video.c`), but the real Win32 surface is small. Grep for types and APIs, not
includes:

| File | Real Win32 usage | Linux replacement |
|---|---|---|
| `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `mugen.h` | none — the include only pulls in `GL/gl.h` transitively | delete `#include <windows.h>`, include `<GL/gl.h>` (Mesa) |
| `timer.c`, `matchload.c` | `timeGetTime`, `QueryPerformanceCounter` (`timer.c` also `timeBeginPeriod`); `matchload.c` reads the timer at lines 28-31 and 1189-1192 | `clock_gettime(CLOCK_MONOTONIC)` shim |
| `video.c` | `Video_SetFullscreen` pokes `HWND` styles via `SDL_SysWMinfo` | `SDL_WM_ToggleFullScreen`, or an X11 path |
| `system.c` | window icon (`LoadIconA` + resource 101), `SHGetFolderPathA(CSIDL_FONTS)` | icon as SDL surface; fonts dir `/usr/share/fonts` |
| `error.c` | RichEdit error dialog (`Riched20.dll`, `DialogBoxParam`, the .rc dialog 200) | SDL message box or terminal output |
| `sdlmain.c` | `WinMain`, the `DDRAW.DLL` preload quirk, stdout redirection | plain `main()`; SDLmain is not needed on Linux |
| `bgm.c`, `bgmplugins.c` | `LoadLibraryA` / `GetProcAddress` for BGM plugins | `dlopen` / `dlsym`, near line-for-line |
| `crt.h` | `<direct.h>` (`_mkdir`) | `<sys/stat.h>` `mkdir()` |

CRT shims needed across `src/` (counts from grep): `_stricmp` 92,
`_snprintf` 44, `_vsnprintf` 9, `strlwr` 6, `_alloca` 3 → one compat header
maps them to `strcasecmp`, `strncasecmp`, `snprintf`, `vsnprintf`, a small
helper, `alloca`. No `_findfirst`, `_access`, `_itoa` uses.

Threads are already POSIX: 17 `pthread_*` symbols, no semaphores, no sched.
On Linux glibc provides them natively, so `pthreadVC2.dll` simply disappears.

### Library sources, DLL by DLL

| DLL | Linux source | Feasibility |
|---|---|---|
| `SDL.dll` 1.2.15 | libsdl 1.2.15 source; or `sdl12-compat` over SDL2 (what modern distros ship) | straightforward |
| `SDL_mixer.dll` 1.2.11 | libsdl-mixer 1.2.12 source | straightforward |
| `SDL_ttf.dll` 2.0.11 | libsdl-ttf 2.0.11 source + freetype (vendor freetype too) | straightforward |
| `SDL_gfx.dll` | sdl-gfx 2.0.26 source; the game calls only `zoomSurface` (2 sites) — alternatively reimplement those two calls with SDL | trivial |
| `zlib1.dll` 1.2.3 | zlib source (~30 files) | trivial |
| `libresample.dll` 0.1.3 | libresample source — **Elecbyte's multichannel variant**: vanilla 0.1.3 is mono and its API differs; the extra exports (`resample_dup`, `resample_get_filter_width`, `resample_get_in/outbuffer_size`) and the interleaved-buffer `resample_process` signature confirm a modified build. Needs a patch or a faithful re-derivation | needs patch |
| `pthreadVC2.dll` | glibc pthreads | delete |
| `libmpg123-0.dll` | libmpg123 source or distro package | straightforward |
| `Riched20.dll`, `DDRAW.DLL` | not needed; the code paths they serve get replaced | n/a |

Headers: the import-time headers in `third_party/sdk/include` stay as the
reference for what the code expects; on Linux the vendored sources carry their
own headers, so `third_party/sdk/include/SDL` becomes unnecessary once the SDL
stack is vendored. `libresample.h` (written for this project from call sites)
remains useful as the contract for the multichannel build.

Planned `third_party/` additions:
`SDL-1.2.15/`, `SDL_mixer-1.2.12/`, `SDL_ttf-2.0.11/`, `freetype-<ver>/`,
`SDL_gfx-2.0.26/`, `zlib-1.2.3/`, `libresample-0.1.3-multich/`.
Each built by the Makefile like lua/libpng, or satisfied by distro packages
(`libsdl1.2-dev`, `libsdl-mixer1.2-dev`, `libsdl-ttf2.0-dev`, `libsdl-gfx1.2-dev`,
`zlib1g-dev`, `libmpg123-dev`) — both routes legitimate; distro packages are
less work and apt-pinned, vendoring is self-contained and version-pinned.

### What has to change in the current Makefile

As it stands today it is a Windows build despite being GNU Make. Every blocker
has a conditional answer, which is why the plan is one Makefile rather than two:

| Blocker | Where | Conditional answer |
|---|---|---|
| `-mwindows` + the `-mwindows` stripping from `PKG_LIBS` | link line | Windows only; defined as an empty variable on Linux |
| `windres` on `src/mugen.rc` + the manifest | `$(RC_OBJ)` | Linux sets `RC_OBJ` empty and drops it from the link |
| `-I$(SDK_DIR)/include` (an MSVC/`__declspec` tree) | `CPPFLAGS` | Windows only; Linux excludes it |
| `-m32` with an `i686-w64-mingw32` compiler | `COMMONFLAGS`, `check-toolchain` | `-m32` is Windows-only; Linux is native |
| `-lSDLmain` / `src/sdlmain.c` `WinMain` | link, source | Linux compiles a `main()` path and does not link SDLmain |
| `_CRT_SECURE_NO_WARNINGS`, `PTW32_STATIC_LIB`, `-D_CRT_NONSTDC_NO_DEPRECATE` | `ENGINE_DEFS` | Windows only; Linux gets the compat header instead |
| `$(TARGET)` = `.exe`, `install` → `install/` | target, install | per-target name; Linux installs to `$(PREFIX)/bin` |
| kit `pkg-config` archives | `STATIC_PKGS` | same package names, resolved from the distro instead |

So the Makefile becomes: detection block → per-target variable block → shared
sources and rules. That is roughly **20% of the Linux job**; the other 80% is
source portability, which is where the plan below spends its steps.

Dependencies also change shape. Windows compiles Lua/GLEW/minizip from
`third_party/` and links a kit's SDL; on Linux most come from the distro —
`libsdl1.2-dev`, `libsdl-mixer1.2-dev`, `libsdl-ttf2.0-dev`,
`libsdl-gfx1.2-dev`, `libpng-dev`, `zlib1g-dev`, `libmpg123-dev`,
`libgl1-mesa-dev` — and only **Lua 5.1.4 stays vendored**, because distros no
longer ship 5.1. GLEW 1.5.4 and minizip 0.15 can stay vendored or come from the
distro; vendored is safer, since `src/symbols.h` pins their symbols.

**SDL 1.2 availability: resolved for Ubuntu 22.04, still worth checking per
distro.** This was the plan's biggest risk — several newer distros ship SDL2
only, and the engine is written against `SDL_SetVideoMode`. Ubuntu 22.04 carries
`libsdl1.2-dev 1.2.15` (verified, see "Verified on WSL2"), so no shim and no
vendoring is needed there. On a distro that has dropped 1.2, the answer is
`sdl12-compat` over SDL2 — which is a real porting cost, not a drop-in — so
confirm before starting:

```sh
pkg-config --exists sdl && pkg-config --modversion sdl   # want 1.2.15
```

## Port work plan

Ordered; each step leaves the tree buildable on Windows (the existing MSVC and
MinGW builds must keep working — nothing in `src/` or `third_party/` existing
today is modified incompatibly).

Three things the static Windows build already settled for the Linux port:

* **libresample is no longer a DLL to replace** — it is vendored source with a
  known, tested API (step 2, partly done).
* **The "vendor every library" strategy is proven workable**, but it is not the
  cheapest route on Linux: there, most of these are one `apt install` away and
  only Lua 5.1.4 needs vendoring, because no distro ships 5.1 any more.
* The static build also produced the dependency list Linux has to satisfy, and
  demonstrated that the engine tolerates a **newer libpng** (1.6 instead of
  1.2.38) and a differently-packaged SDL without behaviour change — which
  makes "use distro libraries" a much smaller risk than it looked.

| # | Step | What it involves | Status |
|---|---|---|---|
| 1 | Compat shim layer | `src/compat_linux.h` / `compat_linux.c`: CRT shims, `dlopen` typedefs for `HMODULE`/`GetProcAddress`, monotonic clock. Included from `crt.h`/`mugen.h` behind a platform check. Unblocks ~10 files with zero game-logic edits | pending |
| 2 | Vendor libresample | **done for Windows** — `third_party/libresample` is upstream 0.1.3 ported to the multi-channel ABI; see "Static build" above. zlib still comes from the kit on Windows and from the distro on Linux | resample **done** |
| 3 | Remove pure-`windows.h` includes | `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `mugen.h` — mechanical; include `<GL/gl.h>` directly. Note `matchload.c` is **not** in this group: it calls `QueryPerformanceCounter`/`timeGetTime` | pending |
| 4 | Port the 4 real Win32 spots | timer (monotonic clock), fullscreen (SDL), window icon + fonts dir, error dialog (SDL message box). ~200 lines total | pending |
| 5 | SDL stack | distro packages: `libsdl1.2-dev` (1.2.15 confirmed on Ubuntu 22.04), `libsdl-mixer1.2-dev`, `libsdl-ttf2.0-dev`, `libsdl-gfx1.2-dev`; `sdlmain.c` gets a plain `main()` path on Linux; `mugen.rc`/manifest skipped (not a PE) | pending — dependency availability **verified** |
| 6 | **Platform detection in the Makefile** | `UNAME_S`/`UNAME_M`/`$(CC) -dumpmachine` selection block, per-target variable blocks (`TARGET`, float flags, `-mwindows`, `RC_OBJ`, `STATIC_PKGS`, `ENGINE_DEFS`), `check-toolchain` reporting the detected target, `MUGEN_TARGET=` override for cross-compiles. Linux output names and `$(PREFIX)/bin` install. **Unsupported triple = hard error** naming what was found | pending — design in "Platform detection" above |
| 7 | **`MUGEN_LAYOUT_32BIT` assertion guard** | Wrap the 205 `MUGEN_ASSERT_SIZE` + 669 `MUGEN_ASSERT_OFFSET` in `#if MUGEN_LAYOUT_32BIT` (defined when `sizeof(void *) == 4`). Active on the reference build, no-ops on 64-bit. No struct changes: 0 raw-offset accesses means relayout is functionally safe (measured above) | pending |
| 8 | 64-bit correctness pass | Build x64, fix what the compiler finds. Two are already confirmed by compiling on WSL Linux: **`__int64`** (MinGW defines it in `_mingw.h`, glibc does not — `unknown type name`) and **`__cdecl`** (a MinGW/Windows attribute, **rejected by Linux GCC on x86-64 as well as arm64**) — 36 and 1607 uses respectively, so both are one `#define` in the compat header. Then whatever `long`-width assumptions remain | pending; the two known ones are verified |
| 9 | arm64 build | native compile on AArch64; expect `FLT_EVAL_METHOD == 0` and accept float drift. Audit the numeric lines of `mugen.log` by hand rather than by exact diff | pending |

Order rationale: steps 1–5 make **one** Linux target build; 6 makes the
`Makefile` choose between them; 7–8 turn the third target on; 9 is the fourth.
Steps 6 and 7 are independent of each other and of the source port, so they can
land first if a Linux target is not yet available to test against.

Effort estimate: steps 1, 3, 6, 7 are mechanical (~2–3 days total); 4 is
behavioural (~1–2 days); 5 is unblocked on Ubuntu 22.04 (SDL 1.2.15 packaged); 8 is
unknown-but-bounded by the compiler's error list; **9 has no fixed end**, because
float drift on arm64 is a per-symptom investigation, not a checklist.

### What "done" means per target

| Target | Done when |
|---|---|
| win32 | `mugen.log` identical to the original (already true) |
| linux-x64 | `mugen.log` identical to the original **on the numeric lines** — the x87 flags make bit equality the expectation, not a hope |
| linux-arm64 | runs a full match without crashing; numeric differences enumerated and explained. Exact equality is **not** a goal here |

If linux-x64 turns out *not* to match bit-for-bit despite the x87 flags, that is
a finding worth recording rather than tuning away — it would mean something
other than the FPU (libm rounding, `long double` width, or `printf` formatting)
differs, and the log diff will point at which.

---

# Rendering optimization

Research notes on where the renderer spends its time, what can be won without
breaking the fidelity rule (quirks kept, `mugen.log` diff-tested), and what a
modern-API port would really cost. Findings were read off the code; the
counter names are the original's own (`g_glStat*`, `g_palConvertCount`).

## What the renderer is built on

The floor is **OpenGL 2.0** (`Gl_Init`, `src/glrender.c:472`): it requires
`g_glewVersion20`, `ARB_texture_non_power_of_two`, `EXT_framebuffer_object`,
`ARB_fragment_program`. Shaders are GLSL `#version 120` (`src/gldetect.c:33`).
But the draw path itself is **immediate-mode fixed function**:

| Call | Uses |
|---|---|
| `glBegin` / `glEnd` | 13 / 13 (one triangle strip per sprite) |
| `glVertex2f` | 34 |
| `glTranslatef` / `glScalef` / `glRotatef` / `glMultMatrixf` / `glLoadIdentity` | 10 / 1 / 3 / 1 / 4 per sprite transform |
| `glMatrixMode`, `glOrtho` | 5, 1 — fixed-function projection |
| `glTexImage2D` | 2 (full reallocation, incl. per-sprite streaming) |
| `glReadPixels` | 1 (screenshots, `Gl_Capture`) |

So today: GLSL fragment shaders (palette lookup, PalFX) sitting on top of a
fixed-function pipeline. 39 `g_glew*` entry points and 5 feature flags are the
whole GL abstraction — there is no renderer interface to swap out.

## Knobs that need no code change

| Knob | Where | Effect |
|---|---|---|
| `rendermode = opengl` | `config.c:243` (default is `system`!) | GL textures + 32-bit vs RGB565 software |
| `vretrace` / Ctrl+V | `config.c:233`, `gameloop.c:152` | vsync on/off |
| `[Debug] Speedup`, frame skip | `frame.c:194`, Ctrl+F/S | `skip_draw` when >1 frame late; timer reset when >4 late |
| `spritedecompressionbuffersize` | `config.c:174` | **top cheap win**: fixed pool, min 128 KB; too small → `SpriteCache_Get` misses every draw → per-frame decode |
| `blitmode = hardware` | `config.c:266` | `VIDEO_HWBLIT` |
| `safemode` | `config.c:261` | skips the screen buffer (`versus.c:103`) but also skips `Gl_DetectOffsets` |
| `fps` console command | `console.c:1607` | FPS counter |

The original's profiling counters are incremented but **never displayed**:
`g_glStatSpriteTexture/Stream`, `g_glStatPalTexture/Stream`,
`g_glStatTexCompress` (`globals.c:275`), `g_palConvertCount` (`globals.c:180`).

## Per-draw costs in the GL path

Per sprite, `Gl_DrawSprite` → `Gl_DrawQuad*`:

1. **Texture reallocation per draw** (`g_glStatSpriteStream`): `GlTex_Upload2D`
   → `Gl_Premultiply` (**malloc + per-pixel copy of w·h·4**) → `glTexImage2D`
   (reallocates the texture object) + 4 `glTexParameteri`. The screen texture
   already uses `glTexSubImage2D` (`glrender.c:295`); sprites do not.
2. **Palette upload per paletted draw** (`g_glStatPalStream`): 256 RGBA colours
   re-uploaded every sprite. Independently, `SpriteSet_GetPaletteConverted` →
   `Pal_Convert` → `PalFx_ToRGB565` **recomputes all 256 colours on every
   call** (`palette.c:178`), on every paletted draw and in both renderers —
   the software renderer needs the RGB565 array, the GL renderer does not use
   it at all. Fixed by the conversion cache (work plan step 2).
3. **Uniform churn**: `Glr_UseProgram` caches the program, but
   `Glr_SetUniforms` re-sends all 9 uniforms — including the two `texunit`
   integers that never change — per quad. Filter params and blend state are set
   per sprite as well.
4. **Filtering inconsistency**: `GlTex_Upload` sets `GL_NEAREST`, every draw
   then overrides to `GL_LINEAR` (`glrender.c:739`) — the upload setting is dead.
5. No batching anywhere: one `glBegin/glEnd` and a full transform per sprite.

## Software renderer costs

- **`Pal_Convert` per sprite draw** (same as GL item 2) — 256-entry RGB565
  conversion per draw, in the *default* render mode. Top CPU cost in mode 0.
- **`Gfx_SoftPutPixel` calls `SDL_UpdateRect(surface, x, y, 1, 1)` per pixel**
  (`prim.c:191`). Only used by debug overlays (`playerstate.c:2465`,
  `title.c:363`), but pathological for any caller that plots many pixels.
- Inner loops are decent: 16.16 fixed point stepping (`softrender.c:401`), no
  per-pixel float.
- `Soft_Put` branches on `op` inside the per-pixel loop (`softrender.c:64`)
  though the drawers are already split per op — should be compile-time.
- The rotated drawer divides per row (`softrender.c:717`) — hoistable.
- SSE2 over opaque/masked blitters is safe; additive/subtractive must match
  `pal_clamp` exactly.
- `SpriteCache_Get` does a **linear scan** of the decompression pool for the
  key (`spritecodec.c:1132`) on **every** sprite draw in **both** renderers,
  via `Sprite_GetPixels`. Eviction is cost-window based, not LRU.

## Dead code worth reviving: `g_glSpriteBufferFlags`

`Sprite_DrawGL` (`glrender.c:945`) checks `(g_glSpriteBufferFlags & 3) == 3`
and then draws scaled/rotated paletted sprites at natural size into a vbuf and
blits that buffer — the cheap path for scaling a tiny texture. **Nothing ever
sets the variable** (only `globals.c:215`, `symbols.h:293` and the two reads),
so the path never runs. Setting it to 3 is a one-line experiment.

## Rendering work plan

| # | Step | Risk | Status |
|---|---|---|---|
| 1 | Add a `stats` console command dumping the counters (decompression pool, texture memory, sprite/palette stream vs. texture, palConvertCount) | none | **done** |
| 2 | Cache `Pal_Convert` results (palette identity + applied PalFX values as key) | low, perf only | **done** |
| 3 | `glTexSubImage2D` when the texture already has the same size/format + reusable premultiply scratch buffer instead of `malloc`/`free` per sprite | low, no behaviour change | **done** |
| 4 | Try `g_glSpriteBufferFlags = 3` (measure; visual change) | low, visual | pending |
| 5 | Hash index for `SpriteCache_Get` instead of the linear pool scan | low | pending |
| 6 | Batch draws (vertex buffers), `Soft_Put` template, SIMD blitters | **changes rounding** — must be opt-in and `mugen.log` diff-tested | pending |

Notes on steps 2 and 3 as implemented (`src/palette.c`, `src/glrender.c`):

- The cache key **cannot** be `g_palFxApplied.serial`: that field overlaps
  `g_palFxApplied.color` (see the comment at the top of `palette.c`), so the
  applied `color/add/mul/invertall` values are compared instead. Entries are
  dropped by `Pal_Init` and `Pal_Free`; `Pal_Init`'s own `Pal_ToRGB565` (which
  ignores the PalFX) drops the entry so it is never credited. A slot collision
  only costs a recompute. Soundness rests on the RGBA buffer never being
  written in place after `Pal_Init` — it holds SFF data or a `PAL_COPY` copy.
- `Glr_AddMemory` is still called on a reused texture: the original over-counts
  video memory on every re-upload, and that number is in `mugen.log`
  ("Max used tex mem"), which this project diff-tests. Only the reallocation is
  skipped, so the logged figure stays identical.
- `spritedecompressionbuffersize` is deliberately left at the original's 128 KB
  minimum in code: a too-small pool silently drops sprites (`Sprite_DrawSoft`
  returns early when the pixels are not resident), so changing it is a user
  decision in `mugen.cfg`, not a build change. Use `stats` to see whether the
  pool is thrashing (`misses`/`evictions` climbing).

### What changed, exactly

| File | Change | Fidelity impact |
|---|---|---|
| `src/palette.c` | `Pal_Convert` returns early when its cached key matches; 64-entry direct-mapped cache keyed on palette identity + applied PalFX; entries dropped by `Pal_Init` / `Pal_Free` / `Pal_Init`'s own `Pal_ToRGB565` | none — identical `rgb565` output, fewer recomputations |
| `src/glrender.c` | `GlTex_Upload` uses `glTexSubImage2D` when size and internal format already match (tracked per texture in a 4-slot table); `Gl_Premultiply` replaced by a reusable scratch buffer, freed in `Gl_Deinit` | none — `tex->w/h`, error handling and `Glr_AddMemory` unchanged |
| `src/console.c`, `src/symbols.h` | new `stats` command; read-only dump of the pool (size/hits/misses/evictions/used/free), `g_glTexMemory(Peak)`, sprite/palette stream vs. texture counters, `g_palConvertCount` | none — new command, no existing path touched |

### Verification state

**Runtime-tested against the real game** (MSVC 2022 `x86-release` build, copied
into `install/` — the shipped M.U.G.E.N 1.1 Beta 1 tree — and run next to the
original `mugen.exe`):

| Scenario | Result |
|---|---|
| Start-up, mode select, clean shutdown (`-nosound`) | runs; 1347 frames / 22.4 s; `Successful program termination.` |
| Lua | `init.lua` ran (`Lua initing` → `Lua init complete`) — all five `require`s resolved through `package.path = data/?.lua`, so the `Vector` class is live |
| OpenGL renderer | active (`render mode 2_20`, `1280x720x32`) — mode 2 requires GL 2.0 + NPOT + FBO + ARB fragment programs, and `Video_SetMode` would have failed otherwise |
| 2-round AI vs AI match (`-rounds 2 -p1.ai 1 -p2.ai 1`) | **`mugen.log` identical to the original** after numeric normalisation (273 tokens each, 0 differences) |
| Quick VS (`-p1 kfm -p2 kfm`) | **`mugen.log` identical to the original** (106 tokens each, 0 differences) |

Method: both binaries run from the same directory with the same arguments; logs
compared with `diff` after replacing decimal and hex numbers and the executable
name. Matching logs are the project's stated fidelity criterion
(`docs/ANALYSIS.md`, "Validation").

Caveats:

- **Palettes under PalFX** were not visually diffed — a log cannot show colour.
  The palette cache is equivalent by construction (identical `rgb565` output,
  only fewer recomputations) and the match logs match, but a visual check of
  blinks, palette swaps and `AllPalFX` is still worthwhile.
- **The `stats` and `lexec`/`lmode` commands** were not exercised: they need the
  console, which is driven by key bindings that cannot be injected in a headless
  run. Build-verified only.
- **`-log <file>` produced no file in either binary** — the same behaviour in the
  original, so it is a faithful quirk rather than a regression; the GL draw
  statistics printed by `Gl_Deinit` therefore never appear in these runs.
- The test executable was removed from `install/` afterwards; the game tree is
  back to its original contents.

## Modern OpenGL (3.3 / 4.6)?

**No benefit for speed as such, and a real fidelity cost.** What 3.3/4.6 would
buy, concretely:

- **Buffers and VAOs** replace `glBegin/glEnd` — this is the one genuine win
  (batching hundreds of sprites into a handful of draw calls). But that win is
  available without changing GL version: the same idea exists as
  `GL_ARB_vertex_buffer_object` on GL 2.0 hardware. The version is not the
  point; the batching is (`PLANS` step 6).
- **`glVertexAttribPointer` + instancing** lets per-sprite parameters
  (UV rects, PalFX, alpha, blend) travel as vertex attributes instead of
  uniform uploads, killing the per-quad uniform churn (GL item 3).
- **Core profile would force** deleting: `glBegin/glEnd`, `glMatrixMode`,
  `glOrtho`, `glTranslatef`/`glScalef`/`glRotatef`/`glMultMatrixf`, client
  arrays, `GL_QUADS`, wide lines, `glTexEnv`. That is the whole transform and
  primitive layer rewritten: ~60 call sites, plus the projection logic in
  `Gl_SetupView` (which encodes a Y flip for FBOs, `glrender.c:559`) and the
  `gldetect.c` offset probes, which are *fixed-function pixel-centre tests* —
  they would have to be re-derived, and their results feed shader `#defines`,
  so sub-pixel accuracy would silently change.
- **4.6 / compute** offers nothing here: the workload is 2D quads with a 256-
  entry texture lookup. Persistence, DSA, `glMultiDraw*` are conveniences, not
  speedups, for a few thousand quads a frame.
- **Shaders** would go `#version 120` → 330 core (`in`/`out`, `texture()`). Note
  the shaders are **not in this repo**; they are loaded from the game's
  `data/<name>.vert|.frag` at runtime with `#defines` injected at
  `////*EB_DEFINES*////`. A core-profile renderer would need new shader files
  and a fallback, i.e. a data-side dependency the modding community would feel.
- **Context creation is the real blocker**: the game gets its context from
  `SDL_SetVideoMode` (`video.c:194`), i.e. SDL 1.2, which cannot request a core
  profile or a specific version. Any 3.3+ path means moving to SDL2 window +
  `SDL_GL_CreateContext` with attributes — which is the same fork of the video
  layer the Linux port already needs (see the port plan: SDL 1.2 → `sdl12-compat`).

**Verdict**: keep the GL2 renderer as the faithful default and as the
reference for diff-testing. Put batching in behind a flag on the existing GL2
path first (VBAs are available at 2.0); only then consider a core-profile
variant, and treat that as a separate renderer (`RENDER_MODE_GL_CORE`) rather
than a replacement, because it changes rounding and needs its own shader set.

## Vulkan?

**Technically possible, not worth it.** The engine has no renderer abstraction
to slot into — `g_gfx*` is a table of 16 function pointers for *primitives*
(`gfx.c:423`), but sprite drawing bypasses it (`Sprite_Draw` branches on
`RENDER_MODE_GL` and calls `Gl_DrawSprite` directly; call sites in `anim.c`,
`bg.c`, `explod.c`, `gameloop.c`, `select.c`, `matchload.c`). A Vulkan backend
means: instance/device/queue setup, swapchain and resize handling (the game
uses standalone fullscreen-on-monitor window surgery, `Video_SetFullscreen`),
memory heaps and staging for every texture upload, descriptor sets for the
sprite + palette pair, pipeline objects for the 9 shader variants × 2 families,
command buffers and fences per frame, and manual screenshot/readback (replacing
`glReadPixels`). That is a full renderer for a 2D blitter, and it would have to
be maintained in parallel with the GL path forever, because the GL path is the
faithful one.

What would justify it: a driver where GL is broken and Vulkan is not, or a
platform that has Vulkan and no GL (some ARM/embedded). Neither is the target
here. If the batching work (step 6) later shows that draw-call overhead is the
bottleneck, **`GL_ARB_vertex_buffer_object` / instancing on GL 2.0–3.3 gets
most of the same win** with a fraction of the surface area. Vulkan's real
advantage — explicit control over threading and memory — solves a problem a
32-bit single-threaded 2D engine does not have.

If an API-agnostic layer is ever wanted, the cheaper direction is to formalize
the existing de-facto interface (texture create/upload/bind, quad draw with
PalFX+blend, clip, capture, present) into one struct of function pointers next
to `g_gfx*`, then implement GL2 against it. That refactor is worth doing on its
own merits, and it is the prerequisite for *any* other backend, including
Vulkan.

