# PS Vita port — issues found, causes, fixes

A record of what went wrong bringing the engine up on the Vita, in the order it
was hit. Each entry is: **symptom → root cause → fix → evidence**. Entries are
written the way they *were diagnosed*, so the dead ends are recorded too; those
were the most expensive part and the next person should not repeat them.

Two things are still open, marked **OPEN** at the end.

The general lesson, stated once rather than in every entry: **almost every fault
presented at a different layer than it lived in.** A memory budget looked like a
graphics bug, a key binding looked like a broken button, and a background scaler
looked like a corrupted palette. Disassembling the relevant shipped binary, or
reading the engine's input plumbing, settled each one; reasoning from API names
never did.

---

## 1. `C1-2609-7` — module refused at load

**Symptom** The console rejects the module before anything runs. No `mugen.log`
is produced.

**Cause** `vita-make-fself -m 131072` — a 128 MiB process budget. The accepted
range is narrow and `vita-make-fself --help` states it:

```
-m : Memory budget for the application in kilobytes.
     (Normal app: 0, System mode app: 0x1000 - 0x12800)
```

That range is the **system-mode** bracket, not a ceiling for a normal app. 128 MiB
is outside it in both readings.

**Fix** Pass `0` (or omit `-m` entirely), which is what the VitaSDK samples do.
`MUGEN_VITA_MEMSIZE` defaults to `0` and `vita_create_self` then takes no
`MEMSIZE` argument.

**Evidence** `vita-make-fself --help`; the sample CMakeLists in
`$VITASDK/share/gcc-arm-vita-eabi/samples/`.

---

## 2. `C2-12828-1`, no log — abort in C++ static init

**Symptom** Instant crash, `C2-12828-1`, **no `mugen.log` at all**. Two core dumps
had the identical fault address.

**Cause** Rebased by `MODULE_INFO`'s segment base (the module's load address is
randomised per run, so raw addresses never compare), the backtrace was:

```
abort
__gnu_cxx::__verbose_terminate_handler
_GLOBAL__sub_I__ZN10expression4ptokE   <- C++ static ctor
__libc_init_array
_start
```

`expression.o` in `libvitaGL.a` has a C++ static constructor whose first act is
`operator new`. It threw `std::bad_alloc`, nothing caught it, `std::terminate`
ran. So the process aborted **before `main`**, which is why no log existed.

Why it was out of memory: VitaSDK's crt0 sizes the newlib heap from the *weak*
symbol `_newlib_heap_size_user` and **defaults to 128 MiB** when it is undefined,
which is more than the process can allocate. `sceKernelAllocMemBlock` then fails
and crt0 zeroes `_newlib_heap_size`, after which every allocation returns NULL.

**Fix** `port/psvita/src/vita_newlib.c` defines `_newlib_heap_size_user`
(`MUGEN_VITA_HEAP_MIB`, default 64). CMake refuses to configure if the heap does
not fit the budget.

**Evidence** `_init_vita_heap` in the linked image reads the weak symbol and
falls back to `#134217728`; the failure path stores 0 into `_newlib_heap_size`.

**Lesson** A crash *before* `main` cannot leave a log, so "no log" is evidence
about *when*, not just *that*.

---

## 3. "Can't change video mode" that was really an audio problem

**Symptom** Log reached graphics init and failed with
`Library (built in) message: Can't change video mode.` — and then the process
died in `GameVars_Free` → `Match_FreeAssets`, so the error was never shown.

**Cause** Three faults stacked:

1. `SDL_Init(0xFFFF)` initialises SDL 1.2's subsystems in a fixed order —
   TIMER, AUDIO, VIDEO, CDROM, JOYSTICK — and returns `-1` at the first failure.
   Audio comes **before** video, so a failed `SDL_AudioInit` leaves video
   uninitialised, and `SDL_SetVideoMode` then fails as though graphics were at
   fault. The engine ignores `SDL_Init`'s return value (as the original did).
