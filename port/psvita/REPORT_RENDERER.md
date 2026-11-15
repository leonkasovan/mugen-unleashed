# Renderer performance on the PS Vita — measurement record

This is the record of the 3D-stage optimisation work on the PS Vita port: what
was measured, what was tried, and what the numbers actually say. Every
conclusion here is tied to a log produced on hardware, and every optimisation
points at the source it touches. Where a result was later found to be an
artefact, that is stated rather than quietly dropped.

The short version: **the 3D stage frame is dominated by `present`, not by model
drawing.** Two optimisations that targeted fragment/shader work gave little or
nothing; one (fast gamma) gives ~15% and is now the Vita default, with
`--no-fast-gamma` kept as the accurate reference. A half-resolution offscreen
model target was measured to be far *worse* than drawing directly and is
disabled. The remaining gap to 60 fps is not yet explained, and the port's own
frame counters disagree in a way that has to be resolved before further
optimisation.

## 1. Method

All numbers come from release builds run on the console, not from a desktop.

- Build: `port\psvita\build.bat`, output `build/psvita-*/mugen-psvita.vpk`,
  installed to `ux0:/app/MUGEN0001`.
- Logs fetched over FTP from `ux0:/data/mugen.log` and
  `ux0:/data/mugen-vita-gl.log`. The log is truncated per run, so it is copied
  off the device before the next install.
- Each run loads the same four KOFXI stages in the same order — Concert, Esaka,
  Shuttle, Wheel — holding each for 13–30 s, then exits through the PS button so
  `Gl_Deinit` runs and the log is flushed.

### Reading the numbers

The `perf:` line (`src/gameloop.c`, `Perf_TakeAverages`) reports CPU wall time
per frame:

| field | meaning |
|---|---|
| `logic` | engine logic |
| `draw` | everything submitted before `Perf_DrawDone` |
| `wait` | frame limiting (`Timer_WaitFrame`) |
| `present` | the presentation phase (`Screen_Present`) |
| `calls`/`tris` | GL draw calls and triangles |

`present` is **not a GPU timestamp**. It is CPU elapsed time around
`Screen_Present` (`src/frame.c`). The blocking operation inside that phase is
not fully isolated: the `vphase` present line times the swap separately, and
logs exist where `present` was large while the swap span itself was short. A
slow GPU still surfaces here as a block, but the phase can also contain waiting
that the swap call did not cause. Do not read `present` as "time spent in
`vglSwapBuffers`".

The `vphase:` lines (added upstream, `src/perf.c` + `port/psvita/src/`) split
the 3D path further: `back`/`front` (the two `Stage3D_Draw` calls), `bind`,
`clear`, `model`, `restore`, `composite`, `direct`, and the present-side
`bind`/`blit`/`restore`/`swap`.

The `fps:` line (`port/psvita/src/vita_fps.c`, `VITA_FPS_WINDOW_SEC = 10`)
reports three different rates:

- `fps` — `Timer_GetFps()`, the engine's own counter.
- `tick` — calls to `Screen_EndFrame` per second (the loop rate).
- `drawn` — those calls where the frame's own `skip_draw` decision was 0, i.e.
  frames presented. The caller now reads that decision before
  `Frame_UpdateSkip` updates it for the next frame (section 7).

**Always use medians for `draw`/`present`, not means.** Early in this work a
single 1,115 ms frame in one sample dragged Wheel's mean `draw` from a 2.9 ms
median to 44.7 ms, and a "3.5× speedup" was nearly reported on that basis. See
section 5.

## 2. Baseline (full size, accurate gamma)

Build `compiled=Nov 15 2026 08:55:11`, `Stage3D: scale=100% … path=direct`.
Four stages, one run.

| Stage | model | `draw` med | `present` med | fps |
|---|---|---|---|---|
| Concert (`KOFXI_Concert_1`) | ~100 prims / ~5,300 tris | 3.2 ms | 39.5 ms | 12.0 |
| Esaka (`KOFXI_Esaka_1`) | 173 prims / 5,540 tris | 3.1 ms | 13.1 ms | ~25 |
| Shuttle (`KOFXI_Shuttle_1`) | 73 prims / 6,169 tris | 1.5 ms | 14.8 ms | ~19 |
| Wheel (`KOFXI_Wheel_1`) | 89 prims / 10,173 tris | 2.9 ms | 13.2 ms | 8–18 |

