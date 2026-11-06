# OpenGL/GLES review fixes

The nine concrete correctness findings in the `src/gl*.c` review are addressed
by this change. The fixed-layout `GlTex` and `GlProgram` ABI structs are unchanged;
format/attachment bookkeeping uses private sidecars. `GlesProgram` is private to
the GLES backend, so it now owns its solid-colour uniform location.

| Finding | Implementation | Regression evidence |
|---|---|---|
| GLES presentation skipped engine overlays | `gles_render.c`: call `g_gfxPrePresent` once on the window, with video-coordinate projection; restore the original target, projection and clip. Window scissors scale to drawable pixels. | Mock callback checks target, viewport, HiDPI clipping and restoration. Real GLES smoke checks red overlay pixels above the blue screen texture. |
| Desktop shader failures reported success | `glrender.c`: validate shader/program creation, compile status and link status. Load all variants into a temporary set and publish only after all succeed. Delete partial objects. Read shader files with checked I/O. | Inject compile/link failures, remove a fragment file, and verify failure, cleanup and preservation of a previously linked program. |
| Failed uploads qualified invalid storage for reuse | Both texture backends publish dimensions/format/accounting only after GL accepts the upload. Invalidate the cached format after failure. A NULL data pointer always allocates storage, never performs SubImage. Depth allocation also checks errors before publishing a handle. | Fail first allocation and a later update; retry must use Image, while a subsequent successful update uses SubImage. Check failed depth allocation deletes its temporary handle. |
| Image arithmetic could overflow | `gl_size.h`: checked `size_t` multiplication; reject nonpositive dimensions, overflow and payloads above `INT_MAX`. Used before RGBA conversion, storage, capture and sprite calibration. The cap protects downstream engine APIs that still use `int`, even on 64-bit. | Zero/negative dimensions, 32768-square RGBA, `INT_MAX` dimensions and `SIZE_MAX` bytes-per-pixel, on both pointer widths. |
| Incomplete FBO accepted as a target | `Gl_AttachFramebuffer` returns success/failure; restore attachments and actual binding on failure. `GlVbuf_Bind` propagates failure; `Gfx_SetRenderTarget` rolls back target metadata. GLES present does not force a rejected target back into use. | Inject incomplete FBO and assert old target, dimensions, attachments, viewport and logical/actual binding remain intact. |
| Stale solid-colour location after GLES relink | `gles_draw.c` queries `u_color` into each linked program; solid draws use that location. | Rebuild with a changed location (3 to 7); verify the new location is used. Real GLES smoke repeats rendering after context recreation. |
| Partial initialization leaked; normal teardown left stale names | Cleanup is based on owned handles, not the ready flag. Desktop init unwinds earlier system textures on later failure. GLES live-context deinit deletes programs, VBO, texture/depth/FBO names, then invalidates caches. Context-loss invalidation always clears names without GL deletion. Android video shutdown releases resources before destroying the context. | Fail the second desktop texture creation and a later GLES shader; assert cleanup. Deinitialize with ready=false and live objects; repeat deinit/loss and assert no repeated deletion. |
| Readback/calibration silently accepted failure | GL read errors return NULL. Indexed calibration treats NULL as failure and cleans shader/program handles. Sprite calibration rejects constant black/white frames and checks allocation ownership; fixed-function calibration explicitly enables texture sampling before shaders are loaded. | Failed capture, failed indexed readback and compile, black/white/checkerboard acceptance fixtures. |
| GLES freed retained textures | Honor `GlTex_Delete` refusal when refs > 0; keep the object, registry entry and CPU backing until release is allowed. | refs=2 refuses free without changing ownership; refs=0 frees the backing and unregisters the object. |

The GLES capability probe now requires ES 3.0, matching the existing R8/RED
texture path and Android context request. It no longer accepts an ES2 or desktop
version string as sufficient for this backend.

## Reproduction

The deterministic driver test needs only a C compiler and vendored headers:

```sh
bash tools/test_gl_errors.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash tools/test_gl_errors.sh
```

`CC`, `CFLAGS`, `LDFLAGS`, `OUT` and `RUNNER` support cross/multilib runs.
`MUGEN_TEST_TARGET=win32|win64` uses the existing kit selection. Both executables
print and, when selected, assert their pointer width. Qualification includes this
harness as `GLERR01`.

An optional Linux EGL test uses the real native SDL2/GLES declarations and the
GLES loader, not the mock desktop declarations:

```sh
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
MESA_SHADER_CACHE_DISABLE=true bash tools/test_gles_smoke.sh
```

It needs SDL2, SDL2_mixer, SDL2_ttf and EGL development headers. Their compiler/link
flags can be supplied as `SDL2_CFLAGS`, `EGL_CFLAGS` and `EGL_LIBS` when pkg-config
is unavailable. Exit 77 means an EGL display or ES3 pbuffer/context could not be
created. The pbuffer is 640x480. It verifies actual pixels, target restoration,
program rebuild and resource teardown across two context lifetimes.

## Verification and limits

Verified during this fix:

- Linux x86-64/GCC 13: both deterministic backend tests pass with ASan/UBSan.
  LeakSanitizer was disabled because the execution sandbox cannot expose the
  `/proc` thread information it needs; explicit resource-count assertions run.
- Linux i386 binaries through QEMU user emulation: both backend tests pass,
  with 32-bit layout assertions enabled (`-m32 -malign-double`). This is not a
  Windows runtime test.
- Mesa llvmpipe, OpenGL ES 3.2 (Mesa 25.2.8): real GLES smoke passes, including
  pixel assertions and context recreation. This is software-driver evidence,
  not Android or Vita device evidence.
- Desktop source syntax checks and Android/GLES source syntax checks with native
  SDL2 and vendored GLES declarations pass. The latter is a host compiler check,
  not an NDK APK link.
- Full Linux x86-64 SDL2 engine build and link pass. An offscreen Mesa quick-VS
  run using the original game assets reports `requested=opengl effective=opengl
  caps=0x1F`, loads KFM and completes 684 match frames before the 12-second
  timeout requests shutdown. The log records successful program termination.
  No game-log equivalence or screenshot-parity claim is made from this run.
- Existing MVP, GLES/Vita shader dialect, Vita capability and renderer-policy
  gates pass on the 64-bit host.
- Nine one-at-a-time mutations (drop overlay, ignore compile status, publish
  failed storage, remove size cap, ignore FBO status, use stale uniform,
  gate cleanup on ready, ignore readback error, free retained object) all fail
  the new regression harness. Sources were restored after every mutation.

MSVC, MinGW Windows execution and Android/Vita hardware remain unverified here.
The calibration test still tolerates placement differences among binary images;
it now rejects blank frames but is not a complete checkerboard geometry oracle.
Legacy desktop texture-memory overcounting is retained as documented. Primitive
batching, an expanded shader-variant system and a renderer-context refactor are
separate optimization/refactoring work, not correctness claims in this fix.
