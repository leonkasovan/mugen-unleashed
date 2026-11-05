# Shader dialects

Shaders used by the renderers. Each directory is one dialect, and every
dialect has the same four files with the same names:

```
indexed.vert  indexed.frag   8-bit palettised sprites
rgba.vert     rgba.frag      32-bit RGBA sprites
```

The **desktop** renderer (`src/glrender.c`) has no directory here on purpose:
it loads the four GLSL 1.20 files that ship with M.U.G.E.N 1.1b1 from the
game installation's `data/`. They are game content, not ours, and are never
copied into this repository.

| Directory | Dialect | Read by | Deployed as |
|---|---|---|---|
| `gles/` | GLSL ES 1.00, `a_position`/`a_texcoord` + `u_mvp` | `src/gles_render.c` | `data/gles/*` next to the game data, staged by `port/android/build.sh` |
| `vita/` | vitaGL dialect, `aPosition`/`aTexCoord`, `gl_ModelViewProjectionMatrix` | `src/glrender.c` | the VPK substitutes them for the game's `data/*.{vert,frag}` at configure time (`port/psvita/CMakeLists.txt`) |

## Contract

- **The marker `////*EB_DEFINES*////` must stay verbatim and appear exactly
  once.** The loaders replace the first occurrence with the `#define …` block
  that selects a variant, so a mention inside a header comment would swallow
  the block; `src/shader_src.h` owns the marker so the desktop and GLES loaders
  cannot disagree on it. It sits in a comment because a dialect may need a
  statement before it (GLSL ES requires `precision` in fragment shaders).
- **Variants are selects, not copies.** `PERSPECTIVE_NONE|STANDARD|EVEN`,
  `INDEX_STANDARD|GEFORCE7|SAFE`, `PALFX_DISABLE*` and `PREMUL` keep their
  desktop meanings in every dialect. Do not fork a file to change a variant.
- **PalFX maths stays byte-for-byte identical to the original.** The
  add/`colorbal`/`invertall` order and the premultiplied variants are what the
  engine's visual output depends on.
- **No desktop-only constructs** in the ES-shaped dialects: no `sampler1D`, no
  `gl_TexCoord`, `gl_MultiTexCoord` or `gl_Vertex`, no `#version 120`. The GLES
  palette is a 256×1 `sampler2D` sampled at `(index_correction(idx), 0.5)`.

`tools/test_gles_shader.sh` (dialect `gles`) and `tools/test_vita_caps.sh`
(dialect `vita`) enforce the structural rules above on the host, without a GPU.