Esaka and Shuttle unload inside a single 10 s `fps:` window, so the 08:55 run
has no clean sample for them; their fps here is from the 10:15 replication run
of the same code (25.8 and 18.8). The `draw`/`present`/`vphase` columns are all
from the 08:55 run.

Per-phase, same run (`vphase` averages): Concert game 2.9 / back 1.6 / front 0.5
/ direct 2.0; Esaka 2.5 / 1.4 / 0.4 / 1.8; Shuttle 1.2 / 0.5 / 0.2 / 0.6.

Two things are already visible here and drive everything that follows:

1. **`draw` is 1.5–3.2 ms while `present` is 13–40 ms.** The model is not the
   frame.
2. **The stages request a 960×720 game resolution** on a 960×544 panel
   (`Setting game resolution 960x720`), so they are drawn with more pixels than
   the display shows.

### The one clean resolution A/B

An earlier experiment rendered the whole game at 480×272 with the display at
960×544 (`mugen-psvita-profile-480x272.vpk`) and moved `present` from ~31 ms to
~8.5 ms with identical model work. That is the strongest evidence that the cost
scales with pixels. It is a 4× pixel reduction and must not be read as a 4× fps
promise.

## 3. Experiment 1 — half-resolution model target (rejected)

**Goal.** Halve only the 3D stage model's fragments, leaving sprites, effects
and the HUD at full size.

**Implemented** in `src/stage3d.c`:

- `Stage3D_EnsureRenderTarget` / `Stage3D_FreeRenderTarget` own a
  `Screen_Create` target with its own depth buffer.
- `Stage3D_Draw` binds it, clears it transparent, draws the layer, restores the
  previous target and composites with `Sprite_DrawGL`.
- `Stage3DRenderScale` (`src/config.c`) selects the percentage, 25–100.
- `Gl_SetPalFxSuppressed` (`src/glrender.c`) stops the composite applying the
  stage PalFX twice.
- `Model_HasDestinationBlend` (`src/model.c`) keeps nodes with `ADD`/`SUB`/`MUL`
  `trans` on the direct full-size path, because those blend against the
  destination and an offscreen target cannot reproduce them.

**Measured** at 50% on Esaka (`path=half-resolution`, target 480×360):

| phase | 100% direct | 50% half-res |
|---|---|---|
| `game` total | 3.0 ms | **18.3 ms** |
| stage back | 1.7 | 10.1 |
| stage front | 0.5 | 6.8 |
| offscreen `clear` | — | **5.5** |
| `model` | — | 3.8 |
| `composite` | — | **7.4** |
| fps | 24–27 | **3.6–4.5** |

**Conclusion: rejected.** The clear plus the composite alone cost ~13 ms a
frame, more than halving the model's fragments saves, and the model's own draws
got slower inside the target (back 10.1 + front 6.8 vs 2.2 combined). The
default is back to 100 in `src/config.c`, and the path is **hard-gated**: unless
the build defines `MUGEN_VITA_STAGE3D_OFFSCREEN_EXPERIMENTAL`
(`port/psvita/CMakeLists.txt`), a `Stage3DRenderScale` below 100 from any
configuration file is forced back to 100 and logged once. The path is kept only
so it can be re-measured if it is redesigned.

**Failure-path note.** A failed restore leaves `Gfx_SetRenderTarget`'s state
rolled back onto the offscreen target (`src/gfx.c`), so freeing it there would
leave `g_renderTarget` dangling. That was fixed, then hardened upstream:
`Stage3D_FreeRenderTarget` detaches the framebuffer before releasing it, and if
it cannot detach it calls `Error_Fatal`, which is `noreturn` and skips GL
teardown. The normal cleanup path returns as usual; only that exceptional path
terminates the process. A regression test covers the rollback precondition
(`tools/test_gl_errors.c`, `test_failed_restore_detaches`).

## 4. Experiment 2 — skip blending for opaque primitives (rejected)