2. `Error_Exit` calls `GameVars_Free`, which assumes a fully initialised game.
   `Match_FreeAssets` dereferences `g_game->palgroups`, allocated only by
   `GameVars_Init` — which runs *after* video and fonts. So the cleanup crashed
   on a NULL and the real message never reached the user.
3. There is no console and no readable stderr, so even a completed error path
   wrote into a void.

**Fix**
- Video and audio are requested separately on the Vita, so audio cannot take
  video down with it.
- `Error_Exit` skips the teardown on the Vita (the process is about to
  `exit(1)` and the OS reclaims it).
- `Error_ShowDialog` calls `SDL_VITA_ShowMessageBox` instead of falling back to
  stderr.
- `Log_Init` falls back to `ux0:data/mugen.log` when the launch directory is not
  writable — that is why *every* diagnostic had been missing.
- `Video_SetMode` degrades instead of giving up: it retries without
  `SDL_DOUBLEBUF`, then without `SDL_HWSURFACE|SDL_HWACCEL`, then without
  `SDL_FULLSCREEN`, logging SDL's reason each time.

**Evidence** The error text was recovered from the *stack image inside the core
dump*: `Error_Exit` builds it there, and the dump includes the stack.

---

## 4. GXM out of memory — the budget bracket

**Symptom** Same "Can't change video mode", but now every retry logged:

```
video: 960x544x32 flags 0xC0000103 refused (gxm init failed: -2141519870)
```

`-2141519870` = `0x805B0002` = `SCE_GXM_ERROR_OUT_OF_MEMORY`.

**Cause** The process budget was passed as `0x12000` (72 MiB, a system-mode
bracket) while the newlib heap took 56 MiB of it, leaving nothing for
`sceGxmInitialize`. SDL's Vita video driver reports a GXM shortfall as a failed
video mode, so a *memory* error looked like a *graphics* error.

**Fix** Same as issue 1: no `-m` at all; the system sizes the process.

**Evidence** Decoding the SDL error code against `psp2/gxm.h`.

---

## 5. Black screen with a perfectly healthy log

**Symptom** The engine initialised fully, reached `Mode select init`, and the
panel stayed black. No crash.

Two separate causes, in sequence:

### 5a. `SDL_HWSURFACE` with no locking

The software renderer writes straight into `surface->pixels` and **never calls
`SDL_LockSurface`** — there is no lock anywhere in `src/`, that is how the
original works. SDL 1.2 requires a hardware surface to be locked before its
pixels are touched, and on the Vita's GceGxm backend a hardware surface keeps
them in a texture that only locking maps out. Every write went to memory nothing
presented.

**Fix** `SDL_SWSURFACE` for the System renderer on this target.

### 5b. The driver returns a double-buffered surface

The diagnostic added in 5a answered it:

```
video: surface 960x544 16bit flags 0xC0000020
```

`0xC0000020` is `SDL_FULLSCREEN|SDL_DOUBLEBUF|SDL_NOFRAME` — **double buffered
even though `SDL_DOUBLEBUF` was never requested**. SDL 1.2 presents a
double-buffered surface only on `SDL_Flip`; `SDL_UpdateRect` just marks the
shadow buffer dirty. The engine flips only when `VIDEO_HWBLIT` is set, which the
System renderer does not set, so every frame went into the back buffer.

**Fix** `Gfx_Present` flips whenever the driver *actually* returns double
buffering, rather than trusting the flags the engine asked for.

**Lesson** Both of these were invisible from the API and obvious from the
driver's own behaviour. `VITA_UpdateRects` in `SDL_vitavideo.o` is an **empty
stub** (`bx lr`) — `SDL_UpdateRect` does nothing at all on this target — and
`VITA_FlipHWSurface` passes no scale. That single fact explains 5b, issue 6 and
issue 7.

---

## 6. Scaling: three attempts, two wrong

