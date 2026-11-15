# VITA_PROBLEM.md — 3D stage performance on the PS Vita

This document describes one measured problem, what was tried, and what the
next steps are. Every claim points at a source line. Where a number came
from a build that is no longer in the tree, it says so and is not treated
as reproducible evidence.

**Read this first.** An earlier revision of this file presented the
half-resolution stage target and the demo/play explanation as committed
facts. Neither was true: the target had been discarded and the demo
explanation did not match `Game_Draw`. Both are corrected below, and the
diagnosis is stated at the strength the evidence supports.

## 1. The problem

3D stages — the glTF backgrounds drawn by `Stage3D_Draw`
(`src/stage3d.c`), reached from `Bg_Draw` (`src/bg.c:852`) — run well below
60 fps on the Vita's 960×544 panel, while menus, 2D stages and non-3D
content hold 60.

Measured on the **release** build (`mugen-psvita-release-cache.vpk`, no
diagnostic counters), over a six-minute run that loaded six stages:

| Stage | fps | Model size |
|---|---|---|
| Esaka (`KOFXI_Esaka_1`) | 25–26 | 5,540 tris / 173 prims |
| Shuttle (`KOFXI_Shuttle_1`) | 19 | 6,169 tris / 73 prims |
| Concert (`KOFXI_Concert_1`) | 12 | ~5,300 tris / 100 prims |
| Wheel (`KOFXI_Wheel_1`) | 3–6 | 10,173 tris / 89 prims |

What those numbers do and do not say:

- **Engine logic is cheap.** Engine logic is 0.2–0.9 ms per frame and the
  model's measured submission is 1.4–3.6 ms in every slow frame. That does
  not rule out driver CPU overhead, synchronisation or CPU/GPU waiting —
  only that the engine's own logic and the measured model submission are
  not where the time goes.
- **The cost scales with resolution.** A 480×272 run of the same scene
  with the same model work moved `present` from ~31 ms to ~8.5 ms. That
  A/B is the strongest single datapoint here. It is a 4× pixel reduction
  and must not be read as a 4× frame-rate promise: the result is bounded by
  whatever part of the frame is not model fragments.
- **The bottleneck is not isolated.** `present` is measured on the CPU
  around `Screen_Present` (`src/frame.c:257-304`) via
  `Perf_PresentDone` (`src/perf.c:117-133`). It is not a GPU timestamp:
  a slow GPU operation surfaces as a block in a later synchronisation
  point, and `vglSwapBuffers` is exactly such a point. So "resolution
  sensitive" is established; "fill-rate bound" is *not* — memory
  bandwidth, driver synchronisation and per-prim submission all remain
  candidates.
- Frame-rate and `present` figures in this document come from different
  runs and must not be divided into each other. `fps:` reports the
  averaged seconds; a 5 fps second with a 1,000 ms `present` is a slow
  second, not a per-frame average.

### Closed questions

- **Demo vs play was not a code path.** In demo mode `fight.bars.display
  = 0` (`install/data/system.def:414`) sets `hud = 0`
  (`src/gameloop.c:449-451`). But the stage front is drawn under a
  separate condition, `assert_nofg` (`src/gameloop.c:550-556`), which demo
  does not change — so `Bg_Draw(bg, 1)` and with it `Stage3D_Draw(1)`
  still run in demo. The earlier claim that demo draws layer 0 only was
  wrong. Whether a given screen draws both layers is now **measured**, not
  inferred: `pic model: layer0 … layer1 …` (section 2).
- **Demo silence is unrelated.** `src/matchload.c:730` returns before
  `Bgm_Load` when `demo_fight_playbgm == 0`
  (`install/data/system.def:412`). Audio runs on its own thread; the
  release run holds its per-stage fps for six minutes with that setting
  constant.
- **Shader gamma maths is not the wall.** Replacing the two
  `pow(vec3, 2.2)` round-trips in `src/shaders/model_basic.frag` with
  `x*x`/`sqrt` in a throwaway build moved `present` by roughly 10–15%.
  The shader is unchanged in the tree.

### A measurement hazard

In `--profile` builds (`port/psvita/build.bat --profile` →
`MUGEN_VITA_PROFILE`, `port/psvita/CMakeLists.txt`), counter readings
became unreliable: on byte-identical frames a per-quad timer alternated
between ~8 ms and ~1,026 ms while the release build on the same device
and stages never showed such a stall. The instrumentation itself is the
suspect. Treat `--profile` numbers as advisory; take fps from a release
build.

## 2. What has been done

