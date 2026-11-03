# B8 verification matrix (PLAN Android 6, IDs T01-T20)

Evidence type per row: HOST (this machine), DEVICE (physical Android hardware),
or OPEN (not yet collected). A row without a cited file is not a pass.

**Artifact under test:** `build/android/mugen-unleashed-arm64-v8a.apk`,
sha256 `2d0ee99eb2b21c47a9dca81b24eda2ba8c51e385e018ea22e83a176c21cc31a4`,
8034618 bytes. All four host rows below cite this one APK; the `libmain.so`
hash that `host-evidence.txt` and `native-closure.txt` both record is
`6d10ef47b4a3bf1ede8724fea9d831aaf731a8254f9a388daa2ba8e95c2756d5`.

An earlier revision of `host-evidence.txt` cited `54ccf452...` (8037443 bytes)
while `native-closure.txt` and `signature.txt` cited `2d0ee99e...`, so the pins,
closure, entry and signature rows described two different builds. That file has
been re-derived from `2d0ee99e...`; any `54ccf452...` figures quoted elsewhere
are historical and no longer back a row.

## Host-proven (evidence in this directory)

- [x] T01 bootstrap selection/checksum/offline — `../apk-baseline/` + 23
  `test_build.py` fixtures (offline); real downloads verified during setup.
- [x] T02 clean real build, exact pins — `host-evidence.txt`: JDK 17.0.19,
  SDK 34, build-tools 33.0.1, NDK 27.1.12297006, CMake 3.22.1; APK
  `mugen-unleashed-arm64-v8a.apk` 8034618 bytes, sha256 `2d0ee99e...` cited in
  that file, in `native-closure.txt` and in `signature.txt`.
- [x] T03 dependency closure — `host-evidence.txt`: libmain NEEDED is exactly
  libSDL2/mixer/ttf + log/android/m/dl/z/c. ELF64 AArch64, one SONAME per
  library, no host/x86/SDL1 archives. libmain has no direct
  libGLESv1_CM/libGLESv2 NEEDED entry, unlike the superseded `54ccf452...`
  build; GLES is reached transitively, `libmain -> libSDL2 ->
  libGLESv1_CM/libGLESv2`, as `native-closure.txt` records for libSDL2.so.
  libandroid.so is not the GLES path.
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
