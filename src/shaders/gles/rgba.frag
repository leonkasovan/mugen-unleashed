/* GLES RGBA fragment shader (GLSL ES 1.00).
 *
 * Lives in src/shaders/gles/ and is embedded into the binary at build time;
 * src/gles_render.c reads it as the compiled-in literal g_shader_gles_rgba_frag.
 *
 * Port of rgba.frag: gl_TexCoord[] -> varying v_texcoord, precision
 * declared, sampler2D only (already 2D upstream). PalFX equations and
 * perspective modes unchanged.
 */
precision mediump float;
////*EB_DEFINES*////
#ifdef SPRITE_ATLAS
uniform sampler2D atlas_tex0, atlas_tex1, atlas_tex2, atlas_tex3;
uniform sampler2D atlas_tex4, atlas_tex5, atlas_tex6;
#else
uniform sampler2D texunit0;
#endif
uniform bool mask;
#ifdef SPRITE_ATLAS
varying highp vec4 v_atlas_rect;
varying highp vec4 v_atlas_params;
varying highp vec4 v_atlas_add;
varying highp vec4 v_atlas_mul;
varying highp vec4 v_atlas_fx;
#define add v_atlas_add.xyz
#define mul v_atlas_mul.xyz
#define invertall (v_atlas_fx.x > 0.5)
#define colorbal v_atlas_mul.w
#define src_alphamul v_atlas_add.w
#else
uniform vec3 add;
uniform vec3 mul;
uniform bool invertall;
uniform float colorbal;
uniform float src_alphamul;
uniform vec2 texsize;
#endif
varying vec4 v_texcoord;

#ifdef SPRITE_ATLAS
vec4 atlas_rgba_sample(highp vec2 uv)
{
  highp float slot = v_atlas_params.z;
  if (slot < 0.5) return texture2D(atlas_tex0, uv);
  if (slot < 1.5) return texture2D(atlas_tex1, uv);
  if (slot < 2.5) return texture2D(atlas_tex2, uv);
  if (slot < 3.5) return texture2D(atlas_tex3, uv);
  if (slot < 4.5) return texture2D(atlas_tex4, uv);
  if (slot < 5.5) return texture2D(atlas_tex5, uv);
  return texture2D(atlas_tex6, uv);
}
#endif


/*
 * Applies color saturation to the color.
 */
void apply_colorbal(inout vec4 color)
{
  if (colorbal != 1.0) {
    float grey;
    grey = color.r * .3125 + color.g * .4375 + color.b * .25;
    color.rgb = mix(vec3(grey, grey, grey), color.rgb, colorbal);
  }
}

/*
 * Applies invertall to the color.
 */
void apply_invertall(inout vec4 color)
{
  if (invertall)
#if !defined(PREMUL)
    color.rgb = 1.0 - color.rgb;
#else
    color.rgb = color.a - color.rgb;
#endif
}

/*
 * Applies palfx to the color.
 */
void palfx(inout vec4 color)
{
#if !defined(PALFX_DISABLE_COLORBAL)
  apply_colorbal(color);
#endif

#if !defined(PALFX_DISABLE_INVERTALL)
  apply_invertall(color);
#endif

#if !defined(PREMUL)
  color.rgb = (color.rgb + add) * mul;
#else
  color.rgb = (color.rgb + color.a * add) * mul;
#endif

  //Clamp:Required//color = clamp(color, 0.0, 1.0);
}

/*
 * Get color from texture.
 */
vec4 texcol()
{
#if defined(SPRITE_ATLAS)
  highp vec2 texel = clamp(floor(v_texcoord.st * v_atlas_rect.zw),
                           vec2(0.0), v_atlas_rect.zw - vec2(1.0));
  vec4 color = atlas_rgba_sample((v_atlas_rect.xy + texel + vec2(0.5)) / v_atlas_params.xy);
#elif defined(PERSPECTIVE_NONE)
  vec4 color = texture2D(texunit0, v_texcoord.st);
#elif defined(PERSPECTIVE_STANDARD)
  vec4 color = texture2D(texunit0, v_texcoord.st / v_texcoord.q);
#else // PERSPECTIVE_EVEN
  vec4 color = texture2D(texunit0, vec2(v_texcoord.s / v_texcoord.q, v_texcoord.t));
#endif

  // Non-masked forces alpha to 1
  if (!mask)
    color.a = 1.0;

  // Hack to mask transparency when src_alphamul+dst_alphamul != 1 or 2
  if (color.a == 0.0)
    discard;

#if !defined(PREMUL)
  color.a *= src_alphamul;
#else
  color *= src_alphamul;
#endif

  return color;
}

/*
 * Shader for drawing RGBA.
 */
void main()
{
  vec4 color = texcol();

#if !defined(PALFX_DISABLE)
  palfx(color);
#endif

  gl_FragColor = color;
}
