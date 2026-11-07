# Problems found on Linux ARM64 (GLES-only hardware)

Findings from running the Linux ARM64 build on a real handheld: an Anbernic
RG351MP running ArkOS. The target is `mugen-linux-arm64` built against an Ubuntu
20.04 arm64 rootfs, copied to the device and executed there — not emulated.

Environment as measured on the device:

| | |
|---|---|
| Board | Anbernic RG351MP (`rg351mp`), aarch64 |
| OS | ArkOS, Ubuntu 19.10 base, glibc 2.30, kernel 4.4.189 |
| GPU | Mali-G31 (`libmali-bifrost-g31-rxp0-wayland-gbm`), `/dev/dri/card0` |
| GL | **OpenGL ES 3.2** only — `OpenGL ES 3.2 v1.r13p0-01rel0…`, renderer `Mali-G31` |
| Display | KMSDRM, EGL; no X server running |
| SDL | SDL2 2.0.10 |
| Compiler | gcc 9.4.0 from `~/ubuntu_root_arm64` (`~/chroot_arm64.sh`) |

| ID | Problem | Status |
|----|---------|--------|
| P1 | Linux ARM64 asked for desktop OpenGL; the hardware only has OpenGL ES | **Fixed** — `MUGEN_GLES` is now the arm64 default |
| P2 | The start-up error path segfaulted instead of reporting the error | **Fixed** (`src/system.c`) |
| P3 | The GLES renderer drew every frame into the window's back buffer and never swapped it, so the panel stayed black | **Fixed** (`src/gles_render.c`) |

---

## P1 — Linux ARM64 required desktop OpenGL, but the hardware is GLES-only

### Symptom (before the fix)

With `mugen.cfg` `RenderMode = OpenGL` (the shipped default) the engine created
its window and a GL context, then died before the first frame:

```
Initializing graphics...gameCoord 640x480...render mode 2_6...trying 640x480x32 mode 0x0...
video: vsync 0 requested, driver reports 1failed. Exit with an error.
```

`RenderMode = System` worked and reached mode select, so the build, input and
asset pipeline were fine; only the GPU path was unusable.

### Root cause

Two GL loaders were in play, and the engine used the wrong one for this device.

1. **SDL2 created a valid EGL/GLES context.** The `video: vsync … driver reports
   1` line is printed by `Sdl2_ApplySwapInterval` (`src/sdl2compat.c`), which runs
   *after* `SDL_GL_CreateContext` succeeded.

2. **The engine then initialised the desktop loader.** `src/glrender.c`
   `Gl_Init()` calls `gladLoadGL()` (`third_party/glad-2.1`), which `dlopen`s
   `libGL.so.1` and resolves through `glXGetProcAddressARB` — the GLX/glvnd
   desktop stack, not the EGL context SDL had just made current. Measured on the
   device with a probe that replicates `video.c`'s attribute setup:

   ```
   video driver: KMSDRM
   SDL_GL_GetProcAddress(glGetString) -> OpenGL ES 3.2 … (Mali-G31)
   gladLoadGL() = 0
     GLVersion = 0.0   GLAD_GL_VERSION_2_0 = 0
   ```

3. **The version gate then rejected the context.** `g_glewVersion20` is
   `GLAD_GL_VERSION_2_0` (`src/symbols.h`), i.e. `0`, so `Gl_Init` returned 0 →
   `Video_SetMode` returned 0 → `System_Init` logged `failed.` and called
   `Error_Exit`.

The engine already had a complete GLES 3.0 backend (`MUGEN_GLES`:
`src/gles_render.c`, `gles_probe.c`, `gles_draw.c`, `gl_mvp.c`, GLSL ES shaders
in `res/shaders/gles/`), but it was wired for Android only: `gles_render.c` was
guarded by `#if defined(ANDROID_PORT) && defined(MUGEN_GLES)`, `mugen.h` included
`glad_es.h` only inside `#ifdef ANDROID_PORT`, the `Makefile` had no `MUGEN_GLES`
option at all, and the render policy/fallback in `config.c` and `system.c` were
Android-only. `RENDERING_PLAN.md` §12 item 5 recorded exactly this gap.

There is no hardware desktop-GL implementation to fall back on: `libGL.so.1` is
glvnd/GLX, there is no X server and no GLX context, and the only hardware driver
is the Mali GLES one. Mesa's `kms_swrast_dri.so` *is* on the image, so a
*software* desktop-GL context can be forced — but that replaces the GPU path the
port exists to use. **OpenGL ES 3 is the correct backend for this hardware.**

