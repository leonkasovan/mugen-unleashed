# VITA_PROBLEM.md — 3D stages are fill-bound on the PS Vita

Status of the tree at the time of writing: the stage-half target, the
`Stage3DRenderScale` key, the pass-1 timer, the 2D area/`vquad`/camera
instrumentation and the neutral-PalFX composite switch are all committed.
Every diagnostic VPK below was built with `--profile`; the numbers quoted
are per **drawn** frame from `build\tmp\device\*-mugen.log` unless the
`fps:` line's own tick/drawn rate is quoted.

The one hard number to reproduce before trusting anything below: the
960×544-vs-480×272 A/B (`mugen-psvita-profile-480x272.vpk`) moved `present`
~31 ms → 8.5 ms with identical model work on both sides. The delta is the
test; the per-prim argument in section 3 rests on it.

## 1. The problem

3D stages (glTF backgrounds drawn by `Stage3D_Draw`, `src/stage3d.c:498`)
run far below 60 fps on the Vita's native 960×544 panel, while menus, 2D
stages and non-3D content hold 60. Measured on the release build
(`mugen-psvita-release-cache.vpk`, no counters, 6-minute run):

| Stage | fps | Model | `present` |
|---|---|---|---|
| Esaka (`KOFXI_Esaka_1`, 5,540 tris / 173 prims) | 25–26 | full | ~31 ms |
| Shuttle (`KOFXI_Shuttle_1`, 6,169 tris / 73 prims) | 19 | full | ~15 ms |
| Concert (`KOFXI_Concert_1`, ~5,300 tris / 100 prims) | 12 | full | ~180 ms |
| Wheel (`KOFXI_Wheel_1`, 10,173 tris / 89 prims) | 3–6 | full | ~1,000 ms |

The model's **CPU** cost is small and constant: `model pass`
(`BatchProf_ModelPass`, `src/model.c:3380`) is 1.4–3.6 ms and `logic` is
0.2–0.9 ms in every slow frame, while `present` is 15 ms–1 s. The stall is
per-pixel GPU work on the 173-prim Esaka draw — the A/B proves ~22 ms of
the ~31 ms `present` vanishes when only the pixel count changes. The mezzo
is per-prim submission plus fragments: Wheel draws 10,173 tris through 89
prims and costs ~1 s; Esaka draws 5,540 through 173 prims at ~31 ms. Pixel
count sets the scale, prim/fragment count sets the slope.

Two red herrings, both closed with evidence:

- **Demo vs play is not a code path.** `Game_Draw`
  (`src/gameloop.c:449-450`) forces `hud = 0` in demo when
  `fight.bars.display = 0` (shipped `install/data/system.def:414`), so the
  stage front `Bg_Draw(g->stage_bg, 1)` at `src/gameloop.c:553` — the call
  that reaches `Stage3D_Draw(1)` — never runs. Demo draws layer 0 only
  (Esaka: 84 prims; Shuttle: 20 prims / ~2,100 tris) with no HUD. Play
  draws both model layers plus the HUD. That is the whole "demo is
  faster" gap: about half the model and no HUD, not AI
  (`char.c:2259` forces computer control in demo, but `logic` stays under
  1 ms either way).
- **Demo silence is a sibling switch, not a cause.** `matchload.c:730`
  returns before `Bgm_Load` when `demo_fight_playbgm == 0`
  (`install/data/system.def:412`); audio runs on its own thread and the
  release run holds identical fps for six minutes with this constant. The
  fps lever is `fight.bars.display`, which gates the layer-1 draw, not the
  music.

Odd-numbered note on the 2D quota: the launch behaviour draws the model to
the full 720-line-tall game resolution these stages request
(`Setting game resolution 960x720`), i.e. more pixels than the 960×544
panel. That inflates the foreground cost and is what the target in
section 2 removes.

## 2. What has been done

- **Fast-gamma diagnostic (rejected).** Replaced the two `pow(vec3, 2.2)`
  round-trips in the basic model shader
  (`src/shaders/model_basic.frag:15,25`) with `x*x`/`sqrt` in a throwaway
  profile VPK. Result: `present` ~31 ms → ~27 ms, ~10–15%. The gamma pows
  are not the wall; the shader was restored byte-for-byte.