**Goal.** `Model_Draw` enabled `GL_BLEND` for the whole model pass and
`Model_DrawNode` set a blend equation per primitive, including opaque ones whose
fragments leave the shader with alpha 1. On a tile GPU that is per-fragment work
that cannot change the result.

**Implemented** in `src/model.c`: blending enabled once per pass (off for the
opaque/masked pass, on for the blended/`trans` pass), with the per-node
`Model_SetBlend` call kept only for the blended pass. `ModelPass_End`
(`src/model_pass.c`) restores blend state, so 2D drawing after the model was
unaffected.

**Measured** on the same four stages:

| Stage | median `draw` | `direct` |
|---|---|---|
| Concert | 3.2 → 3.6 | 2.0 → 2.4 |
| Esaka | 2.8 → 3.0 | 1.8 → 2.0 |
| Shuttle | 1.5 → 1.5 | 0.6 → 0.7 |
| Wheel | **2.9 → 3.3** | 44.6 → 12.6 (mean, misleading — see below) |

**Conclusion: reverted.** No steady improvement, and a slight cost on
Esaka/Concert from the extra per-pass state change. Blending was never on the
critical path: Wheel's steady frame is `draw` ≈ 2.6 ms against `present` ≈ 13 ms,
so removing model draw work entirely would not have moved the frame.

**How this was nearly misreported.** The first pass at the comparison used
means and showed Wheel's `draw` falling 44.7 → 13.4 ms and `direct` 43.9 →
12.6 ms. That was one 1,115 ms outlier in the baseline sample. Sorting the raw
samples gave a baseline median of 2.9 ms and a blendfix median of 3.3 ms. A
re-run of the reverted build measured Wheel `game`/`direct` at 3.9/3.3 ms
(median `draw` 2.6 ms), confirming the baseline mean was the artefact. **This is
why section 1 says medians.**

## 5. Experiment 3 — approximate gamma in the model shader (now the default)

**Goal.** The basic model fragment shader (`src/shaders/model_basic.frag`) does
two `pow(vec3, 2.2)` round trips per fragment (texture → linear, then linear →
sRGB). Measure what they cost.

**Implemented.** `MUGEN_MODEL_FAST_GAMMA` (CMake option,
`port\psvita\build.bat --fast-gamma` / `--no-fast-gamma`) injects
`MODEL_FAST_GAMMA` into the shader head (`src/model.c`), which selects
`x*x` / `sqrt` (gamma 2.0) instead of the two pows. The accurate path is
unchanged and remains selectable. `build.bat` now defaults it **on**; the CMake
option itself still defaults to OFF, so a direct `cmake` configure gets the
accurate shader unless it passes `-DMUGEN_MODEL_FAST_GAMMA=ON`.

**Measured** against the accurate build, same stages, same order:

| Stage | `present` med | fps |
|---|---|---|
| Concert | **41.4 → 29.6 ms** (−29%) | 11.5–12.2 → **13.5–13.8** |
| Esaka | 13.1 → 12.8 | 25.8 → **29.0** |
| Shuttle | 14.7 → 14.5 | 18.8 → **21.6** |
| Wheel | 13.6 → 13.3 | mixed |

**Conclusion: worth ~15%, not the path to 60.** The clearest signal is Concert,
the one stage whose `present` sits well above the ~13 ms floor: −29%. On Esaka
and Shuttle `present` barely moves (already at the floor) yet fps still improves
~12–15%, so the shader does contribute. Results for this were consistent across
two independent experiments (the original throwaway VPK and the committed
option).

Wheel shows no improvement and is spike-prone; its raw `draw` samples are
irregular in both builds, so its medians are not a reliable comparison.

This is now the Vita default in `build.bat`: on this hardware the frame rate
matters more than the slight colour shift, and 12–15% is the largest gain any
experiment in this report produced. `--no-fast-gamma` builds the accurate
gamma-2.2 reference for an A/B or for anyone who prefers exact colour.

## 6. What the numbers say about reaching 60 fps

- Best stage is Esaka at **~29 fps** with fast gamma, ~25 fps accurate. 60 fps
  needs roughly another 2× on top of everything tried here.
- `draw` is 1.5–3.5 ms; `present` is 13–40 ms. Shader- and model-side work has
  little headroom left to give.
