# Measuring renderer performance

`make perf` builds and installs the selected target, then runs the same scenario
with OpenGL and System. Run it on the target machine; a Windows host cannot run
a cross-compiled Linux ARM64 executable.

```sh
make perf PERF_SECONDS=20 PERF_ARGS="-p1 kfm -p2 kfm"
sh tools/measure_perf.sh build/perf/OpenGL.log build/perf/System.log
```

Enable `ShowFPS = 1` in `[Debug]` to record per-second `perf:` samples.
Without those samples the summary uses the exit frame/time totals. The FPS
value measures the paced game loop; it is not a GPU or CPU render-time profile.
Use `drawn%` alongside FPS: a loop reporting 60 FPS but drawing half its frames
only presents about 30 frames per second. Compare the same scene, duration,
resolution, vsync and asset set. A renderer fallback invalidates a comparison;
check `renderer: requested=... effective=...` in both logs.

`tools/run_perf.sh` restores the exact original `mugen.cfg` on normal completion,
engine failure, SIGINT, SIGHUP or SIGTERM. A missing log or an engine failure is
reported as an error; timeout exit 124 is expected. SIGKILL and loss of power
cannot run shell cleanup; the original remains in `PERF_DIR/mugen.cfg.orig`.
Use a separate `PERF_DIR` for independent runs. Benchmark runs should not overlap
against the same installation, because they temporarily select its renderer.
