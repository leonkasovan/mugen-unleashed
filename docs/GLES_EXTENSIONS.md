# GLES extensions on the reference device

What the ARM Mali-G31 in the reference RG351MP (ArkOS) actually advertises,
categorised, with a one-line note on what each one is for. Collected with
`MUGEN_GL_EXT=1` (debug builds), which prints every extension the driver lists
rather than the curated subset `Gles_LogCapabilities` normally reports:

```sh
MUGEN_GL_EXT=1 ./mugen-linux-arm64-debug -nosound      # 93 extensions on this device
```

```
vendor   ARM
renderer Mali-G31
version  OpenGL ES 3.2 v1.r13p0-01rel0 (GLSL ES 3.20)
```

`OpenGL ES 3.2` is the baseline: everything core to ES 3.0-3.2 (ETC2/EAC
compression, instancing, 3D textures, sampler arrays, UBOs, …) needs no extension
and therefore does not appear below. This list is only what comes *on top*.

> **Correction to an earlier claim in `docs/BUILD-LINUX.md`.** That document said
> this device has "no texture compression (no ASTC/S3TC)". That was a false
> negative: the curated probe looked for `EXT_texture_compression_astc_ldr`, and
> Mali advertises the `KHR_` spelling, so it went unnoticed. **ASTC LDR and HDR
> are present** (see *Compression*). S3TC/DXT genuinely is absent.

## Compression

| extension | what it does |
|---|---|
| `GL_OES_compressed_ETC1_RGB8_texture` | ETC1 RGB blocks — 4 bpp, no alpha |
| `GL_OES_compressed_paletted_texture` | paletted (index-colour) compressed blocks |
| `GL_OES_texture_compression_astc` | umbrella for the ASTC family |
| `GL_KHR_texture_compression_astc_ldr` | **ASTC LDR** — 1-8 bpp blocks, the one worth using |
| `GL_KHR_texture_compression_astc_hdr` | ASTC HDR (high dynamic range blocks) |
| `GL_KHR_texture_compression_astc_sliced_3d` | ASTC for 3D-texture slices |
| `GL_EXT_texture_compression_astc_decode_mode` | choose the decode precision (float/half/UNORM) |
| `GL_EXT_texture_compression_astc_decode_mode_rgb9e5` | as above, with RGB9_E5 output |

ETC2/EAC is core in ES 3.0, so it is not listed — it is nevertheless available and
is the safest compressed format to target since every ES 3.0 device has it.

## Texture formats and storage

| extension | what it does |
|---|---|
| `GL_ARM_rgba8` | RGBA8 is a guaranteed renderable/texture format |
| `GL_OES_rgb8_rgba8` | 8-bit RGB/RGBA renderbuffer formats |
| `GL_OES_required_internalformat` | the driver keeps the internal format you ask for (no silent substitution) |
| `GL_EXT_texture_storage` | **immutable textures** (`glTexStorage2D`): allocate once, upload many times |
| `GL_OES_texture_npot` | NPOT textures with mipmaps and repeat wrap |
| `GL_EXT_texture_rg` | two-channel `R8`/`RG8` textures |
| `GL_EXT_texture_format_BGRA8888` | upload BGRA8888 directly, no CPU swizzle |
| `GL_EXT_read_format_bgra` | `glReadPixels` in BGRA (a screenshot without a swizzle pass) |
| `GL_EXT_texture_type_2_10_10_10_REV` | packed 10:10:10:2 format (HDR-ish, 4 bytes/pixel) |
| `GL_EXT_texture_border_clamp` | `CLAMP_TO_BORDER` + border colour |
| `GL_OES_texture_border_clamp` | same, OES spelling |
| `GL_OES_texture_3D` | 3D textures |
| `GL_EXT_texture_cube_map_array` | arrays of cube maps |
| `GL_OES_texture_cube_map_array` | same, OES spelling |
| `GL_EXT_texture_buffer` | buffer textures (sample a buffer object) |
| `GL_OES_texture_buffer` | same, OES spelling |
| `GL_OES_texture_stencil8` | stencil-only textures |

## Depth, stencil and framebuffer

