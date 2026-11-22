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

## FPS overlay and streamed textures

The RG351MP's `font/f-4x6.sff` packs all 95 glyphs onto one 678 × 10
indexed page. The engine loads the sibling `font/atlas.f-4x6.sff` when
present. Generate it with `sffcli --atlas font/f-4x6.sff`. The 95 exported
PNGs matched byte for byte on the device.

With batching on, ShowFPS enabled, and the same seeded match, four short
instrumented runs gave the following averages per rendered frame. The overlay
column excludes the separate screen blit.

| FPS font | Streamed texture slots | Frames | Overlay draws | Total draws | Stream cache misses |
|---|---:|---:|---:|---:|---:|
| Original | 8 | 147 | 9.09 | 23.37 | 11.69 |
| Original | 32 | 156 | 2.51 | 14.96 | 1.56 |
| Original | 64 | 158 | 2.25 | 14.54 | 0.80 |
| One atlas page | 8 | 157 | 1.00 | 14.17 | 1.89 |

Use 32 cache slots as a compromise for installations with ordinary bitmap
fonts. It captured most of the measured draw savings from increasing the cache.
For another workload, sweep slot counts with the same scenario and measure both
misses and resident texture bytes. Choose the smallest count that keeps uploads
within the frame budget. A cache slot holds a streamed texture across frames,
while the seven texture pairs limit one instanced draw.

## ARM64 presentation timing

Two counterbalanced 25-second pairs with the same match seed reproduced the
FPS regression. The stage and font fixes reduced GPU submissions from 29.81
to 13.21 per rendered frame. CPU draw time fell from 8.41 to 5.45 ms and
presentation work outside the buffer swap fell from 7.37 to 2.34 ms. Time
inside the swap rose from 12.49 to 21.94 ms. Rendered FPS fell from 14.32
to 13.82 while the game still advanced at about 60 ticks per second.

The device uses SDL 2.30.10 with KMSDRM. SDL reported interval 1 for both
requests even though both setter calls succeeded. A request for VSync off
therefore did not establish an unsynchronized display mode.

| Build | Requested interval | SDL reported | Rendered FPS | CPU draw (ms) | Present (ms) | Swap (ms) |
|---|---:|---:|---:|---:|---:|---:|
| Before | 0 | 1 | 14.12 | 8.37 | 19.88 | 12.78 |
| Before | 1 | 1 | 14.08 | 8.39 | 19.85 | 12.57 |
| Stage and font fixes | 0 | 1 | 13.48 | 5.43 | 24.31 | 21.96 |
| Stage and font fixes | 1 | 1 | 13.52 | 5.51 | 24.27 | 21.92 |

Waiting for rendering with `glFinish` immediately before the swap reduced
time inside swap to 0.64 ms but left presentation at 24.64 ms and FPS at
13.28. A flush immediately before swap also left FPS at 13.56. The swap
timing includes a wait that largely moves earlier with explicit GPU
synchronization. Neither synchronization call improves throughput.

Calling EGL directly with interval 0 succeeded but did not materially change
swap time (21.94 ms). Earlier submission before the screen composite gave
13.80 FPS. Packing the stage into two SFF v3 pages gave 14.28 FPS with
20.70 ms in swap. Splitting the stage batch at element boundaries while
keeping the font atlas and 32-slot cache gave 15.88 FPS and 17.39 ms in
swap. This isolates the regression to combining stage elements on the
tested Mali-G31/KMSDRM stack. The measurements do not identify the
specific shader or driver behavior that makes those draws slower.

The first workaround preserved stage element boundaries on ARM64 Linux GLES
with KMSDRM. Tiles within the same element could still batch.

The final code reproduced the improvement in two 25-second device runs.
`VRetrace=0` gave **15.88 FPS**, 5.98 ms CPU draw, 19.70 ms presentation,
17.38 ms swap and 19.19 draws per rendered frame. `VRetrace=1` gave
**15.64 FPS**, 6.14 ms CPU draw, 19.70 ms presentation, 17.39 ms swap
and 19.18 draws per rendered frame. Both exceeded the original baseline
of about 14.1 FPS. SDL still reported interval 1 for the off request.
The device test used the one-page FPS font atlas in both final runs.