The System renderer renders at the game size (480×272) while the panel is fixed
at 960×544, so something has to scale.

| Attempt | Result |
|---|---|
| `SDL_VITA_SetVideoModeScaling` | **Black screen.** It writes a scale into the driver's video struct that only its software-blit path reads; this build never uses that path. |
| `zoomSurface` per frame | **4.4 fps** (from 60). It allocates a fresh panel-sized surface and runs generic nearest-neighbour code every call. |
| `Vita_ScaleToVideo` | Writes pixels directly into the video surface: an exact-2× `uint16` fast path (one 32-bit store per source pixel), a cached-column-map path for other ratios, `zoomSurface` only as a format fallback. |

**Evidence for the fast path** A host test scales 480×272 → 960×544 with
**padded pitches** and checks every source pixel appears as a 2×2 block —
0 mismatches over 130,560 source pixels.

`SDL_MUSTLOCK` is false for this surface (no `HWSURFACE`/`ASYNCBLIT`/`RLEACCEL`),
so writing its pixels directly is legal.

**Measured** 60 fps at 480×272 with no upscale (image too small) → **25-30 fps at
640×480** → 45.8/38.2/36.5/35.0/45.7/45.9 at 480×272 with the upscale, `tick`
60/s throughout.

---

## 7. Joystick bindings were for a 24-button PC pad

**Symptom** In a match, Right jumped, Left crouched, Start walked left, and no
attack button worked.

**Cause** The shipped `mugen.cfg` binds `[P1 Joystick]` to indices **9..24** —
a 24-button PC pad. SDL's Vita driver reports **12** buttons in a different
order. The engine's own defaults are no better: `config.c` seeds
`Jump=1, Crouch=2, Left=3, Right=4`, which on this pad are Circle, Cross, Square
and L.

**Fix** `src/config.c` overwrites both players' bindings with the real indices,
written **by name** rather than positionally — the `KEY_*` enum is
`JUMP, RIGHT, CROUCH, LEFT, A..Z, START`, so a reading-order initializer silently
swaps the directions.

The indices came from disassembling `SDL_sysjoystick.o` and were **confirmed on
hardware** with `port/psvita/input-mapper` (`axes=4 buttons=12 hats=0`; all
indices 0-11 observed):

| idx | button | idx | button |
|---|---|---|---|
| 0 | Triangle | 6 | Down |
| 1 | Circle | 7 | Left |
| 2 | Cross | 8 | Up |
| 3 | Square | 9 | Right |
| 4 | L | 10 | Select |
| 5 | R | 11 | Start |

**Note** The override is in C, not in the packaged config. An attempt to rewrite
the `[P1 Joystick]` block with `string(REGEX REPLACE)` matched far too much
(`[^\[]*` runs across newlines) and truncated 300 lines of the file.

---

## 8. Menus read a different channel than the one being written

**Symptom** Several rounds of "the button does the wrong thing": Circle moved the
cursor, Cross quit the game, L/R confirmed, etc. Every fix produced a new wrong
behaviour.

**Cause** Two independent channels feed screens, and different screens read
different ones:

| Channel | Contents |
|---|---|
| `screen->key` | the keyboard queue. Menus like the title screen act on this. |
| `screen->input` | `Input_FindKey(input_keys, 2, screen->key_code)` — matched against the **built-in keyboard table** in `system.c` (`Jump=SDLK_UP`, ..., `Start=SDLK_RETURN`) |

A key reaches a menu only if it is present in that table. Two of my own changes
broke exactly that:

- Moving `[P1 Keys] Start` from `13` (Return) to `283` (F2) deleted the only
  entry mapping Return → `KEY_START`, so Circle stopped confirming.
- Adding `SDLK_RETURN` to the title screen's info-box toggle made Circle both
  confirm an item *and* toggle the panel, which reads as "Circle is back".

Also: `Escape` is the engine's cancel key, and `Title_Update` turns Escape into
*choose Exit* — so mapping Cross to Escape made Cross quit from the mode select.