- The stages draw at a 960×720 game resolution on a 960×544 panel.
- The one measurement that moved the needle a lot was reducing the whole
  render resolution (4× fewer pixels → `present` 31 → 8.5 ms), not reducing
  model fragments.

So the remaining levers, in the order this work would pursue them, are:

1. **Scale the existing game render target**, not the 3D layer only. Candidate
   sizes for a 960×720 target: 720×540 (56% of pixels) and 640×480 (44%). This
   keeps `Stage3DRenderScale = 100` and avoids the extra framebuffer that made
   section 3 fail. Trade-off: characters and HUD lose resolution too.
2. **Resolve the frame accounting** (section 7) before trusting any further fps
   claim.
3. Draw-call/overdraw work inside `Model_Draw` only if measurement later shows
   submission cost matters — on the current numbers it does not.

## 7. Open issue — the counters disagree

This is unresolved and is the most important caveat on everything above.

A Wheel sample reports:

```
fps: 16.2  (tick 47/s, drawn 35/s)
perf: … draw=2.5ms wait=0.0ms present=13.5ms calls=37 tris=2654
```

`draw` + `present` ≈ 16 ms, which is a 60 fps frame. Yet only 14–35 frames a
second are presented, and `Timer_GetFps()` reports 8–18. Either the measured
phases do not cover everything an iteration spends, or the three rates are not
measuring the same thing.

Two caveats apply:

- **The phase means mix drawn and skipped iterations.** `Perf_DrawDone` and
  `Perf_PresentDone` still execute on an iteration that frame-skip dropped, so
  `draw`/`present` are averages over a mixed population, not a clean
  per-presented-frame cost. Work outside the measured phases — event polling,
  input, the timer wait accounting and the skip decision itself — is not
  attributed anywhere.
- **`drawn` was sampled one frame late.** `Vita_FrameLog` used to read
  `skip_draw` *after* `Frame_UpdateSkip`, which sets the **next** frame's state,
  so it counted a decision that had not been drawn yet. `Screen_EndFrame` now
  captures the frame's own decision before updating skip state and passes it to
  `Vita_FrameLog`. Logs taken before that fix have `drawn` off by up to one
  frame per window, which does not explain the size of the disagreement but
  makes the counter an estimate rather than an exact presentation count.

Until the phases are reconciled with the delivered rate, the visible frame rate
cannot be fully explained by `draw` + `present`, and no further optimisation
should be justified by fps alone. The next instrumented experiment belongs in
the skip/present path, not in the fragment shader.

## 8. Reproducing any run here

```bat
:: approximate gamma, stage scale 100 (the current default)
port\psvita\build.bat

:: accurate gamma, stage scale 100 (the reference for an A/B)
port\psvita\build.bat --no-fast-gamma

:: the rejected half-resolution target, for re-measurement
::   the build must opt in, or config.c forces the scale back to 100:
::   cmake -DMUGEN_VITA_STAGE3D_OFFSCREEN_EXPERIMENTAL=ON ...
::   then set [Video] Stage3DRenderScale = 50 in the packed mugen.psvita.cfg
```

Install the VPK, run Concert → Esaka → Shuttle → Wheel in one mode (all demo or
all versus — demo draws fewer primitives and mixing them invalidated early
comparisons), exit with PS → Close Application, then copy
`ux0:/data/mugen.log` and `ux0:/data/mugen-vita-gl.log` off the device.

Compare medians per stage, not means, and treat `drawn/s` as the delivered rate
until section 7 is resolved.

## 9. Commits from this work

- stage-half target, `Stage3DRenderScale`, PalFX suppression, per-layer model
  attribution, and the connected counters (`BatchProf_Pass1`,
  `BatchProf_QuadArea`, `BatchProf_VitaQuad`).
- blend fallback for `ADD`/`SUB`/`MUL`, checked render-target restore and
  abandoned-target handling.
- default scale back to 100, with the A/B recorded in `src/config.c`.
- `MUGEN_MODEL_FAST_GAMMA` / `--fast-gamma`, with `--no-fast-gamma` as the
  accurate reference; `build.bat` now defaults fast gamma on.

The opaque-blend experiment is not in the tree; it was reverted after
measurement (section 4).