### Fix — `MUGEN_GLES` on Linux ARM64

`MUGEN_GLES` now defaults to **1 for `linux-arm64`** and stays 0 for
`win32`/`win64`/`linux-x64`, so the reference builds and the log comparison
against the original binary are untouched. `make MUGEN_GLES=0|1` overrides
either way; the option is rejected on Windows targets.

What changed, file by file:

| File | Change |
|---|---|
| `Makefile` | New GLES block: default per target, `MUGEN_GLES` in `CONFIG_ID` (so switching recompiles), `-DMUGEN_GLES`, `GLAD_DIR`/`GLAD_SRC` switched to `third_party/glad-es-3.0`, `-lGL -lGLU` dropped from the link line, `glrender.c` + `gldetect.c` removed from `ENGINE_SRCS`, `make install` stages `res/shaders/gles/*` into `install/data/gles/` |
| `src/mugen.h` | `#ifdef MUGEN_GLES` now selects `glad_es.h` before the `ANDROID_PORT` branch, so the dialect is chosen by `MUGEN_GLES` alone on every platform; plain Android still gets no GL declarations |
| `src/glad_es.h` | Defines `__gl_h_` after including glad, so a system `<GL/gl.h>` read later (via `<GL/glu.h>` in `video.c`) self-suppresses instead of colliding with glad's `#define glClearColor glad_glClearColor` |
| `src/sdl2compat.c` | Requests an **ES 3.0 context** (`SDL_GL_CONTEXT_PROFILE_MASK`/major/minor) before `SDL_CreateWindow`, and loads entry points with `gladLoadGLES2Loader(SDL_GL_GetProcAddress)` after `SDL_GL_CreateContext` — SDL's loader is the only one that resolves EGL entry points. Logs `video: <renderer>, GLES <major>.<minor>`. Adds `Sdl2_GLDrawableSize()` |
| `src/sdl2compat.h` | Declares `Sdl2_GLDrawableSize()` |
| `src/gles_render.c` | Guard relaxed from `ANDROID_PORT && MUGEN_GLES` to `MUGEN_GLES`; `Android_GLDrawableSize` replaced by a `Gles_DrawableSize` helper that calls SDL on the desktop. Log lines at init start with `\n` so they do not splice into `video: vsync …`, and the "context lost" line only prints when something was actually tracked (a cold start has nothing to drop) |
| `src/symbols.h` | Under `MUGEN_GLES` the `g_glew*` capability macros are constant 0 — glad-es has no `GLAD_GL_VERSION_2_0`/ARB flags, and their only consumer (`prim.c`'s desktop section) is excluded |
| `src/prim.c` | The fixed-function GL section is now `#if !defined(ANDROID_PORT) && !defined(MUGEN_GLES)`; its `Gfx_Gl*` functions are defined by `gles_render.c` on GLES and `glBegin`/`glVertex2f` do not exist there |
| `src/gfx.c` | `Gfx_PresentGlScreen`/`Gfx_NextPow2` (desktop-only, they call `GlScreen_Upload`/`GlScreen_Draw` from `glrender.c`) excluded under `MUGEN_GLES`; the present-time `glClearDepth` block skipped (ES spelling is `glClearDepthf`, and the GLES presenter already clears); `RENDER_OPENGL_SCREEN` present logs that the mode is unavailable instead of calling a NULL presenter |
| `src/palette.c` | The palette-as-texture path stays desktop-only: GLES has no 1D textures and its backend uploads the palette into a 256x1 `sampler2D` from the CPU colours each draw, so the palette stays CPU-side (same as Android) |
| `src/video.c` | `Video_ResetGLScreenProjection` and the `RENDER_OPENGL_SCREEN` resize case excluded under `MUGEN_GLES` (fixed-function matrix stack); `Gl_Init` comment updated — the GLES renderer reaches the same call with its context already current |
| `src/config.c` | The `RenderPolicy_Resolve` capability block now runs for `MUGEN_GLES` as well as `ANDROID_PORT`, reporting `system=1, gl_sprite=1, gl_present=1, gl_screen=0`, so an OpenGL request is not rewritten to System before it is tried |
| `src/system.c` | New `#elif defined(MUGEN_GLES)` arm in the failed-`Video_SetMode` branch: falls back to System with a reason instead of `Error_Exit`, like the Vita and Android branches. `MUGEN_STRICT_RENDERER` still turns the fallback into a fatal error |

### Verified on the device

```
Initializing graphics...gameCoord 640x480...render mode 2_6...trying 640x480x32 mode 0x0...
video: Mali-G31, GLES 3.2
video: vsync 0 requested, driver reports 1
GLES: renderer ready (indexed+rgba+solid, max texture 8192)
success...renderer: requested=opengl effective=opengl caps=0x1F
```

`caps=0x1F` is the same capability set Android reports on a real GPU;
`requested=opengl effective=opengl` with `MUGEN_STRICT_RENDERER=1` set means no
silent fallback.

| Check | Result |
|---|---|
| Startup → mode select | runs, `Entering mode select` / `Mode select init` |
| Quick VS (`-p1 kfm -p2 kfm`) | match loads and runs, 1423 frames in 25 s |
| Frame rate vs System, same scenario | 1423 vs 1440 frames — GLES is not slower |
| Pixel output | screenshot of each renderer on the same match, decoded and compared: **99.6 % of pixels non-black** on GLES, mean absolute difference **3.5/255**, max 49. Low-magnitude differences are expected: System is RGB565 and the two runs are separate playthroughs, so they are close rather than bit-identical. **This row proved the renderer, not the screen** — `Gl_Capture` reads the render *target* (the screen-buffer FBO), so a missing present was invisible to it. See P3 |
| Fallback when the ES shaders are missing | logs `GLES: cannot open data/gles/indexed.vert … failed. falling back to the system renderer.` and runs on System (exit 124 = still alive) |
| Strict renderer on that same scenario | exits 1: `strict renderer: requested mode 2 unavailable (effective 0)` |
| Shutdown | `Successful program termination.`, GL resources released |

### Not claimed

- **Pixel parity per fixture is not measured.** The comparison above is one
  screenshot against a separately played frame, not a per-fixture readback. The
  GLES backend still has the limitations RENDERING_PLAN.md §12 lists (one shader
  variant per path, no `OpenGLScreen` presenter, `Gfx_GlInvertPixel` unsupported
  because ES has no logic ops).
- **One device, one image.** Mali-G31 under ArkOS. A different SoC may report a
  different ES version or lack a feature the probe requires; that case falls back
  to System with a logged reason rather than failing.
- **The 64-bit caveat in AGENTS.md still applies**: the layout assertions are
  compiled out on arm64, so this is "portable in progress", not an audited
  target. The GLES backend does not add any offset-dependent code.

---

## P2 — The start-up error path segfaulted (fixed)

### Symptom

Start-up failures that happen before the game variables are created ended with a
SIGSEGV instead of the error dialog. On the device, both the OpenGL failure above
and a deliberately broken video driver (`SDL_VIDEODRIVER=dummy`, `offscreen`,
`x11`) produced:

```
bash: … Segmentation fault … ./mugen-linux-arm64 -nosound
EXIT=139
```

`mugen.log` was complete up to `Exit with an error.` and
`Log file successfully closed.`, so the crash happened *while reporting* the
error, not while detecting it.

### Root cause

Backtrace from `libSegFault` with the symbols resolved against the unstripped
build:

```
System_Init +0x98c
Error_Exit +0x1b8
GameVars_Free.part.0 +0x24
Match_FreeAssets +0x20        <-- SIGSEGV
```

`Error_Exit` (`src/error.c`) calls `GameVars_Free` (`src/system.c`), which begins
with `Match_FreeAssets()`. That function (`src/playerstate.c`) dereferences
`g_game->palgroups` without a check — but `g_game->palgroups` is only allocated by
`GameVars_Init`, which `System_Init` calls **after** video and font setup. Every
failure in that window (video mode refused, font missing, …) freed through a NULL
pointer.

The fault was already known on the Vita port, which worked around it by skipping
`GameVars_Free` entirely under `#ifdef MUGEN_VITA_GL` (`src/error.c`), with a
comment asserting that "Windows and Linux keep the original order, where this is
reachable only after a successful start-up". That assertion is not true: a
refused video mode is exactly such a failure, and Linux reached it.

### Fix

`src/system.c`, in `GameVars_Free`:

```c
if (g_game->palgroups != NULL) {
    Match_FreeAssets();
}
```

The later `free(g->palgroups)` in the same function already tested for NULL, so
the original author knew the pointer can be absent — only this earlier use of it
was missed. The rest of the teardown (fonts, video surface, the game struct) is
kept, since it is what releases a half-built game. Marked in the code as a port
fix, with the reasoning.

This matters more now than it did: P1's failure path is a start-up error, and
that is precisely the sequence that used to crash.

### Verification

| | Before | After |
|---|---|---|
| `RenderMode = OpenGL` (the failing path) | exit **139** (SIGSEGV) | exit **1**, `Exit with an error.`, `Log file successfully closed.` |
| `RenderMode = System` | exit 124 (runs until killed) | exit 124, still reaches `Mode select init` |

Not a fidelity change: the original on Windows reports the error and exits, it
does not crash.

---

## P3 — The GLES renderer never presented the frame (black screen)

Found on the device after P1 was fixed: `mugen.cfg` `RenderMode = OpenGL`, the
log completely healthy, the game running — and nothing on the panel.

### Symptom

```
Initializing graphics...gameCoord 640x480...render mode 2_6...trying 640x480x32 mode 0x0...
video: Mali-G31, GLES 3.2
video: vsync 0 requested, driver reports 1
GLES: renderer ready (indexed+rgba+solid, max texture 8192)
success...renderer: requested=opengl effective=opengl caps=0x1F
...
Entering mode select.
Mode select init
End of mode select loop
Frames      : 1209
Successful program termination.
```

No error, no fallback, 1209 frames drawn, a clean shutdown — and a black screen
throughout. Every status line that exists said the renderer was fine.

### Root cause

`Gl_Present` (`src/gles_render.c`) draws the screen-buffer texture into the
window's default framebuffer — and stops there. **Nothing swapped.**

That is correct on Android, where M8 put the swap outside the renderer:
`Android_GLPresent()` is called from `Screen_EndFrame` (`src/frame.c`, inside
`#ifdef ANDROID_PORT`). The SDL2 desktop has no such hook, and the two things
that could have covered for it do not:

- `g_gfxPresent` for `RENDER_OPENGL` **is** `Gl_Present`, so the desktop's
  swap in `src/glrender.c Gl_Present` is not in this path;
- `Sdl2_PresentFrame()`, called at the frame boundary by `Screen_Present`, only
  flushes the *System* renderer's software surface. It returns immediately for a
  GL window, because `Sdl2_MarkDisplayDirty` deliberately ignores one.

So every frame was drawn into the back buffer and then never shown. The
`#ifdef ANDROID_PORT` around the swap in `Screen_EndFrame` is the line that the
Linux port needed to duplicate and did not.

This is a port bug introduced by making a renderer reusable on a platform whose
frame-boundary hook it did not have — the same class as P1, one layer further
out.

### Fix

`src/gles_render.c` `Gl_Present`, after the quad and the `g_gfxPrePresent`
overlays, on every target except Android:

```c
if ((g_videoFlags & VIDEO_HWBLIT) && (g_videoSdlFlags & SDL_DOUBLEBUF)) {
    SDL_GL_SwapBuffers();
} else { /* once-per-run log: a GL mode without double buffering cannot present */ }
```

This is the same tail the desktop GL renderer has in its own present, so both
plugins present from the same place. Android keeps its frame-boundary swap and
is untouched (`#ifndef ANDROID_PORT`).

Two silent-black guards were added while tracking this down, because a healthy
log plus a black panel has no other tell:

| Guard | Behaviour |
|---|---|
| `Gl_Present`, drawable size | a one-time `GLES: drawable size is WxH; the frame cannot be presented` instead of returning quietly |
| `Gl_Present`, double buffering | a one-time `GLES: GL mode is not double buffered; the frame cannot be presented` instead of drawing and never swapping |

### How it was found

Instrumented builds, run on the device (the instrumentation was removed
afterwards; the numbers below are from those runs):

| Question | Method | Result |
|---|---|---|
| Is the frame being drawn at all? | `glReadPixels` of the screen-buffer FBO | 3832/4096 non-black — the renderer was fine |
| Which step returns early? | step markers through `Gl_Present` | all four steps reached, so nothing returned early and the swap was simply missing |
| Is the image in the **window** framebuffer? | `glReadPixels(GL_FRAMEBUFFER 0)` before the swap, 32x32 centre | non-black, `max_sum=400` |
| Is the geometry or the texture at fault? | same quad, drawn with the solid program in red | red appeared — geometry, projection and depth state are correct |
| What does the panel actually receive? | dumped the whole 640x480 window framebuffer to a PPM before the swap and decoded it | 50 % of pixels non-black, letterboxed mode-select screen, correct orientation and colours |

The last row is the frame the swap hands to the display: it is a real image with
the right content, so the only missing link was the swap call itself.

### Verification

| | Before | After |
|---|---|---|
| Panel | black | image (confirmed on the device) |
| `mugen.log` | identical either way — no diagnostic existed | unchanged, plus the two new guards if either condition fails |
| Frame rate | 1209 frames / 20 s | 797 frames / 15 s (same rate, different run length) |
| System renderer | unaffected | unaffected |

### Why the P1 work did not catch it

Worth stating plainly, because it is the interesting part: P1 was "verified" with
screenshots, and those screenshots were taken through `Gl_Capture`, which copies
the **render target** — the screen-buffer FBO. That path was always correct. The
presenter's output, the window framebuffer, was never looked at, so a missing
`SwapBuffers` could not show up in any check that was run. A green pixel
comparison proved the renderer and said nothing about the screen.

The dump of the window framebuffer above is what that verification should have
done in the first place, and it is what caught this.

## Reproducing

Build (arm64 with the chroot's rootfs as sysroot — the chroot itself is not
needed on the host, `qemu-aarch64` via binfmt runs the compiler). The
`build/tmp/arm64-libs` directory holds corrected `libpthread.so`/`libdl.so`
symlinks, because the rootfs's own `lib*.so` links point at absolute `/lib/...`
paths that do not exist from the host side:

```sh
R=~/ubuntu_root_arm64
export QEMU_LD_PREFIX=$R
export PKG_CONFIG_SYSROOT_DIR=$R
export PKG_CONFIG_LIBDIR=$R/usr/lib/aarch64-linux-gnu/pkgconfig:$R/usr/share/pkgconfig
export PKG_CONFIG=$R/usr/bin/pkg-config

make MUGEN_TARGET=linux-arm64 BUILD=build/linux-arm64 \
     CC="$R/usr/bin/gcc --sysroot=$R -B$R/usr/bin -L$PWD/build/tmp/arm64-libs" -j8
make MUGEN_TARGET=linux-arm64 BUILD=build/linux-arm64 \
     CC="$R/usr/bin/gcc --sysroot=$R -B$R/usr/bin -L$PWD/build/tmp/arm64-libs" \
     STRIP=$R/usr/bin/strip install      # also copies data/gles/* into install/
```

On the device, from the game directory:

```sh
./mugen-linux-arm64 -nosound                      # whatever mugen.cfg says
MUGEN_STRICT_RENDERER=1 ./mugen-linux-arm64 -nosound   # refuse any fallback
grep -E 'renderer:|GLES' mugen.log
```

The renderer line reports what was asked for and what actually ran:

```
success...renderer: requested=opengl effective=opengl caps=0x1F    # GLES active
success...renderer: requested=opengl effective=system caps=0x7     # fell back
```

`render mode 2_6` means mode 2 (`RENDER_OPENGL`) with video flags `0x6`
(`VIDEO_FULLSCREEN | VIDEO_HWBLIT`).

## What is verified vs. what is not

Verified on the device:

- the ARM64 binary runs, loads assets, reads the gamepad and shuts down cleanly
  with both the System and the GLES renderer;
- the GLES renderer presents: the window framebuffer holds a correct,
  letterboxed frame and that frame reaches the panel (P3);
- SDL2 on this image creates an **OpenGL ES 3.2** context on Mali-G31, and
  `gladLoadGLES2Loader(SDL_GL_GetProcAddress)` loads the entry points where
  `gladLoadGL()` reported `GLVersion 0.0`;
- the GLES renderer initialises, a match runs at the same frame rate as System,
  and a screenshot of each shows a real, close image (99.6 % non-black, mean
  difference 3.5/255);
- a missing shader set falls back to System with a logged reason, and
  `MUGEN_STRICT_RENDERER` turns that fallback into a failure;
- the P2 segfault, its backtrace, and its disappearance after the fix.

Not verified:

- per-fixture pixel parity between System and GLES (needs a readback harness;
  the same gap RENDERING_PLAN.md §12 lists as OPEN for Android);
- other ARM64 boards or GPUs;
- the 64-bit layout audit AGENTS.md asks for before a 64-bit target is called
  anything more than experimental.