**Fix** `Start` is back at `13`. Circle emits `SDLK_SLASH` (the built-in C key,
`0x040`) — see issue 9. Cancel lives on Select. Nothing maps to Escape from a
face button.

**Lesson** `screen->key` and `screen->input` are different channels with
different tables. Changing a key binding without tracing which consumer reads it
is how three consecutive "fixes" made input worse.

---

## 9. Character select: Circle would not confirm

**Symptom** In the character grid, directions and confirm-by-face-button worked,
but Circle did nothing.

**Cause** The two screens accept different masks:

- Title screen: `TITLE_IN_BUTTONS = 0xFF0` — any button bit confirms, so Return's
  `KEY_START` (`0x400`) was fine there.
- Character select: `Charsel_MoveCursor` has explicit branches for `0x010`–`0x200`
  **only**:

```c
if (input & 0x010) result = 0;   /* A */
...
if (input & 0x200) result = 5;   /* Z */
                                 /* no 0x400 case */
```

In the original, Start is a *modifier* there (palettes 7..12 via start+button),
not an accept. So Circle → `Return` → `0x400` → nothing.

**Fix** Circle emits `SDLK_SLASH` (`47`, the built-in C key) → `0x040`, which
both masks accept, and which is also the conventional
Square/Triangle/Circle/Cross = A/B/C/X layout, so Circle is the C attack in a
fight.

**Evidence** The `charsel:` trace showed the cursor moving correctly for all four
directions and `sel=1` for `input=0x20` but `sel=-1` for `input=0x400`.

---

## 10. The OpenGL renderer cannot be used — and why

The software renderer is the shipping path. The GL renderer in `src/glrender.c`
is fully implemented but cannot run on this stack.

**VitaSDK's SDL 1.2 has no OpenGL.** `SDL_vitavideo.o` references no GL context
API and never calls `vglInit`; `SDL_config_vita.h` defines no
`SDL_VIDEO_OPENGL`, so `SDL_SetVideoMode(..., SDL_OPENGL)` cannot succeed.

**The SDL-Vita `sdl12_gl` fork does not help.** Despite the name it is "2D
blitter rewritten on top of vitaGL": `SDL_config_psp2.h` defines no
`SDL_VIDEO_OPENGL` either, and its driver contains no `SDL_OPENGL` handling.
(`src/video/vgl/` in that repo is the FreeBSD libVGL console driver — not
vitaGL.)

**vitaGL itself** has the fixed-function path this engine uses plus FBOs and a
GLSL translator. Of the engine's four `Gl_Init` gates, three are misread and one
is genuinely missing:

| Gate | vitaGL | Verdict |
|---|---|---|
| GL >= 2.0 | `GL_VERSION` = `"OpenGL ES 2.0 VitaGL"` | string is not `N.M`; the probe's parse reads 0 |
| `ARB_texture_non_power_of_two` | `GL_OES_texture_npot` advertised | same capability, different name |
| `EXT_framebuffer_object` | advertised | pass |
| `ARB_fragment_program` | not implemented | the engine never uses it — its shaders are GLSL 1.20 |

**What blocks it anyway** — three things, found by reading vitaGL's source:

1. **`GL_TEXTURE_1D` is compiled out** of VitaSDK's prebuilt `libvitaGL.a`:
   `glTexImage1D` is `bx lr`. The engine binds every palette as a 1D texture.
   `UNPURE_TEXFORMATS=1` fixes this, and the vendored build
   (`third_party/vitagl/`) was verified to produce real code.
2. **`sampler1D` / `texture1D` are not in vitaGL's GLSL shim**, only
   `texture2D`/`textureCube`. Fixable by rewriting `data/indexed.frag` to sample
   a 2D texture at `vec2(idx, 0.5)`.
