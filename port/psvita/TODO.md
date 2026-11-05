# PS Vita — remaining work

Moved out of the session todo list so the open Vita items live with the port
they belong to. Source of truth for the milestones is
[RENDERING_PLAN.md §11](../../RENDERING_PLAN.md); this file tracks what is
actually outstanding and what has already been done without a console.

Nothing here is a device result unless it says so. Everything marked
"no-device" was verified on the host (builds, unit tests, structural checks).

## What is already done (no-device)

| Area | Commit | Evidence |
|---|---|---|
| Vendored vitaGL is linked into the executable, not merely built | `ffd5980` | `MUGEN_VITA_BUILD_VITAGL=ON` links `vitagl_vita`; dependency pins and the GPU-owner decision recorded in [DEPENDENCIES.md](DEPENDENCIES.md) |
| Capability resolution as testable logic | `ffd5980` | `port/psvita/src/vita_caps.c` + `tools/test_vita_caps.sh`: ES version parsing, NPOT via `GL_OES_texture_npot` **or** the ARB spelling, FBO by version, no `ARB_fragment_program` requirement |
| `ARB_fragment_program` gate dropped for the Vita | `ffd5980` | `src/glrender.c` skips it under `MUGEN_VITA_GL`; the real qualification is shader compile+link and FBO completeness |
| FBO completeness probed with a real attachment | `ffd5980` | `vita_gl_shim.c` attaches a 16x16 colour texture before `glCheckFramebufferStatus`; failure stops `Gl_Init` |
| Blending gaps stated instead of hidden | `ffd5980` | `MugenVita_BlendColor` records the value, exposes `MugenVita_ConstantAlphaSupported` (0 on vitaGL) and logs once what degrades; `MugenVita_LogicOp` declares unsupported once |
| Shader dialect gated structurally | `ffd5980` | `tools/test_vita_caps.sh` runs `tools/test_gles_shader.c` in `vita` mode over `port/psvita/shaders/` |
| Vita's own `mugen.cfg` | `f76426d` | `port/psvita/mugen.cfg`: `RenderMode = System`, 640x480 render size, `FullScreen = 1`, `[P<n> Joystick]` in the driver's engine codes; packed via `MUGEN_VITA_CFG` |

## V1 — Dependencies, link and a single GPU owner

- [ ] Prove on hardware that only one component owns GXM, for **both** branches
      (SDL2-vitaGL provider, and Vita-owned `vglInitExtended`/`vglSwapBuffers`).
- [ ] Prove a GL → System fallback releases the old owner before System renders;
      a failed teardown must not be reported as a successful recovery.
- [ ] Confirm the vendored build actually initialises `vglInitExtended` on the
      device (the link is proven; the run is not).
- [ ] Record the final link map and confirm no host library leaks into the
      binary (the pin list exists in [DEPENDENCIES.md](DEPENDENCIES.md)).

## V2 — Functional capability probe

- [ ] Real context creation, clear + colour capture.
- [ ] A custom programmed triangle drawn and read back.
- [ ] A 2D texture sampled, and the 256x1 palette sampled at 0/1/254/255 and
      every entry.
- [ ] FBO render target with an attachment, clipping, and swap — each with
      independent pixel readback.
- [ ] A context that creates but cannot compile/link its shaders or complete its
      FBO must **not** count as "OpenGL available".

## V3 — Vertex submission and shaders

- [ ] Replace `glBegin`/`glEnd` in the programmable Vita draw path with explicit
      vertex buffers/attributes (the ES dialect shaders already expect
      `a_position`/`a_texcoord` + `u_mvp`).
- [ ] Carry position and texcoord as four components so the projective Q is not
      dropped.
- [ ] Audit the nine program variants, PalFX, masking, premultiplication, the
      alpha multiplier, compile/link logs, and `location == -1` /
      attribute-optimisation cases.
- [ ] Port `gldetect.c`: embedded calibration shaders, primitives, draw setup and
      readback are still on the desktop immediate-mode API.

## V4 — Blending, targets and complementary features

- [ ] Constant-destination-alpha needs a two-pass composite: add an intermediate
      target / ping-pong. Never read the texture that is the current draw
      attachment.
- [ ] Derive blend state from the shader and engine equations; do not assume RGB
      and alpha always share a factor or that every op can be additive.
- [ ] Pixel parity against the desktop renderer for every blend op, on hardware.
- [ ] Finish render targets, viewport/scissor, texture/FBO orientation, depth,
      capture/screenshot, font, primitives and the final screen quad.
- [ ] `OpenGLScreen` on the Vita needs its own screen-upload path: no CPU panel
      upscale followed by a GPU upscale.
- [ ] Logic/XOR: implement or declare unsupported with a correct fallback. An
      empty function is not accepted as rendering parity.

## V5 — Integration and device qualification

- [ ] `RenderMode=OpenGL` with strict ON reports the programmable VitaGL
      renderer, correct shader draws and passing pixel readback; a System
      fallback fails the test.
- [ ] Original and generated `mugen.cfg`, shader hashes inside the VPK, install
      update, cold boot, suspend/resume, resource pressure, app exit.
- [ ] `SDL2_ttf`/scaler, RWops, audio, input and teardown through the related
      Vita-plan gates (a startup/GPU-subsystem change can affect audio and
      input).
- [ ] Separate measurements for CPU draw, upload, GPU/present/wait, FPS and peak
      memory on an identical configuration. Do not change the resolution and do
      not claim 60 FPS without data.
- [ ] Default SDL1/System stays until every claimed gate passes.

## How to close these

Each item needs the console. The harness side already exists for what can be
checked without one:

```sh
tools/test_vita_caps.sh          # capability logic + shader dialect (host)
tools/test_qualify.sh --target win32   # reports VT01-device / VT02-device as OPEN
```

A device run should produce, under `build/render-validation/<sha>/psvita/`:
the build metadata, the packed cfg, the shader hashes, the pixel readback for
each V2 fixture, and the V3/V4 capture comparisons. Record the console model and
firmware with the result; "it built" is not a device result.

## Known gaps worth stating plainly

- Stock VitaSDK SDL 1.2 has no `SDL_VIDEO_OPENGL`, so a GL context needs either
  a patched SDL or a Vita-owned GL path. That decision is V1's open item.
- vitaGL has no `glBlendColor`, so `BLEND_ALPHA` sprites composite additively
  there. This is logged, not hidden, and it is why V4's ping-pong work matters.
- vitaGL advertises `GL_OES_texture_npot`, not the ARB spelling; the probe
  accepts either, but a driver that advertises neither cannot use this renderer.
