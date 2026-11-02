# B8 verification matrix (PLAN Android 6, IDs T01-T20)

Evidence type per row: HOST (this machine), DEVICE (physical Android hardware),
or OPEN (not yet collected). A row without a cited file is not a pass.

## Host-proven (evidence in this directory)

- [x] T01 bootstrap selection/checksum/offline — `../apk-baseline/` + 23
  `test_build.py` fixtures (offline); real downloads verified during setup.
- [x] T02 clean real build, exact pins — `host-evidence.txt`: JDK 17.0.19,
  SDK 34, build-tools 33.0.1, NDK 27.1.12297006, CMake 3.22.1; APK
  `mugen-unleashed-arm64-v8a.apk` 8037443 bytes, SHA-256 cited inline.
- [x] T03 dependency closure — `host-evidence.txt`: libmain NEEDED is exactly
  libSDL2/mixer/ttf + system GLES/log/android/m/dl/z/c. ELF64 AArch64, one
  SONAME per library, no host/x86/SDL1 archives. API min 21 (NDK platform).
- [x] T04 native entry — `host-evidence.txt`: `T SDL_main` + `T Mugen_Main`
  resolved in libmain.so. Load + early logcat need a device.
- [x] T20 signing/packaging — `host-evidence.txt`: debug-signed (CERT.RSA/SF),
  META-INF present. Release signing identity is OPEN.

## Device-only (OPEN -- needs hardware)

- [ ] T05 System startup/menu/quick VS (visible frames, renderer, dimensions)
- [ ] T06 unsupported GL cfg (clear failure before stub draw path)
- [ ] T07 key press/release/resync on device (ASCII/special/keypad/modifiers)
- [ ] T08 gamepad topology/instance/remaps, two devices, reconnect/removal
- [ ] T09 same-format resize, format change, OOM rollback on device
- [ ] T10 counted presents across init/menu/fight/error (B3 counters in logcat)
- [ ] T11 display/orientation/aspect + touch coordinate conversion
- [ ] T12 scaling/TTF pixel/state/metric comparisons on ARM
- [ ] T13 callback whole-buffer/guard + RWops streaming lifetime on device
- [ ] T14 audible SND + WAV/OGG/MP3 routes, loop/seek/volume/mute/pause
- [ ] T15 first-run interruption + APK update with user-modified cfg
- [ ] T16 background/resume + surface recreation, no stuck input/audio
- [ ] T17 partial init, normal exit, error exit without double-free
- [ ] T18 sustained fixed scenario: frame-time distribution + memory
- [ ] T19 touch/import profile (only if advertised; currently unimplemented)
- [ ] T20 minimum-API install, 4K/16K page-size, release signing identity

## Rule reminders (PLAN 6.1)

- Normalized logs do not prove graphics, audio or input.
- Same-binary controls first to separate RNG/timing before cross-build logs.
- No exact-AI-attract-log demand for uncontrolled nondeterministic runs.
- Any accepted float/glyph difference names fixture, location, channels,
  dependency and reason. No whole-format exemption.