3. **`gl_TexCoord`, `gl_MultiTexCoord0` and `gl_Vertex` are not handled by the
   translator.** The engine's shaders are written entirely in those built-ins.
   And `glEnd` in `ffp.c` calls `sceGxmDraw` directly with vitaGL's own
   fixed-function shaders, with **no reference to a bound program** — so
   immediate mode ignores `glUseProgram` outright. Using shaders therefore needs
   the renderer to draw through VBOs and `glVertexAttribPointer`, which is a
   rewrite of `Gl_DrawQuadIndexed`/`Gl_DrawQuadRgba`, not a shader edit.

The shaders were rewritten to vitaGL-compatible GLSL anyway (see
`port/psvita/shaders/`, PalFX maths byte-identical), shipped in the VPK, because
they are a prerequisite. They are not sufficient.

**Flags selected** (all justified from vitaGL's source in
`port/psvita/cmake/VitaGl.cmake`): `HAVE_UNPURE_TEXFORMATS`,
`HAVE_FFP_SHADER_SUPPORT` (provides the three matrix built-ins) and
`HAVE_UNPURE_TEXCOORDS` (multitexture with unit 0's `GL_TEXTURE_2D` disabled —
which is exactly how `Gl_DrawQuadIndexed` draws). Crash-prone speedflags such as
`PRIMITIVES_SPEEDHACK` (glitches `GL_LINES`/`GL_POINTS`, both used) are avoided.

---

## 11. Frame-rate measurement, not guesswork

The engine can show a counter (`Game_DrawFps`) but only via the console's `fps`
command, and the console opens on a backquote key — unreachable on a console with
no keyboard. The source comment on `Game_DrawFps` named a `[Debug] ShowFPS`
option that was **never read anywhere**.

**Fix** `config.c` reads `[Debug] ShowFPS`; the packed config sets it. And
`port/psvita/src/vita_fps.c` writes a window every 10 s:

```
fps: 45.9  (tick 57/s, drawn 45/s)
  per frame: scale 3.2 ms, flip 8.7 ms
```

`tick` is `Screen_EndFrame` calls (game logic rate), `drawn` frames actually
presented. `drawn < tick` is the engine rate-limiting itself via
`Frame_UpdateSkip`, not a port fault. The `per frame` split decides whether to
optimise the copy or the driver's present.

---

## 12. Packed `mugen.cfg` — and patches that silently vanish

The game tree is never modified; patches are applied to a copy that is packed
into the VPK. Five values change:

| Key | Pristine | Packed |
|---|---|---|
| `GameWidth` / `GameHeight` | 1280 / 720 | 480 / 272 |
| `FirstRun` | 1 | 0 |
| `ShowFPS` | — | 1 |
| `RenderMode` | OpenGL | system |
| `FullScreen` | 0 | 1 |

**Two patches were silently lost during development** — an edit to the patching
block removed the `FirstRun` substitution, and the build happily shipped
`FirstRun = 1`; an earlier `ShowFPS` pattern matched a *comment*
(`;If Debug = 1, this will be ignored.`) and produced
`ShowFPS = 1, this will be ignored.`

**Fix** After substitution the block now **asserts** the expected values are
present and fails the configure with the missing value named. A wrong config is
otherwise invisible until it reaches the console.

---

## 13. `build/psvita/input-mapper` — proving the mapping instead of guessing

A small standalone SDL 1.2 app in `port/psvita/input-mapper/` (source in
`port/psvita/src/`) that displays button indices, hats and axes, with an embedded
5×7 font so it needs no game data, plus a text log at
`ux0:data/sdl-input-mapper.log`.

It was written because guessing SDL's Vita button order produced an unusable
menu. Its own log confirmed the driver's report exactly
(`axes=4 buttons=12 hats=0`, all indices 0-11), which is what made the
`joy_keys` fix (issue 7) safe to write.

---

## OPEN — crash in the character preview

**Symptom** Selecting a character, then the standing-animation preview, faults.
Eight core dumps, no consistent signature at first.

