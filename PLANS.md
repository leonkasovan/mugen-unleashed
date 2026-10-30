# PLANS

Porting mugen-unleashed from Windows/MSVC to Linux/GCC, and optimising the
renderer. This file records the research (what the code actually depends on),
the decisions, and the ordered work plans. Update the status column as work
lands; keep the research sections true to the findings they were based on.

## Status at a glance

| Area | State |
|---|---|
| Windows build (MSVC/CMake) | **done, verified** — outputs `mugen_unleashed.exe`; `docs/BUILD-MSVC.md` |
| Windows build (GNU Make + MinGW-x86) | **done** — `Makefile`, `docs/BUILD-MINGW.md` |
| Linux port | **not started** — research done, 6 steps planned below |
| Lua | **done** — `lmode`/`lexec` implemented, full API documented in `docs/LUA.md` |
| Renderer optimization | steps 1–3 **done and runtime-verified**; 4–6 pending |
| Modern GL (3.3/4.6) / Vulkan | researched, **not recommended** — reasons below |

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

This is the same 32-bit constraint that makes the Linux port's `-m32` vs 64-bit
decision matter (see "The two portability landmines" below): on Windows there
is no choice, the target is i386 only.

## Goal

Build `mugen` on Linux with GCC and a standard Makefile, with every library the
game links either vendored as source under `third_party/` (built by the
Makefile, like lua and libpng already are) or coming from distro packages.
Replace the Windows DLL mechanism entirely: no import libraries, no
`Elecbyte.MUGEN.libs`, no manifests.

Two build targets are in scope:

| Target | Toolchain | Fidelity |
|---|---|---|
| linux-32 | `gcc -m32` on a multilib distro | faithful: struct layouts and x87 float semantics preserved |
| linux-64 | `gcc` (default x86-64) | functional: layout fixes accepted, float drift accepted and tested |

## Research findings

### How much Windows is actually in `src/`

11 files include `windows.h`, but the real Win32 surface is small. Grep for
types and APIs, not includes:

| File | Real Win32 usage | Linux replacement |
|---|---|---|
| `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `matchload.c`, `mugen.h` | none — the include only pulls in `GL/gl.h` transitively | delete `#include <windows.h>`, include `<GL/gl.h>` (Mesa) |
| `timer.c` | `timeGetTime`, `QueryPerformanceCounter`, `timeBeginPeriod` | `clock_gettime(CLOCK_MONOTONIC)` shim |
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

### The two portability landmines

**Struct layouts.** 205 `MUGEN_ASSERT_SIZE` checks pin field offsets
(`src/mugen.h`). On LP64, `long` (in `bgm.h` callback typedefs) and pointers
grow to 8 bytes; some asserts will fail. Options:
- build `-m32` (faithful, needs multilib dev packages), or
- fix the affected layouts with `int32_t` / explicit padding and accept a
  64-bit build that is functionally equivalent but not binary-faithful.
The asserts identify exactly which structs fight back; no guessing.

**x87 float semantics.** The engine assumes `FLT_EVAL_METHOD == 2`
(x87 53-bit intermediates, rounding on every store) — the Win build pins
`-mfpmath=387 -fexcess-precision=standard` for this. On x86-64, SSE2 math gives
`FLT_EVAL_METHOD == 0`; trigger maths, sprite scaling and parallax will drift.
`-m32` sidesteps it; a 64-bit build must accept the drift and diff-test against
the original (`mugen.log` scenarios in README) rather than expect bit equality.

## Port work plan

Ordered; each step leaves the tree buildable on Windows (the existing MSVC and
MinGW builds must keep working — nothing in `src/` or `third_party/` existing
today is modified incompatibly).

| # | Step | What it involves | Status |
|---|---|---|---|
| 1 | Compat shim layer | `src/compat_linux.h` / `compat_linux.c`: CRT shims, `dlopen` typedefs for `HMODULE`/`GetProcAddress`, monotonic clock. Included from `crt.h`/`mugen.h` behind a platform check. Unblocks ~10 files with zero game-logic edits | pending |
| 2 | Vendor zlib + libresample | smallest risk first; removes the two oddest DLLs; wire into Makefile like lua/libpng; patch libresample to the multichannel API of `third_party/sdk/include/libresample.h` | pending |
| 3 | Remove pure-`windows.h` includes | `gfx.c`, `prim.c`, `glrender.c`, `gldetect.c`, `matchload.c`, `mugen.h` — mechanical; include `<GL/gl.h>` directly | pending |
| 4 | Port the 4 real Win32 spots | timer (monotonic clock), fullscreen (SDL), window icon + fonts dir, error dialog (SDL message box). ~200 lines total | pending |
| 5 | SDL stack | vendor `SDL-1.2.15` + `SDL_mixer` + `SDL_ttf` + `freetype` + `SDL_gfx`, or use distro packages; `sdlmain.c` gets a plain `main()` path on Linux; `mugen.rc`/manifest skipped (not a PE) | pending |
| 6 | Layout + float audit | attempt the 64-bit build, let the `MUGEN_ASSERT_SIZE` asserts enumerate the layouts to fix; decide `-m32` vs 64-bit target policy; diff-test `mugen.log` against the original's scenarios | pending |

Effort estimate: steps 1–3 about a day (mechanical); step 4 another day or two
(behavioural); step 6 is where the real time goes for a 64-bit build.

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

