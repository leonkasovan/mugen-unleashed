# Review of the ten commits ending at 06ad1f3

Scope: `sdl2`, `8ec1b35` through `06ad1f3`, reviewed on 2026-11-08.
This report distinguishes defects still present at that head from defects
already corrected by another commit in the same range.

| Commit | Review outcome |
|---|---|
| `06ad1f3` Player/texture ABI accesses | No additional defect found. Player spans remain 0x420/0x158 on i386 and become 0x434/0x164 on x86-64. GlTex uses its actual member. |
| `3c85ac1` Versioned toolchain folders | Separate defaults help, but the build cache still needed the compiler identity, and an explicit toolchain directory still needed safe replacement. Fixed below. |
| `92b71c1` Performance logging/runner | The benchmark changed the live config without an interruption trap. Fixed below. Per-second FPS remains a paced-loop measurement, not a GPU-time profile. |
| `556b6e1` ARM64 cross-build | Found replacement, upload, cache and pkg-config path defects. Fixed below. |
| `e495385` Shared controller policy | No additional engine defect found. Configuration/menu/reload tests pass; a native SDL2/GLES engine links. Physical Windows/handheld pads were not tested. |
| `de25cff` GLES GLU include | Correctly fixes the unwanted GLU dependency. The GLES engine builds without GLU development headers or GL/GLU link libraries. |
| `05f8a9c` FNT v1 mapping | Correct. Production frame helpers write glyph A's x/width into the actual arrays on both ABIs; 32-bit offsets are unchanged. |
| `e6e8ecc` GLES window swap | Correctly fixes the missing Linux swap. New Linux regression verifies a swap on the window FBO; Android still leaves the swap to its frame boundary. |
| `61e3aea` Linux GLES/startup | Missing display/context and loader-state checks remained. Cold video initialization could also lose the ES profile request. Fixed below. |
| `8ec1b35` GL ownership/error handling | Existing regressions pass; expanded them to Linux GLES and rejected display setup. No additional defect attributed to this commit. |

## Active findings and fixes

1. **P1 — Cold video setup can create desktop GL in a GLES build.**
   `sdl2compat.c` set GL attributes before SDL video necessarily existed.
   `SDL_CreateWindow` initialized it lazily, after those requests had failed.
   Reproduced after the host's ignored `SDL_Init(0xFFFF)` failed with
   `Could not initialize UDEV`: GLES loading reported 4.5, then the ES-version
   probe rejected the desktop context. Initialize video before selecting the
   profile, and before forwarding the legacy pre-window attribute calls.
   The real SDL2 regression starts without video, then requires an actual
   `OpenGL ES` version string after setup.

2. **P1 — A rejected GL display still reaches `Gl_Init`.**
   `Video_SetMode` exempted GL modes from its NULL-surface check. Linux GLES
   therefore called the probe even if the window/context/loader failed, when
   GL function pointers could still be NULL. Return `LIBERR_VIDEO_MODE` before
   entering GL. A production-function test rejects the display and asserts
   zero GL calls, then successfully retries System and GLES.

3. **P2 — Failed loader context is reused without requalification.**
   `Sdl2_SetVideoMode` kept `g_sdl2Video.gl` after a failed loader call; a retry
   skipped the loader because the context was non-NULL. Delete and clear the
   refused context. A real SDL2 test rejects the first loader call and requires
   the second attempt to call it again and succeed on the same window.

4. **P1 — Toolchain setup deleted a working compiler before validating its replacement.**
   `install_toolchain` removed the installed bin/lib trees before downloading,
   verifying or extracting the new archive. A failed download could leave no
   working compiler. Prepare a verified replacement beside the destination,
   validate its compiler, then rename with a backup/rollback. Refuse unmanaged
   nonempty directories and unfinished transactions. Tests cover failed
   download, managed success and protection of unmarked installations.
   Also corrected the MD5 fallback: `shasum -a 1` computes SHA-1, not MD5;
   the fallback now preserves the requested algorithm using OpenSSL.

5. **P2 — Switching toolchain versions could reuse the old executable.**
   Both kits use the same cross prefix/output folder. CONFIG_ID recorded only
   backend, target and GLES, so changing PATH from the 9.2 kit to 15.3 did not
   make any object or executable stale. Record the resolved compiler path,
   compiler version and dependency-root path in the existing rebuild stamp.
   Two fake cross compilers verify distinct configuration identities.

6. **P2 — Generated pkg-config files pointed outside the dependency tree.**
   `make_sysroot.sh` copied into `DEST/usr/{include,lib}` but wrote `prefix=DEST`
   and derived `DEST/{include,lib}`. The Makefile's explicit cross flags masked
   the mistake. Generate `prefix=DEST/usr`. A fixture checks that the include
   and library paths encoded by the generated metadata actually exist.

7. **P2 — Upload could overwrite the live executable before checksum verification.**
   The tar stream used the local basename and renamed it only after extraction.
   When DEVICE_BIN matched that basename, extraction already replaced the live
   executable; verification was too late. Put the temporary name in the archive
   itself. A mocked SSH device forces checksum failure and verifies that its
   existing binary remains byte-identical. No actual SSH/device upload was run.

8. **P2 — Interrupted benchmark left the user's renderer changed.**
   `make perf` restored mugen.cfg only after each engine process returned.
   SIGINT/SIGTERM could leave the temporary renderer in place. Move execution
   into `tools/run_perf.sh`, back up once and restore exact bytes from an EXIT
   trap, including interrupt/error exits. Missing logs and unexpected engine
   failures are errors. Tests check normal completion and process-group TERM.

## Verification

- `tools/test_port_build.py`: 9 tests pass, with fake toolchains and a mocked
  device. No real toolchain installation or remote connection is performed.
- `tools/test_gl_errors.sh`: desktop, Android GLES and Linux GLES tests plus
  rejected-video startup pass on x86-64 with ASan/UBSan, and on i386 via QEMU
  with 32-bit ABI assertions active. LeakSanitizer is disabled because of the
  sandbox's `/proc` restrictions; ownership/count assertions remain enabled.
- `tools/test_sdl2_gles_context.sh`: real SDL2/Mesa ES3 context test passes,
  including cold initialization and retry after a rejected loader.
- Existing gamepad configuration suite: LF, CRLF/BOM, mixed devices, missing
  groups and a fresh-process reload pass. This suite exercises the shared
  policy via its Android host configuration, not physical devices.
- Full native Linux x86-64 SDL2/GLES build/link passes without GL/GLU libraries.
  Offscreen Mesa quick-VS with original assets reports requested/effective
  OpenGL and caps 0x1F. The final six-second run records 340 frames, none skipped and
  exactly 340 calls to SDL_GL_SwapWindow. Dummy-video fallback records System,
  361 frames and clean shutdown. Strict fallback exits 1, not SIGSEGV.
- Production FNT-frame helpers and Player span probes pass on both pointer
  widths: i386 offsets/spans preserve the original values.
- Reverting the installer, upload, cross-stamp and metadata fixes individually
  makes their tests fail. Removing the perf restore trap, rejected-loader
  cleanup or cold-video guard also makes the corresponding tests fail.
  The refused-display test was observed failing before its guard was added.

Remaining limits: no MSVC/MinGW execution, NDK APK link, ARM64 cross-compiler run,
physical controller test or Android/PS Vita/handheld GPU qualification was done
here. Toolchain replacement and upload safety are tested with small fixtures.
SIGKILL/power loss cannot execute shell traps; benchmark backups and interrupted
installation transaction directories are deliberately retained for recovery.