**What the dumps established.** Rebasing every dump by `MODULE_INFO`'s segment
base (mandatory — the load address is randomised per run) and reading the
registers gives a moving target:

| Fault site | Register evidence |
|---|---|
| `Soft_Put` via `Soft_Scale` (6 dumps) | destination `~0x818b…`, ~96 MB past the module |
| `Font_DrawLine` → `PalFx_Init` | `r1 = 0x8199` — `g_palFxCur`, not a pointer |
| `_write_r` → `sceIoWrite` (2 dumps) | writing 59 bytes, plausible fd and buffer |

**What has been ruled out**, with evidence:

- **Draw parameters** — the animation trace logged every element transition
  through the preview: all scales `1.000x1.000`, zero `SUSPICIOUS` lines.
- **The animation state machine** — the trace ran 970 lines across the character
  select, budget never exhausted, no `ChangeAnim … MISSING`.
- **A stale render target** — downgraded to **not proven**. The one-shot `soft:
  skipping scaled draw into a stale target` diagnostic never fired, but that
  only shows the check did not trigger: it validates `w/h/pitch` ranges, not
  whether the object is still alive, and a freed target can retain plausible
  values. The identical-size path also diverts to `Soft_BlitVariant` before
  the check. Lifetime (allocation, free, pointer swap) still needs tracing
alongside the global-corruption checks below.
- **stdout/stderr** — redirected to `ux0:data/mugen-std*.txt`; both stayed empty,
  so that path is not being used.

**Current hypothesis, not yet proven.** The faults do not share a code path but
they *do* share a shape: each reads a **global** and finds nonsense — a
`PalFx*`, a sprite-set pointer, a `FILE*`. `g_palFxCur == 0x8199` is the clearest
evidence: something wrote a small integer into a global. A single out-of-bounds
write corrupting adjacent globals would explain why the crash site moves and why
every point-of-use guard missed. A candidate of the right shape sits in the log:

```
Char kfm720.def (1) request pal 4 3F (3F) -> reserved 4 (2F)
```

— palette reservation during character load, a fixed-size table indexed by slot.

**Guards currently in place** (defensive, none of which is a fix):

- `Soft_Scale` and `Soft_Rotate` reject non-finite parameters. `Soft_Rotate`
  previously had no such check despite the claim; the guard was added alongside
  this correction.
- `Soft_Scale` bounds each row against the target surface and sanity-checks the
  surface's own fields, logging once if it looks stale.
- `Soft_Scale` checks the palette for NULL — the check `softrender.c`'s own
  header says is missing.
- `Font_DrawLine` rejects an impossible `g_palFxCur`, logging once.

**Next step.** Put canaries around the globals the crashes read
(`g_palFxCur`, `g_log`, `g_renderTarget`) and check them after the
character-load/palette-reserve path, logging the first one that breaks. That
identifies the overwriting code by elimination instead of guessing which pointer
went stale.

**Honest assessment.** This has taken many rounds and is not fixed. The
intermediate guards were each plausible and each ineffective, which cost more
time than the original bug. What eventually helped was refusing to reason from
API names: disassembling the driver, reading vitaGL's source, and putting the
*decision values* in the log.

## OPEN — presentation cost

45 fps at 480×272 with the upscale. That is ~22.22 ms per frame against a
16.67 ms budget at 60 Hz: a **5.56 ms** gap, not ~20 ms as previously written.
`tick 60/s` alone does not locate the bottleneck either: the engine can hold
its tick while skipping rendering through `Frame_UpdateSkip`, so a steady
tick with a low frame count says nothing about where the missing
milliseconds go.

The `per frame: scale … ms, flip … ms` line from a match is the measurement that
decides whether to optimise the copy or accept a smaller render size; that
data has not been collected from a match yet. When it is, report logic,
raster/upload, present/wait separately with medians and frame-time
distributions -- not a single FPS number, and not an inferred deficit.
