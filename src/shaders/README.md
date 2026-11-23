# Shaders

Every shader the engine uses lives here and is **embedded into the binary as a C
string literal** for every platform (desktop GL, GLES 3.0, PS Vita). Nothing is
loaded from disk at runtime and no port stages shader files any more.

## Layout

```
model.vert  model.frag  model_basic.vert  model_basic.frag   glTF models (src/model.c)
shadow.vert shadow.frag env.vert env_panorama.frag env_filter.frag
gles/indexed.vert  gles/indexed.frag  gles/rgba.vert  gles/rgba.frag
vita/indexed.vert  vita/indexed.frag  vita/rgba.vert  vita/rgba.frag
desktop/indexed.vert  desktop/indexed.frag  desktop/rgba.vert  desktop/rgba.frag
```

The model shaders are shared by every renderer; the sprite dialects below them are
one directory per dialect, and each dialect has the same four files:

```
indexed.vert  indexed.frag   8-bit palettised sprites
rgba.vert     rgba.frag      32-bit RGBA sprites
```

## The sprite dialects

| Directory | Dialect | Read by |
|---|---|---|
| `gles/` | GLSL ES 1.00, `a_position`/`a_texcoord` + `u_mvp` | `src/gles_render.c` (Android, Linux arm64) |
| `vita/` | vitaGL dialect, `aPosition`/`aTexCoord`, `uMvp` | `src/glrender.c` under `MUGEN_VITA_GL` |
| `desktop/` | desktop GLSL 1.20 (`gl_TexCoord`, `gl_MultiTexCoord0`, `gl_ModelViewProjectionMatrix`) | `src/glrender.c` |

**Desktop keeps the game's own files primary.** The original ships
`data/{indexed,rgba}.{vert,frag}` with M.U.G.E.N 1.1b1. Those are Elecbyte
content, stay untracked in `install/`, and `src/glrender.c` still loads them
first so log-based fidelity validation is unchanged. `desktop/` holds our
reproduction of the same four shaders, used only when the game's file is absent.
The GLES and Vita dialects are always taken from here — they are our rewrites,
not game content, so there is no file to prefer.

## Embedding

`tools/embed_shader.sh` (GNU Make) and `port/cmake/Model3D.cmake` +
`port/cmake/EmbedModelShader.cmake` (CMake ports) turn each file into a
`g_shader_<flattened-name>` literal. The name flattens `/` and `.` to `_`, so
`gles/indexed.vert` becomes `g_shader_gles_indexed_vert` and cannot collide with
the model shaders or the other dialects.

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
  and Vita palettes are 256×1 `sampler2D`s sampled at
  `(index_correction(idx), 0.5)`.

`tools/test_gles_shader.sh` (dialect `gles`) and `tools/test_vita_caps.sh`
(dialect `vita`) enforce the structural rules above on the host, reading these
source files directly, without a GPU.
