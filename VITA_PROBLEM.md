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

### Confirmed: the 50% offscreen approach is slower on Vita

Two **Release** VPK runs on the same Esaka stage, 2026-11-15:
`scale100-mugen.log` and `scale50-mugen.log`, both with the same
444/222/222 MHz clocks, model (5,540 triangles/173 primitives), and
960×720 game render size:

| [Video] Stage3DRenderScale | Average *drawn* FPS | Average game rate |
| --- | ---: | ---: |
| 100 (direct) | **24.85** | 59.87 |
| 50 (480×360 offscreen target, upscale to 960×720) | **3.75** | 4.98 |

At 100, `vphase` attributes ~2.1–2.3 ms CPU wall time to the direct
stage draws; `vglSwapBuffers` typically waits ~12–13 ms. At 50, FBO
clearing alone is typically ~5–6 ms, stage compositing ~6–7 ms, and the
`vphase-max` composite sample reaches **~1,024 ms**. The `perf`
`present` span is often ~172–207 ms even when individually measured
`vglSwapBuffers` is ~0.1 ms, so the remaining present-side blocking
operation is not yet isolated. The causal A/B conclusion is limited to
**this offscreen FBO/clear/full-size composite implementation**, not
downscaling in general, and the timings are CPU wait time rather than GPU
execution timestamps.

The Vita offscreen method is now **disabled by default**, including if
an older game config still specifies 50%. The engine and the packed
`port/psvita/mugen.psvita.cfg` both default to 100; experiments below
100 require the opt-in CMake flag
`MUGEN_VITA_STAGE3D_OFFSCREEN_EXPERIMENTAL=ON`.

## 2. What has been done

- **Offscreen stage target (retained but gated off on Vita).**
  `Stage3D_Draw` (`src/stage3d.c`) contains the smaller render target
  and full-size composite path, but the release Vita default is now
  `Stage3DRenderScale = 100` (direct draw). `src/config.c` ignores values
  below 100 on Vita unless built with the non-default CMake option
  `MUGEN_VITA_STAGE3D_OFFSCREEN_EXPERIMENTAL`. The packed overlay
  `port/psvita/mugen.psvita.cfg` explicitly specifies 100 as well.
  The experimental path uses `Screen_Create` with depth, clear,
  `Sprite_DrawGL` composite and `Stage3D_FreeRenderTarget` on unload;
  it must not ship enabled until the blocking behavior is resolved.
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
- **Framebuffer failures are handled without freeing live attachments.**
  If binding the model target fails, the stage disables half-resolution
  rendering and draws directly. If restoration of the original render target
  fails, it is retried once; persistent failure is fatal, because substituting
  the window framebuffer would not restore the engine's screen texture.
  `Stage3D_FreeRenderTarget` verifies that an active model target can be
  detached before freeing it; a failed detach is also fatal. On Vita the
  fatal-error path exits without GL teardown. The composite never samples a
  texture that remains bound for rendering.
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

1. **Ship Vita Release at `Stage3DRenderScale = 100`.** Re-test Esaka
   and the other stages on the same hardware. The 100% Esaka A/B result
   (24.85 drawn fps) is the verified baseline; 50% is a regression, not
   a candidate default. The packed Vita config forces the safe default.
2. **If investigating a faster model pass, isolate the driver stall in an
   experimental build before reenabling offscreen scaling.** The existing
   `vphase` logs already isolate a ~1-second `Stage3D` texture
   composite, but the separate `present` stall outside the timed
   `vglSwapBuffers` call needs its own probes (pre-present callback,
   end-of-present GL clear, frame-boundary adapter). Avoid expensive
   per-primitive timing; a Release build is essential.
3. **Use a design that avoids repeatedly clearing and compositing the
   offscreen stage texture**, or validate a different whole-frame scaling
   strategy first. Do not infer performance from pixel count alone.
4. **Use `--profile` only for attribution** and do not interpret its
   timings as release frame rate. The existing model-layer counters can
   clarify whether front-layer model work is present.