| extension | what it does |
|---|---|
| `GL_OES_depth24` | 24-bit depth renderbuffer |
| `GL_OES_depth_texture` | depth as a sampleable texture |
| `GL_OES_depth_texture_cube_map` | depth cube maps |
| `GL_OES_packed_depth_stencil` | `DEPTH24_STENCIL8` combined format |
| `GL_OES_fbo_render_mipmap` | render into a specific mip level of an FBO attachment |
| `GL_EXT_discard_framebuffer` | **discard** attachments after use — saves the tile writeback on a tiled GPU |
| `GL_EXT_multisampled_render_to_texture` | MSAA resolved implicitly into the texture (no separate resolve) |
| `GL_EXT_color_buffer_float` | render to 16/32-bit float targets |
| `GL_EXT_color_buffer_half_float` | render to half-float targets |
| `GL_OES_surfaceless_context` | a context with no default framebuffer (offscreen-only work) |
| `GL_EXT_draw_buffers_indexed` | per-draw-buffer blend and write masks |
| `GL_OES_draw_buffers_indexed` | same, OES spelling |
| `GL_OES_texture_storage_multisample_2d_array` | multisampled 2D-array texture storage |

## Colour space and gamma

| extension | what it does |
|---|---|
| `GL_EXT_sRGB` | sRGB textures and framebuffers (hardware gamma conversion) |
| `GL_EXT_sRGB_write_control` | switch sRGB encode/decode off for a draw |
| `GL_EXT_texture_sRGB_decode` | decode sRGB at sample time instead of at upload |
| `GL_EXT_texture_sRGB_R8` | sRGB-encoded single-channel texture |
| `GL_EXT_texture_sRGB_RG8` | sRGB-encoded two-channel texture |

## Blending and pixel effects

| extension | what it does |
|---|---|
| `GL_KHR_blend_equation_advanced` | **advanced blend equations** (multiply, screen, overlay…) in fixed-function hardware |
| `GL_KHR_blend_equation_advanced_coherent` | guarantees those blends are coherent with framebuffer reads |
| `GL_EXT_blend_minmax` | `MIN`/`MAX` blend equations |
| `GL_ARM_shader_framebuffer_fetch` | **read the destination colour in a fragment shader** (Mali native) — custom blends without a second pass |
| `GL_ARM_shader_framebuffer_fetch_depth_stencil` | as above, including depth/stencil |
| `GL_EXT_shader_pixel_local_storage` | on-chip per-pixel storage shared between passes (deferred shading without G-buffer traffic) |

## Queries, timing and robustness

| extension | what it does |
|---|---|
| `GL_EXT_occlusion_query_boolean` | "did anything pass the depth test?" queries |
| `GL_EXT_disjoint_timer_query` | **GPU elapsed-time queries** — advertised here, but see below |
| `GL_OES_EGL_sync` | EGL fence sync objects (`eglCreateSync`) for CPU/GPU hand-off |
| `GL_EXT_robustness` | defined behaviour on out-of-range resource access |
| `GL_KHR_robustness` | newer robustness API |
| `GL_KHR_robust_buffer_access_behavior` | bounds-checked buffer access in shaders |

`GL_EXT_disjoint_timer_query` returns sentinel values that never become
measurements on this driver — the probe in `src/gputime.c` is turned off by
default because of it (`docs/BUILD-LINUX.md`, "GPU timing").

## Buffers and vertex submission

| extension | what it does |
|---|---|
| `GL_OES_vertex_array_object` | VAOs (used by the model path and the instanced experiment) |
| `GL_OES_mapbuffer` | map a buffer for direct CPU writes |
| `GL_EXT_buffer_storage` | immutable + persistent buffers (`glBufferStorage`) — stable pointers, no per-frame reallocation |
| `GL_OES_element_index_uint` | 32-bit index buffers |
| `GL_OES_vertex_half_float` | 16-bit half-float vertex attributes (half the attribute bandwidth) |
| `GL_EXT_draw_elements_base_vertex` | `baseVertex` offset for indexed draws |
| `GL_OES_draw_elements_base_vertex` | same, OES spelling |
| `GL_EXT_copy_image` | GPU-side copy between textures/renderbuffers |
| `GL_OES_copy_image` | same, OES spelling |