[SDL 2.30.10's KMSDRM swap path](https://github.com/libsdl-org/SDL/blob/release-2.30.10/src/video/kmsdrm/SDL_kmsdrmopengles.c)
waits for the previous page flip and calls EGL to swap the new buffer.
The device timings include both parts of that path.

### Stage texture pairs on Mali-G31

The follow-up used 25 seconds from the first rendered match frame, with
session seed 12345, the one-page FPS font, 32 streamed texture slots, and
`VRetrace=0` (SDL reported 1). Only stage grouping and the indicated stage
asset changed. All runs submitted an average of seven stage instances per
rendered frame.

| Stage policy | Rendered FPS | Stage draws/frame | Total draws/frame | CPU draw (ms) | Swap (ms) |
|---|---:|---:|---:|---:|---:|
| Element boundaries, first control | 15.92 | 7.00 | 19.19 | 6.24 | 17.42 |
| Up to seven pairs | 13.76 | 1.01 | 13.21 | 5.45 | 21.97 |
| Up to two pairs | 15.96 | 4.01 | 16.20 | 5.55 | 17.94 |
| One pair, deferred | 16.08 | 7.00 | 19.18 | 5.67 | 17.39 |
| Up to three pairs | 15.72 | 3.01 | 15.19 | 5.51 | 18.26 |
| Native stage atlas, up to two pairs | 15.96 | 4.01 | 16.19 | 5.61 | 17.89 |
| Native stage atlas, one pair | 16.16 | 7.00 | 19.18 | 5.74 | 17.43 |
| Element boundaries, second control | 15.92 | 7.00 | 19.17 | 6.18 | 17.42 |

Deferring one-pair draws recovered the throughput without element flushes.
The two-page stage atlas still used seven distinct page/palette pairs in
this run. The measurements isolate the regression to wider texture/palette
groups, with their cost showing up at GPU completion. A trial fragment shader
selected the image and palette together through one balanced selector instead
of selecting them separately. Keeping the same seven-pair batches and scene,
it reduced swap from 21.97 to 18.51 ms and recovered FPS from 13.76 to 15.64
at 1.01 stage draws per frame. This points to fragment shader sampler
selection as a major contributor. The trial shader was not adopted.

The [Mali-G31 counter guide](https://arm-software.github.io/libGPUCounters/mali-g31_counter_reference.html)
lists texture cache and shader counters that would distinguish the underlying
costs. The device did not provide reliable GPU timer query results, so the
exact shader or hardware mechanism remains unconfirmed.

The two-pair workaround capped stage draws on the Mali-G31 KMSDRM backend
on ARM64 Linux. Adjacent stage sprites still batched in painter's order.
Other draws retained the seven-pair limit. Both 25-second runs measured
15.84 FPS, 4.01 stage draws and 16.19 total draws per rendered frame.
Average CPU draw time was 5.54 ms and average swap time was 17.92 ms.
The two element-boundary controls each ran at 15.92 FPS, a difference of
two rendered frames per 25-second window.

### Recovering seven-pair stage batching

The paired fragment shader selects the sprite and palette in the same
branch. The matching vertex shader computes the reciprocal texture size,
so indexed fragments multiply their sprite-local texel center by that
reciprocal instead of dividing by the page size for each fragment.
The shader still selects the same local texel and uses the existing
palette lookup, mask, and PalFX logic.

The following 25-second tests use the same seed, stage, font atlas, and
device configuration as the texture-pair sweep.

| Indexed shader and stage limit | Rendered FPS | Stage draws/frame | Total draws/frame | CPU draw (ms) | Swap (ms) |
|---|---:|---:|---:|---:|---:|
| Original shader, two pairs, first control | 15.68 | 4.01 | 16.20 | 5.61 | 17.95 |
| Paired selector, seven pairs | 15.48 | 1.01 | 13.20 | 5.42 | 18.49 |
| Paired selector with vertex reciprocal, seven pairs | 16.24 | 1.01 | 13.19 | 5.43 | 17.49 |
| GLES 3 integer texel fetch, seven pairs | 15.32 | 1.01 | 13.20 | 5.48 | 18.91 |
| Original shader, two pairs, second control | 15.72 | 4.01 | 16.19 | 5.63 | 17.90 |

The engine enables the reciprocal path only when both installed indexed
shaders declare support. Older or mixed installations keep the existing
texture size convention and the Mali-G31 two-pair stage guard. The RGBA
stage guard is unchanged. See [Building and testing ARM64](BUILDING.md)
for the device workflow.

With the integrated engine and shader files, both final runs rendered
412 frames at **16.48 FPS**, with 1.01 stage draws and 13.20 total draws
per rendered frame. Average CPU draw time was 5.38 ms, presentation
19.84 ms, and swap 17.52 ms. The same engine with the older shader files
fell back to 4.01 stage draws, 16.19 total draws, and **15.96 FPS**
(399 rendered frames).

Pixel parity was checked separately with frame skipping disabled. All
30 final-window 640 × 480 RGBA frames at round ticks 60–89 matched the
two-pair baseline byte for byte. The FPS overlay was enabled with the same
fixed text in both captures. Capture and timing hooks were removed after
verification.

### No-swap and internal resolution tests

The benchmark used the paired indexed shaders on the RG351MP with
`MUGEN_SPRITEATLAS=1`, `MUGEN_SPRITEBATCH=1`, the one-page FPS font,
and ShowFPS enabled. The match was GohanZ2 versus VegetaZ2 on
`hdbz_lookout`, with `-nosound -sessionseed 12345`. SDL reported a
59 Hz display mode and swap interval 1. Each condition ran twice,
sampling 25 seconds starting at the first drawn match loop iteration.
The first four conditions ran in forward and reverse order. The two
smaller-target no-swap runs followed that sequence.

All runs kept the 640×480 game coordinates and the actual
640×480 drawable. For the 320×240 test, the engine allocated a smaller
game color and depth target and scaled its viewport and scissor.
Projection, the final screen blit, and presentation overlays retained
their usual sizes. The `glFinish` controls called it after the final
blit and overlay flush and before any swap. The no-swap FPS counts
completed renders with no new frames sent to the panel.

| Game target | Presentation | Rendered FPS, run 1 / run 2 | Mean FPS | Logic (ms) | Draw (ms) | Present (ms) | Finish (ms) | Swap (ms) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 640×480 | Normal swap | 16.12 / 16.08 | 16.10 | 9.82 | 5.35 | 19.80 | — | 17.54 |
| 640×480 | Finish, then swap | 15.88 / 15.92 | 15.90 | 9.82 | 5.48 | 20.05 | 17.14 | 0.63 |
| 640×480 | Finish, no swap | 16.40 / 16.56 | 16.48 | 9.78 | 5.32 | 19.38 | 17.17 | 0.00 |
| 320×240 | Normal swap | 29.56 / 29.36 | 29.46 | 10.03 | 5.16 | 8.18 | — | 5.85 |
| 320×240 | Finish, no swap | 30.56 / 30.84 | 30.70 | 9.99 | 5.11 | 7.71 | 5.45 | 0.00 |

Timing columns average the rendered iterations from both runs. Present
includes the blit, overlays, and any finish or swap wait. Finish is
timed separately for 25 seconds from the first match present call.
The engine continued to run at about 59.5 loop iterations per second
in these runs.

At 640×480, `glFinish` moved roughly 17 ms out of the swap call.
Omitting swap then recovered only 0.38 rendered FPS over the normal
control. The frame was still waiting on completion of submitted work.
The explicit finish serializes completion, so these values measure
CPU wait with this probe and cannot be read as GPU timer results or
as an uncapped throughput ceiling.

With normal presentation, 320×240 increased rendered FPS by
**83%** and reduced presentation by **11.61 ms** per rendered frame.
The median gap between rendered loop starts went from about 63 ms to
33 ms. The 95th percentile dropped from about 80 ms to 47–48 ms.
These are engine timings, with no claim of uniform panel cadence.

The resolution gain persisted without swaps. The completion wait
dropped by **11.72 ms** while the draw span changed by only 0.21 ms.
This places the main measured gain in resolution-sensitive rendering
and framebuffer work. The test does not distinguish fragment shading,
overdraw, and framebuffer bandwidth or resolves. The resolution change
alters pixel sampling and was tested as a temporary performance option,
with no pixel parity claim. The probe was removed after the runs.

### ARM64 RenderScale retest

To enable the smaller game buffer, uncomment `RenderScale = 0.5` under
`[Video]` in `data/mugen.arm64.cfg`. ARM64 Linux GLES defaults to
`1.0` and accepts values from `0.5` through `1.0`. The setting scales
the game color and depth buffers and maps clipping to the smaller
viewport. The game projection, output drawable, and presentation
overlay still use their original dimensions. Screenshots are expanded
to the logical game size.

The same seeded match ran for two 25-second samples per setting in
full, half, half, full order, with the paired shaders, FPS font atlas,
and normal swaps. The device confirmed 640×480 game coordinates and a
640×480 drawable in all four runs. SDL reported swap interval 1.

| RenderScale | Game buffer | Frames in each run | Mean rendered FPS | Logic (ms) | Draw (ms) | Present (ms) | Swap (ms) |
|---|---|---:|---:|---:|---:|---:|---:|
| Default (1.0) | 640×480 | 410 / 413 | 16.46 | 9.70 | 5.29 | 19.73 | 17.49 |
| 0.5 | 320×240 | 747 / 752 | 29.98 | 10.02 | 5.06 | 8.12 | 5.84 |

The config option reproduced the prototype's FPS gain with normal
presentation. Rendered FPS rose by 82% and average swap time fell by
11.65 ms per rendered frame. The loop continued at about 59.5
iterations per second. At the smaller size the game image has lower
sampling detail. Fifteen half-scale captures from match ticks 60–74
were verified as 640×480 RGBA PNGs.

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
