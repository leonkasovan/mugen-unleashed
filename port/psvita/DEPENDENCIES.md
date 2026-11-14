# Vita: dependency pinning and GPU ownership (RENDERING_PLAN.md V1)

No-device record of what the Vita build depends on and who owns the GPU.
Everything here is verifiable from the tree, the SDK and a build; nothing
here claims a device result.

## Pinned dependencies

| Dependency | Pin | Where |
|---|---|---|
| VitaSDK | `vitasdk.zip` on the `toolchains-v1.0` release, SHA-256 `12410f2edaedb5de51bfba8a66c4c807dd587ee540311d846544daa556fb4b7d` | `port\psvita\build.bat` (`VITASDK_URL` / `VITASDK_SHA256`), cached in `build/downloads/` |
| vitaGL (vendored) | `9bf3e3f4e55efd1f4a191b179144f7f9a98cd925` | `third_party/vitagl/VENDORED.md` |
| vitaGL (SDK) | whatever `vdpm install vitaGL` put in `$VITASDK/arm-vita-eabi` — shipped in the pinned SDK archive | reported by `cmake --status` line `vitaGL` |
| vitashark / SceShaccCgExt / mathneon | SDK-provided, resolved by `find_library` | `port/psvita/CMakeLists.txt` |
| SDL 1.2 | VitaSDK `vdpm sdl` (no OpenGL video driver) | `CMakeLists.txt` pkg-config check |
| SDL2 / SDL2_ttf / SDL2_mixer | VitaSDK `vdpm` packages | same |
| Lua 5.1.4, libpng 1.2.38, minizip 0.15, libresample 0.1.3 | vendored in `third_party/` | `port/psvita/CMakeLists.txt` |

The VitaSDK archive is pinned the same way `arm64-sysroot.zip` is in
`port\arm64\build.bat`: downloaded once into `build/downloads/vitasdk.zip`,
verified against the SHA-256 on every use, then unpacked (600 MB) into
`C:/vitasdk` and given a `.mugen-vitasdk-<hash>` marker so a later run can tell
this project's installation from one made by hand. The archive already carries
the `vdpm` packages above, which is why a clean machine needs no `vdpm` step;
`build.bat` still verifies the artefacts and installs anything genuinely absent.

The configure step prints the chosen vitaGL under `vitaGL       <value>` so a
build log records which copy was linked.

## Vendored vs SDK vitaGL

`MUGEN_VITA_BUILD_VITAGL=ON` builds `third_party/vitagl` as the `vitagl_vita`
target **and links that target into `mugen`**. This is the V1 requirement: the
vendored copy must end up in the executable, not merely be built.

Before this, the option built the target and the executable still linked the
SDK's `libvitaGL.a` from `MUGEN_VITA_VITAGL_DEPS`, so the vendored build was
dead weight. Now the two branches are exclusive:

- `MUGEN_VITA_BUILD_VITAGL=ON` → `vitagl_vita` + the non-vendored helpers
  (`vitashark`, `SceShaccCgExt`, `mathneon`) looked up in the SDK.
- `MUGEN_VITA_BUILD_VITAGL=OFF` (default) → all four libraries from the SDK,
  which is what SDL 1.2's GL video driver needs.

## GPU / context owner

There must be exactly one owner of GXM, the display and the GL context. The
decision recorded here:

- **System renderer (shipping default):** SDL 1.2's Vita video driver owns
  SceGxm for 2D presentation. No vitaGL init happens, and no GL context is
  created. `virtualgl`/`vitaGL` symbols are not touched at run time.
- **OpenGL renderer (experimental):** SDL 1.2's Vita video driver has no
  `SDL_OPENGL` support (`include/SDL_config_psp2.h` defines no
  `SDL_VIDEO_OPENGL`), so `SDL_SetVideoMode(..., SDL_OPENGL)` cannot succeed
  and `system.c` falls back to System. A GL path therefore needs its own owner
  (either a provider that implements the SDL_GL contract, or a Vita-owned
  `vglInitExtended`/`vglSwapBuffers` with SDL's video driver not taking GXM).
  That is V1's remaining open item and needs hardware to prove.

`src/system.c`'s fallback releases the requested mode before rebuilding
System, and `Video_SetMode`'s Vita branch degrades surface flags rather than
abandoning start-up; both are in-tree and covered by the SDL1 log comparison.

## What each V milestone can be verified without a device

| Milestone | No-device verification |
|---|---|
| V1 | `cmake` configure succeeds with both `MUGEN_VITA_BUILD_VITAGL` values; the `vitaGL` status line names the vendored target when ON |
| V2 | `tools/test_vita_caps.sh` (host): version parsing, OES/ARB NPOT equivalence, FBO-by-version, no ARB_fragment_program requirement |
| V3 | `res/shaders/vita/*` stay GLSL-ES-compatible (sampler2D palette, attribute/varying); structural checks as for GLES |
| V4 | `MugenVita_BlendColor`/`LogicOp` state what degrades, once, in `mugen.log`; capability flag `MugenVita_ConstantAlphaSupported` is 0 |
| V5 | needs the console: strict mode, cold boot, suspend/resume, readback |