## Interop, external images and multiview

| extension | what it does |
|---|---|
| `GL_OES_EGL_image` | import an EGLImage as a texture — zero-copy surface sharing |
| `GL_OES_EGL_image_external` | external (e.g. camera/video) textures |
| `GL_OES_EGL_image_external_essl3` | external textures usable from GLSL ES 3.00 |
| `GL_EXT_YUV_target` | sample YUV (video) framebuffers directly |
| `GL_OVR_multiview` | render several views in one pass (VR/stereo) |
| `GL_OVR_multiview2` | multiview with texture arrays |
| `GL_OVR_multiview_multisampled_render_to_texture` | multiview + implicit MSAA resolve |

## Shading (ES 3.1/3.2 feature set)

| extension | what it does |
|---|---|
| `GL_OES_standard_derivatives` | `dFdx`/`dFdy` in ES 2.0-style shaders |
| `GL_EXT_shader_io_blocks` | interface blocks between shader stages |
| `GL_OES_shader_io_blocks` | same, OES spelling |
| `GL_EXT_geometry_shader` | geometry shaders |
| `GL_OES_geometry_shader` | same, OES spelling |
| `GL_EXT_tessellation_shader` | tessellation control/evaluation shaders |
| `GL_OES_tessellation_shader` | same, OES spelling |
| `GL_EXT_gpu_shader5` | 32-bit ints, `textureGather`, extended indexing, FMA-ish ops |
| `GL_OES_gpu_shader5` | same, OES spelling |
| `GL_EXT_primitive_bounding_box` | per-primitive bounding box for tiled drivers |
| `GL_OES_primitive_bounding_box` | same, OES spelling |
| `GL_OES_sample_variables` | `gl_SampleID`, per-sample varyings |
| `GL_OES_sample_shading` | force per-sample fragment shading |
| `GL_OES_shader_multisample_interpolation` | `sample`/`centroid` interpolation qualifiers |
| `GL_OES_shader_image_atomic` | atomics on image units |
| `GL_EXT_shader_non_constant_global_initializers` | initialise globals with non-constant expressions |
| `GL_EXT_shadow_samplers` | comparison samplers for shadow maps |
| `GL_ANDROID_extension_pack_es31a` | bundle: ASTC + ES 3.1 features in one promise |

## Program binaries and diagnostics

| extension | what it does |
|---|---|
| `GL_OES_get_program_binary` | save/load linked programs |
| `GL_ARM_mali_program_binary` | Mali's own program-binary format |
| `GL_ARM_mali_shader_binary` | Mali's shader-binary cache format |
| `GL_KHR_debug` | driver error callbacks and object labels instead of `glGetError` polling |

## What this engine uses, and what is left on the table

Used today: `GL_OES_vertex_array_object` (model path; the instanced experiment),
core NPOT/FBO/instancing from ES 3.0, and `GL_KHR_debug` would be a cheaper
diagnostic than the `glGetError` drains in `src/gles_render.c`.

Notable things this device offers that the renderer does **not** currently use:

- **`GL_KHR_texture_compression_astc_ldr`** — sprite sheets and stage art stay
  RGBA8 in VRAM; ASTC would cut that several-fold. A memory/bandwidth win, not a
  draw-call one.
- **`GL_EXT_texture_storage`** — the streaming sprite path still reallocates on
  upload (`glTexImage2D`); `glTexStorage2D` allocates once.
- **`GL_EXT_discard_framebuffer`** — on a tiled GPU a discarded attachment skips
  its writeback, which is exactly the kind of cost hiding inside `present`.
- **`GL_EXT_buffer_storage`** — the per-quad vertex upload is still
  `glBufferData` per draw; persistent storage would remove the reallocation.

Absent on this device (so not worth designing around): S3TC/DXT compression,
`GL_EXT_map_buffer_range`, `GL_KHR_partial_update` (tile-based partial present),
`GL_EXT_texture_filter_anisotropic`.