- **Half-resolution stage target (present, default on, Vita only).**
  `Stage3D_Draw` (`src/stage3d.c`) now draws the model layer into a target
  sized by the new `[Video] Stage3DRenderScale` key
  (`src/config.c`, Vita default 50, clamped 25–100, global in
  `src/globals.c`) and composites it at full size through `Sprite_DrawGL`.
  The target is created by `Stage3D_EnsureRenderTarget`, given its own
  depth buffer by `Screen_Create`, cleared transparent, and freed by
  `Stage3D_FreeRenderTarget` from `Stage3D_Free`. Characters, effects and
  the HUD keep their full size; menus and non-3D stages never allocate it.
  `Stage3DRenderScale = 100` disables the path entirely.
- **No double PalFX.** The stage PalFX is applied when the model is drawn;
  the composite asks for a neutral fx through `Gl_SetPalFxSuppressed`
  (`src/glrender.c`), so the stage's grade is applied once.
- **Destination-blended models keep the full-size path.** A node whose
  `trans` extra is `ADD`, `SUB` or `MUL` blends against whatever is behind
  it (`Model_SetBlend`, `src/model.c`). Behind it in an offscreen target is
  transparent black rather than the stage, so those modes cannot be
  reproduced by compositing a texture. `Model_HasDestinationBlend`
  (`src/model.c`) walks the nodes the layer would draw, using the same
  layer rules as `Model_DrawNode`, and `Stage3D_Draw` falls back to drawing
  that layer directly at full size. Ordinary alpha-blended stages — the
  common case — still use the target.
- **Failure paths are closed.** A target that cannot be bound, or whose
  restoration after the model pass fails, is dropped and the half-resolution
  path is disabled for the rest of the stage
  (`Stage3D_AbandonRenderTarget`). The composite never samples a texture
  that may still be bound for rendering, which would be a framebuffer
  feedback loop.
- **Per-layer model attribution.** `Stage3D_DrawLayers` brackets each
  layer with `BatchProf_ModelLayerBegin/End`, and `Model_Draw`'s
  submissions are attributed through `BatchProf_ModelSubmit`. The log
  line `model: layer0 calls=… tris=… layer1 calls=… tris=…`
  (`src/gameloop.c`) answers "which layers did this screen draw" with a
  number, as the review asked.
- **Counters connected and tested.** `BatchProf_Pass1` wraps the
  `Sprite_DrawGL` pass-1 offscreen block (`Screen_Create` → draw →
  `SpriteSet_Free`); `BatchProf_VitaQuad` brackets both
  `VitaGl_DrawQuad` call sites; `BatchProf_QuadArea` sums the quads'
  **nominal scaled area** — sprite size times scale factors, with no
  clipping, rotation, perspective or overdraw — so it measures 2D demand,
  not executed fragments. All of these were previously defined with no call
  sites, which made earlier zero readings meaningless.
  `tools/test_batchprof.c` covers the layer arithmetic, including an
  out-of-range layer, and passes.
- **`--profile` build mode.** Release optimisation plus `MUGEN_DEBUG`
  diagnostics (`port/psvita/build.bat --profile`).

## 3. Next plan

1. **Release VPK of the current tree, same four stages, demo and play.**
   This is the first measurement of the half-resolution target that is not
   contaminated by diagnostic counters. Record fps and `present` per stage.
   If Esaka's ~25 fps becomes ~60 and Wheel's 3–6 improves by roughly the
   4× its pixel reduction implies, resolution scaling is confirmed and the
   default stays 50.
2. **Profile build of the same tree for layer attribution only.** Read
   `model: layer0 … layer1 …` in demo and play on one stage. This replaces
   the discarded claim with a measurement, and decides whether a
   layer-specific scale is worth having.
3. **If half resolution is not enough, establish where `present` blocks.**
   There is no GPU timer on this target today: `src/gputime.c` is compiled
   only under `MUGEN_GLES` (`src/gputime.c:10`), and the Vita build defines
   `MUGEN_VITA_GL`, so `GpuTime_TakeAverages` does not exist in a Vita
   link. Using it is not an available option until a Vita-specific timer
   path is written. Until then, bracket individual GL calls with the CPU
   clock to find which call blocks; that locates the blocking call but
   cannot by itself measure GPU execution time. Only after that should
   draw-call batching inside `Model_Draw` be considered, since it is real
   engine surgery with a fidelity risk.
4. **Do not re-chase:** shader gamma maths (measured ~10–15%), the sprite
   pass-1 path (counter now connected; no evidence it dominates), BGM
   (`demo_fight_playbgm`), AI (`logic` < 1 ms in every slow frame), or
   thermals (the release run is flat for six minutes).
