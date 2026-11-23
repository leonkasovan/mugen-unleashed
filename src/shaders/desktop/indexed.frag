/*
 * Desktop GLSL 1.20 fragment shader for paletted (8-bit indexed) sprites.
 *
 * Port addition: the embedded fallback for the game's own data/indexed.frag,
 * used by src/glrender.c only when that file is absent (the game's copy is
 * Elecbyte content and stays untracked in install/). The variant contract and
 * the PalFX maths are reproduced byte for byte so the visual output is
 * identical; the comments are ours.
 *
 * Variant defines, inserted at the EB_DEFINES marker by src/glrender.c:
 *   PERSPECTIVE_NONE | PERSPECTIVE_STANDARD | PERSPECTIVE_EVEN (default)
 *   INDEX_STANDARD | INDEX_GEFORCE7 | INDEX_SAFE (default)
 *   PALFX_DISABLE | PALFX_DISABLE_COLORBAL | PALFX_DISABLE_INVERTALL
 *   PREMUL
 */
#version 120
#pragma optimize(on)
////*EB_DEFINES*////

uniform sampler2D texunit0;
uniform sampler1D texunit1;
uniform bool mask;

uniform vec3 add;
uniform vec3 mul;
uniform bool invertall;
uniform float colorbal;
uniform float src_alphamul;

uniform vec2 texsize;

/* Palette index correction per GPU family (the original's three modes). */
#if defined(INDEX_STANDARD)
  #define index_correction(a)   (a)
#elif defined(INDEX_GEFORCE7)
  #define index_correction(a)   ((a) * 255.99 / 256.0)
#else
  #define index_correction(a)   ((a) * 255.1 / 256.0)
#endif

void apply_colorbal(inout vec4 color)
{
  if (colorbal != 1.0) {
    float grey;
    grey = color.r * .3125 + color.g * .4375 + color.b * .25;
    color.rgb = mix(vec3(grey, grey, grey), color.rgb, colorbal);
  }
}

void apply_invertall(inout vec4 color)
{
  if (invertall)
#if !defined(PREMUL)
    color.rgb = 1.0 - color.rgb;
#else
    color.rgb = color.a - color.rgb;
#endif
}

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
}

/* Nearest-neighbour palette lookup, with fragment discard when masking. */
vec4 nearest()
{
#if defined(PERSPECTIVE_NONE)
  vec4 idxv = texture2D(texunit0, gl_TexCoord[0].st);
#elif defined(PERSPECTIVE_STANDARD)
  vec4 idxv = texture2D(texunit0, gl_TexCoord[0].st / gl_TexCoord[0].q);
#else
  vec4 idxv = texture2D(texunit0, vec2(gl_TexCoord[0].s / gl_TexCoord[0].q, gl_TexCoord[0].t));
#endif
  float idx = idxv.r;

  vec4 color = texture1D(texunit1, index_correction(idx));
#if defined(PREMUL)
  if (mask)
    color.rgb *= color.a;
#endif

  // Non-masked forces alpha to 1
  if (!mask)
    color.a = 1.0;

  // Mask transparency when src_alphamul + dst_alphamul != 1 or 2
  if (color.a == 0.0)
    discard;

#if !defined(PREMUL)
  color.a *= src_alphamul;
#else
  color *= src_alphamul;
#endif

  return color;
}

void main()
{
  vec4 color = nearest();

#if !defined(PALFX_DISABLE)
  palfx(color);
#endif

  gl_FragColor = color;
}
