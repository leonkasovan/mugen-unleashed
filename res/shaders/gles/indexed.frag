/* GLES indexed fragment shader (GLSL ES 1.00).
 *
 * Lives in res/shaders/gles/ and is deployed next to the game data as
 * data/gles/indexed.frag, which is where src/gles_render.c loads it from.
 *
 * Port of indexed.frag with the desktop-only constructs replaced:
 * - sampler1D texunit1 -> sampler2D texunit1: GLES has no 1D textures.
 *   The palette uploads as a 256x1 2D texture (nearest, clamp); the index
 *   samples texel (index_correction(idx), 0.5). Mapping is tested for
 *   0/1/254/255 and every entry (plan section 12, ES01 gate).
 * - gl_TexCoord[] -> varying v_texcoord from indexed.vert.
 * - precision mediump float declared (required in GLSL ES fragment).
 * - inout params, mix(), discard: valid GLSL ES 1.00, kept as-is.
 * Perspective modes (PERSPECTIVE_*), index modes (INDEX_*) and PalFX
 * disable flags (PALFX_*) keep their desktop meanings; the EB_DEFINES marker
 * below is the same insertion marker the loader uses.
 */
/* Perspective modes:
 *  PERSPECTIVE_NONE
 *  PERSPECTIVE_STANDARD
 *  PERSPECTIVE_EVEN (default)
 *
 * Index color mode:
 *  INDEX_STANDARD
 *  INDEX_GEFORCE7
 *  INDEX_SAFE (default)
 *
 * Palfx disabling:
 *  PALFX_DISABLE
 *  PALFX_DISABLE_COLORBAL
 *  PALFX_DISABLE_INVERTALL
 *
 */
precision mediump float;
////*EB_DEFINES*////

uniform sampler2D texunit0;
uniform sampler2D texunit1;
uniform bool mask;

uniform vec3 add;
uniform vec3 mul;
uniform bool invertall;
uniform float colorbal;
uniform float src_alphamul;

uniform vec2 texsize;
varying vec4 v_texcoord;

#if defined(INDEX_STANDARD) // This works on Radeon Mobility 1400 and is faster
  #define index_correction(a)   (a)
#elif defined(INDEX_GEFORCE7) // GeForce 7, some ATI 4xxx, possibly GeForce 6
  #define index_correction(a)   ((a) * 255.99 / 256.0)
#else // INDEX_SAFE -- Radeon Mobility HD 3400
  #define index_correction(a)   ((a) * 255.1 / 256.0)
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
 * Get nearest-neightbor color.  Performs fragment discard if masking is on.
 */
vec4 nearest()
{
#if defined(PERSPECTIVE_NONE)
  vec4 idxv = texture2D(texunit0, v_texcoord.st);
#elif defined(PERSPECTIVE_STANDARD)
  vec4 idxv = texture2D(texunit0, v_texcoord.st / v_texcoord.q);
#else // PERSPECTIVE_EVEN
  vec4 idxv = texture2D(texunit0, vec2(v_texcoord.s / v_texcoord.q, v_texcoord.t));
#endif
  float idx = idxv.r;

  /* Palette is a 256x1 2D texture: row 0.5, column = corrected index. */
  vec4 color = texture2D(texunit1, vec2(index_correction(idx), 0.5));
#if defined(PREMUL)
  if (mask)
    color.rgb *= color.a;
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
 * Shader converting 8-bit indexed color to RGBA.
 */
void main()
{
  vec4 color = nearest();

#if !defined(PALFX_DISABLE)
  palfx(color);
#endif

  gl_FragColor = color;
}
