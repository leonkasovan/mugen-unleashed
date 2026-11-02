# Native Android SDL2 API boundary: host evidence

Date: 2026-11-02 (Asia/Jakarta). Source base: 2ab0dcd.
Scope: native API removal integrated with the upstream B0–B8 commits.
The prior APK evidence in ../release and ../apk-baseline is historical and
is not evidence of an APK built from this change.

## Executed

- Linux x86_64 GCC/CMake 3.28.3; all Android-selected engine and vendored
  translation units built and linked. Android-only android_main.c/JNI/assets
  entry is excluded from the host target and still needs NDK verification.
- Real SDL2 2.30.0, SDL2_mixer 2.8.0, SDL2_ttf 2.22.0; dummy video/audio.
  These are host packages, not the pinned Android production libraries.
- ASan/UBSan enabled. LeakSanitizer disabled because this environment cannot
  read process task information in /proc. No leak-check result is claimed.
- CTest native_sdl2: PASS. DejaVuSans rendered solid and blended glyphs.
- Explicit WAV/OGG/MP3 fixtures: PASS, including direct Sdlmix_Open/Start/
  Pause/Resume/mute/close, real Bgm_Init/Play/Deinit worker, and effect channels.
- Real SDL2 upload/present calls counted with host-only GNU ld --wrap probes:
  three dirty marks make zero uploads before frame end, then exactly one
  upload and present; a clean second boundary makes no additional calls.
- Virtual joystick attachment/removal, instance lookup and held-button release:
  PASS. Native key translation, unknown/bounds cases, repeat, text and focus:
  PASS. Display resize/depth/reset/background and repeated teardown: PASS.
- Deliberate mutations all FAIL as intended: no dirty marking; wrong Up ID;
  dropped blend mode; BGM pointer packed as only 32 bits. Restored originals
  and reran: PASS.
- CMake ELF dependency gate accepts native SDL2 and rejects an injected
  libSDL.so dependency. Compile guards reject MUGEN_SDL2, MUGEN_VITA_GL and
  SDL_SetVideoMode aliases on Android.
- nm scan: no SDL1 entry points, zoomSurface, Mix_QuerySpecEx or desktop
  Sdl2_SetVideoMode exported/imported by the Android host library.
- Linux syntax-only checks: 83 C translation units with SDL1 headers PASS;
  83 with desktop SDL2 headers PASS. These do not replace Windows/Vita builds.
- Existing bootstrap/orchestration suite: 23 tests PASS.

## Reproduce

From repo root with native Linux SDL2/ttf/mixer development packages:

```sh
cmake -S port/android/tests -B build/android-native-tests -DNATIVE_SDL2_SANITIZE=ON
cmake --build build/android-native-tests -j4
ctest --test-dir build/android-native-tests --output-on-failure
python3 port/android/tests/test_build.py
```

CTest writes its WAV under the build directory and removes it. Supply actual
font and music paths to test_native_sdl2 to repeat the extra codec cases.

## Not verified

New arm64 NDK/APK build, Java/JNI loading on Android, physical controllers,
GPU output, audible output, device suspend/resume/audio focus, asset recovery,
performance and end-to-end mugen.log/game-frame comparisons. The production
Android dependency pins differ from the tested host libraries. See PLAN.md
and REVIEW.md for the remaining qualification gates.
