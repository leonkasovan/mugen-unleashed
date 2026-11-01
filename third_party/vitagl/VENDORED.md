# vitaGL (vendored)
# =================
#
# Upstream: **vitaGL** by Rinnegatamante — a GL 1.x/2.1 compatibility layer over
# the PS Vita's SceGxm GPU (fixed-function pipeline, GLSL via the on-console
# shader compiler).
#
#   https://github.com/Rinnegatamante/vitaGL
#
# Pinned commit: `9bf3e3f4e55efd1f4a191b179144f7f9a98cd925` (2026-10-30)
#
# Licence: **LGPL** — `COPYING.LESSER` in `src/`, and `COPYING`. Kept verbatim.
#
# ## Why it is vendored
#
# VitaSDK ships a prebuilt `libvitaGL.a` with **`GL_TEXTURE_1D` compiled out**.
# Its `glTexImage1D` is a stub that returns immediately:
#
# ```
# 00002aa4 <glTexImage1D>:
#     2aa4:	bx	lr
# ```
#
# and vitaGL's own source states the condition (`source/textures.c`):
#
# ```c
# #ifndef HAVE_UNPURE_TEXFORMATS
#     vgl_log("GL_TEXTURE_1D support is disabled. Compile vitaGL with "
#             "UNPURE_TEXFORMATS=1 to enable it.\n");
# ```
#
# mugen-unleashed's renderer binds every palette as a 1D texture
# (`src/glrender.c`, `GL_TEXTURE_1D`), so with the SDK build a paletted sprite
# cannot be drawn at all. The switch is a compile-time flag, so the library has
# to be built here.
#
# ## Flags this build uses
#
# | Flag | Why |
# |---|---|
# | `HAVE_UNPURE_TEXFORMATS` | Makes `glTexImage1D` forward to `glTexImage2D` with height 1, giving 1D textures a real backing. |
# | `HAVE_FFP_SHADER_SUPPORT` | Provides the GLSL 1.20 matrix built-ins (`ffp_bind_names[]`: `gl_ModelViewProjectionMatrix`, `gl_ModelViewMatrix`, `gl_NormalMatrix`). Without them the engine's shaders do not compile. |
# | `HAVE_UNPURE_TEXCOORDS` | Legalises multitexture with unit 0's `GL_TEXTURE_2D` disabled — which is exactly how `Gl_DrawQuadIndexed` draws. |
# | `TEXTURES_SPEEDHACK` | Slightly cheaper texture binds. Verified compatible. |
#
# Deliberately not enabled, with reasons in `port/psvita/cmake/VitaGl.cmake`:
# `PRIMITIVES_SPEEDHACK` (glitches `GL_LINES`/`GL_POINTS`, both used),
# `SAMPLERS_SPEEDHACK` (risks the palette sampler), `NO_TEX_COMBINER`, and the
# draw/buffer/readback flags documented as potentially crashing.
#
# ## What is still missing for the OpenGL renderer
#
# These flags fix the *textures*. They do not fix the *shaders*: vitaGL's GLSL
# translator (`source/utils/glsl_utils.c`, `source/shaders/glsl_translator_hdr.h`)
# provides `texture2D`/`textureCube`/`texture2DProj` and the three matrices, but
# has **no handling for `gl_Vertex`, `gl_MultiTexCoordN` or `gl_TexCoord[N]`**.
# The engine's shaders are written entirely in those built-ins and are fed by
# immediate mode (`glVertex2f`, `glMultiTexCoord2f`), so using them would need
# both a shader rewrite and a way to receive immediate-mode vertex data.
#
# The software renderer remains the shipping path. This vendored library is the
# prerequisite for the OpenGL work, not a working renderer on its own. See
# `port/psvita/README.md`, "The OpenGL renderer".
#
# ## Fetching
#
# The upstream tree is not committed here (it is ~2.4 MB of source). Restore it
# with:
#
# ```sh
# tools/psvita/fetch_vitagl.sh
# ```
#
# which clones the pinned commit into `src/`.
