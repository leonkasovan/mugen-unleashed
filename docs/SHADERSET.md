# ShaderSet

ShaderSet applies a fragment shader to an individual 2D sprite draw. Set
`[Video] RenderMode = OpenGL` to use it with desktop OpenGL, OpenGL ES, or
VitaGL. It uses the configured game resolution, including 640×480 on the
RG351MP. System mode draws the original sprite.

## Character setup

Add the shader alias and relative path to the character DEF.

```ini
[Shaders]
tint = shaders/tint.frag
```

Save this as `shaders/tint.frag` alongside the character's assets.

```glsl
#include "sprite_lib.frag.glsl"

void main()
{
    vec4 color = ikemenSpriteColor(ikemenSpriteUV());
    color.rgb *= vec3(param[0], 1.0, param[15]);
    FragColor = color;
}
```

Activate the effect from a CNS state.

```ini
[State 200, Tint]
type = ShaderSet
trigger1 = Time = 0
name = "tint"
time = 20
param0 = 0.4
param15 = 0.7
```

The same controller is available in ZSS.

```text
ShaderSet { name: "tint"; time: 20; param0: 0.4; param15: 0.7 }
```

| Field | Behavior |
| --- | --- |
| `name` | Shader alias. Omit it to update the active shader. Use an empty name to clear the effect. |
| `time` | Duration in simulation ticks. Defaults to one tick. Use `0` to clear or `-1` to keep the effect until it is cleared or the object resets. |
| `param0` through `param15` | Float expressions exposed as `param[0]` through `param[15]`. Use literal indices for VitaGL. |
| `tex1.spr`, `tex2.spr` | Optional SFF sprite, specified as group and image numbers. |
| `tex1.anim`, `tex2.anim` | Optional AIR action number for an animated texture. Takes precedence over the sprite in that slot. |
| `redirectid` | Runtime player ID of the effect recipient. |
| `playerno` | Select the shader aliases of a numbered player slot from 1 through 4. |
| `shadow`, `reflection`, `afterimage` | Set a flag to `0` to disable that pass. Each defaults to `1`. |

Use the `shader.` prefix on `Explod`, `ModifyExplod`, and the CNS
`Projectile` controller. For example, set `shader.name = "tint"` and
`shader.param0 = 0.4` on an Explod. A newly configured object effect defaults
to the object's lifetime. Afterimages retain the shader parameters from the
frame that produced the trail sprite.

## Stage setup

Declare aliases in the stage DEF's `[Shaders]` section. Use `shader.name`,
`shader.param0` through `shader.param15`, and the optional `shader.texN.*`
fields on a 2D `[BG ...]` element. A `[BGCtrl ...]` with `type = ShaderSet`
uses unprefixed fields and the existing stage controller timing and `ctrlid`
rules. The stage element resumes its configured shader when the override ends.

```ini
[BGCtrlDef Example]
ctrlid = 12

[BGCtrl Tint]
type = ShaderSet
time = 90, 120
name = tint
param0 = 0.4
param15 = 0.7
```

## Fragment interface

| Input | Meaning |
| --- | --- |
| `ikemenSpriteUV()` | Sprite-local texture coordinates. |
| `ikemenSpriteColor(uv)` | Engine sprite sampling with the current palette, masking, color effects and alpha. Use this when changing sprite color. |
| `ikemenSampleTex1(uv)`, `ikemenSampleTex2(uv)` | Samples the optional texture slots. Indexed sprite textures expose their indices in the red channel as normalized values. |
| `iTime`, `sTime` | Game time in seconds at 60 ticks per second, and elapsed simulation ticks for the current effect. |
| `iResolution`, `aspectRatio` | Current render target dimensions and the aspect correction relative to the game's logical coordinate space. |
| `COMPAT_TEXTURE` | Portable alias for sampling a 2D texture. |
| `bgl_RenderedTexture` | Declare this optional `sampler2D` to read pixels already drawn to the current target. On VitaGL such a shader falls back to stock rendering. |

Use the portable include and the syntax accepted by GLSL 1.20 and GLSL ES
1.00. A missing file or shader compilation/link error falls back to the stock
sprite shader. Check `mugen.log` for `ShaderSet:` diagnostics. A first draw can
compile a new shader variant. Framebuffer sampling also copies the render
target for each draw that uses it.

## Handheld verification

On an RG351MP, use the regular `port\arm64\build.bat` workflow and keep its
640×480 configuration. Run the same match once with no active ShaderSet and
again with the effect. Keep renderer, stage, session seed, and atlas/batching
settings fixed. Check the effect and the FPS overlay, and compare drawn FPS
and frame times. Repeat with sprite batching enabled if it is part of the
intended configuration.

For Vita, build with `port\psvita\build.bat --gl-selftest` and follow the Vita
port instructions for the runtime shader compiler. Check the tint on indexed
and RGBA sprites, then test a stage override and an auxiliary texture on the
device.

## Port integration notes

The feature was written against an older tree and merged back; four things
differ here and are worth knowing before changing this code again.

- **The sprite atlas.** `src/sdl2`'s GLES renderer packs eligible indexed
  sprites into shared pages and addresses them through the per-instance
  attributes (`a_instance_mvp`, `a_atlas_rect`, seven texture/palette slots).
  An effect program does not declare those, and it samples the sprite through
  its own UVs, so **an effect draw bypasses the atlas**: `gles_atlas_texture` is
  asked for the standalone texture, an SFF v3 page sprite streams its own
  region, and the quad is submitted immediately instead of being queued. The
  desktop renderer skips its own atlas queue for the same reason
  (`Gl_DrawSprite` checks `g_spriteShaderDraw` before `Sff3_Texture`). The cost
  is one draw call per shader-affected sprite rather than a merged batch.
- **Shader source.** The stock bodies are the embedded literals in
  `src/shaders/` (desktop prefers the game's `data/` file first, for fidelity
  validation; GLES and Vita always use the embedded dialect). The asset is
  spliced in by `SpriteShader_Fragment`, which rewrites the stock body into a
  callable `mugenStockColor()` and leaves the `////*EB_DEFINES*////` marker in
  place so the variant defines are still inserted afterwards. On GLES the two
  base sources are **static literals** and must not be freed -- only
  `SpriteShader_Fragment`'s result is owned.
- **Variant defines.** A desktop effect inherits `g_glShaderDefines[variant]`,
  so it gets the same index mode, perspective mode and `PALFX_DISABLE*` set as
  the stock program it replaces. GLES builds them with `gles_build_defines`.
- **Parameter delivery.** Desktop and GLES fetch `param[0]` and `glUniform1fv`
  all 16; VitaGL has no `param[16]` array and takes four `vec4 mugenParamsN`
  instead. A shader that indexes `param` with a computed index works on desktop
  and GLES but not on Vita -- use literal indices there.

### Device status

Verified on a Retroid Pocket Flip 2 (Adreno, GLES 3.0, Android 13): a
`ShaderSet` in `[Statedef -1]` tinting the character, on a 2D stage and on a
glTF 3D stage (`KOFXI_Esaka_1`), with the sprite atlas both off (default) and
on (`SpriteAtlas = 1`). `param0 = 0` renders identically to no effect, so the
uniform path is confirmed rather than the shader merely being always-on. VitaGL
remains unverified.
