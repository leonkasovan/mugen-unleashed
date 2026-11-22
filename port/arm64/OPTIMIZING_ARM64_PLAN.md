# Optimizing ARM64 sprite rendering

## Goal and baseline

Reduce GPU submissions on the ArkOS RG351MP without changing visible pixels or
M.U.G.E.N draw order. The older atlas and instancing experiments in
[`docs/BUILD-LINUX.md`](../../docs/BUILD-LINUX.md) are historical measurements:
the atlas sampled scaled sprites differently, and instancing lowered draw calls
without a repeatable frame-rate gain. The current SFF v3 path samples indexed
sprites in sprite-local coordinates and uses seven sprite/palette texture pairs
per ordered instanced draw, following the Ikemen GO GLES approach. SFF v3 remains
optional; ordinary non-stretched sprites can use the same queue.

## Current device measurement

Built the release ARM64 GLES target with Arm GNU Toolchain 9.2 and ran the
unmodified game installation at `/roms/ports/hdbz_indigo` on the RG351MP. The
test binary and four GLES shaders were staged temporarily, then removed or
restored. The four SFFs were `fight.sff`, `system.sff`, `fightfx.sff`, and
`hdbz_lookout.sff`. Each run used `-nosound -sessionseed 12345 -p1 GohanZ2 -p2
VegetaZ2 -s hdbz_lookout`, started its 25-second window at `Match loop init`,
and supplied 23 usable one-second `perf:` samples after discarding the initial
`fps=-1` sample. `ShowFPS=1` was already set in the device's platform config.

| Assets | `MUGEN_SPRITEBATCH` | GPU calls/game frame | Triangles/game frame | Displayed FPS |
|---|---:|---:|---:|---:|
| Four v3 atlas siblings | 0 | 11.09 | 22.04 | 13.69 |
| Four v3 atlas siblings | 1 | 7.09 | 23.04 | 14.27 |
| Original SFFs | 0 | 10.96 | 21.78 | 13.57 |
| Original SFFs | 1 | 7.09 | 23.17 | 14.36 |

The on/off atlas pair is a roughly 36% reduction in GPU calls per game frame.
The final atlas-on run includes per-instance source alpha; its draw count stayed
at 7.09. Atlas and original SFFs produce nearly the same call count when the
seven-slot queue is enabled. In this match, ordinary sprite draws had been
interrupting the atlas runs, and batching them accounts for the large gain.
These are single runs, so the small FPS differences are not a speedup claim.
Frame skipping changes the number of rendered frames per game frame; compare
draw counts and FPS together, not draw counts alone.

Five additional alternating on/off pairs used the atlas siblings, the same
25-second match window and seed, and 23 usable samples per run. Frame capture
was disabled. The mean of the five per-run averages was **14.33 FPS and 7.08
calls/game frame** with batching, versus **13.66 FPS and 11.00 calls/game
frame** without it. Mean draw time was 2.08 versus 2.30 ms. Each paired FPS
gain was positive, from 0.59 to 0.72 FPS; the average gain was 0.67 FPS (4.9%).
This confirms a modest speed gain for this match on this device, alongside the
35.6% draw-call reduction. It does not predict other stages or devices.

**Device pixel parity:** a temporary capture hook rendered every match frame
with frame skipping disabled and saved raw 640×480 RGBA at relative match frames
60–119. With the same session seed, all 60 frames matched byte for byte between
batching on and off for the original SFFs and for the four v3 atlas siblings.
All 60 frames also matched between original and atlas SFFs in each batching
mode. The capture hook was removed after the comparison; it is not part of the
shipping engine.

## GPU calls by drawing process

A temporary profiler tagged the match draw sections and carried each tag with
queued sprites to the actual GPU submission. Batches crossing category
boundaries were counted as mixed, without adding flushes. The following is the
mean **per rendered frame** for one 25-second atlas match in each mode (355
rendered frames with batching, 323 without). The profiler was removed after
measurement.

| Process | Batching on | Batching off |
|---|---:|---:|
| Stage background and foreground | 7.00 | 7.01 |
| Fighters and their shadows | 1.68 | 1.80 |
| Projectiles, explods, and afterimages | 1.98 | 2.68 |
| Lifebar, HUD, and combo display | 8.34 | 24.22 |
| Screen presentation and overlays | 10.61 | 12.82 |
| Mixed-category batches | 0.04 | 0.00 |
| **Total** | **29.66** | **48.53** |

Most saved calls are in the lifebar and HUD: about 15.9 fewer per rendered
frame. Stage draws stay near seven because their order and state already limit
batching. Presentation includes the final screen composite and the ShowFPS
overlay. Plain SFFs produced nearly identical stage, fighter, effects, and HUD
counts in the four-run check; the seven-slot queue accounts for the reduction,
whether its texture is a v3 page or an ordinary sprite.

## Implementation and next checks

1. Keep the queue in submission order. Group only adjacent draws with the same
   shader, blend operation and destination alpha, masking, and clip rectangle.
   Flush at render-target changes, texture or palette mutation, uploads, and
   immediate primitives. Assign each unique sprite/palette texture pair a slot
   from 0 to 6; the eighth starts a new draw. Store each instance's rectangle,
   texture dimensions, slot, PalFX terms, and source alpha in its 36-float
   record. Keep special trapezoid/projective quads on the existing path until
   their geometry and UVs can be represented without changing pixels.
2. Run `tools/test_gles_smoke.sh` on a Linux host with EGL development headers.
   It compares ordinary and instanced framebuffer bytes with different
   textures, palettes, PalFX, and source alpha, and checks the seven-slot split.
   This host was only able to cross-compile the ARM64 engine and syntax-check
   the smoke test; the EGL pixel test remains to be run.
3. Extend the completed 60-frame device comparison with focused scenes for
   fractional scaling, flips, palette swaps, overlapping translucency, and
   clip changes. Inspect any mismatch before considering wider batching of
   special quads.
4. Extend the completed five-pair release A/B benchmark to other stages and
   effects. Record displayed FPS, GPU calls per rendered frame, draw time, and
   texture uploads. A call-count reduction alone is not a performance win;
   retain the path only where pixel parity and frame time hold.
5. Recheck desktop GL, software, and VitaGL rendering. Their draw paths do not
   use the GLES seven-slot program, but shared SFF loading and palette changes
   still need regression coverage.

Keep all benchmark artefacts under the repository's `build/tmp/` on both host
and device, and restore the device's shaders, SFF selection, DEF alias, and log
after each run.
