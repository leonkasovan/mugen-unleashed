# Vita validation artefacts (PLAN.md 5.4)

This directory holds Vita hardware evidence. Fill it on the console; do not
fabricate entries from host runs.

```
port/psvita/validation/<engine-sha>/<profile>/
  manifest.json      engine/SDK/provider/vitaGL/firmware pins (M0)
  configure.log      full CMake configure output (M1)
  link.map           final link map, one SDL + one vitaGL (M1)
  packed-config.cfg  mugen.cfg as packed into the VPK (M1/M8)
  shader-hashes.txt  sha256 of every shader inside the VPK (M6)
  mugen.log          on-device log for the scenario (M0/M8)
  frames/            captured framebuffer images (M5/M6/M8)
  audio/             captured/loopback audio (M4)
  frame-times.csv    per-frame timings, not averages (M8/P01)
  memory.csv         heap/GXM allocations across scenes (M0/M8)
  core-dumps/        unstripped ELF + rebased addresses (M0/M7)
  results.md         per-scenario verdict against the matrix
```

Naming: `<profile>` is e.g. `sdl1-system-baseline`, `sdl2-system`, `sdl2-gl`.
Record console model, controller topology, firmware, power/clock conditions,
test duration and whether each result is: source inspection, host test,
cross-build, emulator observation, or Vita hardware.