- **Stage-half render target (committed, default on).** `Stage3D_Draw`
  (`src/stage3d.c:572-610`) draws the layer's model into a smaller FBO
  target and composites it at full size. The target is made by
  `Stage3D_EnsureRenderTarget` (`src/stage3d.c:456`), sized by the new
  `[Video] Stage3DRenderScale` key (`src/config.c:427-431`, Vita default
  50, clamped 25–100), kept with its own depth buffer, cleared to
  transparent, and upscaled bilinearly through `Sprite_DrawGL`; it is freed
  when the stage unloads (`src/stage3d.c:312`). PalFX is applied at model
  time and **suppressed** on the composite via `Gl_SetPalFxSuppressed`
  (`src/stage3d.c:600-602`, `src/glrender.c:1368-1370,1462`), whose neutral
  fx is `{ .color = 256, .mul = { 256, 256, 256 } }`
  (`src/glrender.c:1366`) so the stage's own grade is not doubled.
  Characters, effects and HUD still draw at full size; menus and non-3D
  stages never allocate the target.
- **Skip-front diagnostic (rejected as a fix, kept as a tool).** A one-line
  diagnostic that returns early from `Stage3D_Draw(1)` proved the
  foreground layer is roughly half the cost and was then removed from the
  tree; the VPK snapshot remains for re-testing.
- **Wrong theories killed with counters, all committed under
  `MUGEN_DEBUG`:**
  - `BatchProf_Pass1` (`src/batchprof.h:129`,
    `src/glrender.c:1640`, reported at `src/gameloop.c:1274`) times the
    whole `Sprite_DrawGL` pass-1 offscreen block (`Screen_Create`,
    `src/glrender.c:1610`, through `SpriteSet_Free`,
    `src/glrender.c:1639`). It reads **0 calls / 0 us for entire runs** —
    the offscreen path never fires here.
  - `BatchProf_Area` / `BatchProf_VitaQuad` (`src/batchprof.h:133-134`,
    fed at `src/glrender.c:1468,1284,1339`, reported at
    `src/gameloop.c:1278`) plus the `stage:` line
    (`src/gameloop.c:1280-1282`) separate 2D fill, per-quad submission and
    camera state. The 2D menu sections show ~200 quads at ~3 ms batch;
    the slow 3D sections show 3–8 quads with `present` ~1 s.
- **`--profile` build mode (committed).** Release + `MUGEN_DEBUG`
  diagnostics (`port/psvita/build.bat --profile`); it is how every number
  above was taken.
- **A measurement trap documented, not fixed.** In profile VPKs the
  `vquad` counter alternates ~8 ms / ~1,026 ms on byte-identical frames
  (`area` exactly 1,571,400 px both times), and the release build on the
  same device/stages never shows the ~1 s stall. The suspect is the
  instrumentation itself — per-quad `SDL_GetPerformanceCounter` reads
  stalling on the device — so `vquad` readings are distrusted. Only the
  release fps table and the fill A/B are treated as ground truth.

## 3. Next plan

1. **Release VPK of the stage-half target, same demo/play sequence.**
   The only unknown that matters: does half-res model rendering move
   Wheel/Concert/Esaka, and does the known-good Esaka reach ~60? This
   needs a release build (no counters) because the profile VPKs' own
   stalls contaminate the reading.
2. **If play still misses 60: layer-aware scale.** The skip-front run
   says layer 0 alone is demo-fast, so scale (or skip) only layer 1
   further — a second key alongside `Stage3DRenderScale`, leaving the
   background at 50% and taking the foreground to 25% or off — rather
   than softening the whole frame again.
3. **If the full model at half res still stalls: per-prim submission.**
   The Concert-vs-Esaka gap (near-equal tris, 100 vs 173 prims, 180 ms
   vs ~1 s `present`) points at draw-call/fragment overhead inside
   `Model_Draw` (`src/model.c:3276`, pass wrapped by
   `ModelPass_Begin/End` at `src/model.c:3325,3379`). Batching or state
   sorting there is real engine surgery — fidelity risk, needs
   pixel-compare against the reference — so it stays last.
4. **Do not chase:** gamma shader maths (measured ~10–15%), sprite
   pass-1 (measured 0), BGM/audio (constant load, `logic` < 1 ms), AI or
   game code (`logic` < 1 ms in all slow frames), thermals (the release
   run is flat for 6 minutes; the 1 s stalls exist only in profile
   builds).
