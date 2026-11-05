/*
 * Fragment shader for paletted (8-bit indexed) sprites -- vitaGL build.
 *
 * Rewritten from data/indexed.frag. The PalFX maths is kept byte-for-byte
 * identical to the original: the add/mul/invertall/colorbal order and the
 * premultiplied variants are what the engine's visual output depends on, and
 * the Windows and Linux builds still run the original file.
 *
 * Three things had to change for vitaGL, all established by reading its source:
 *
 *  1. gl_TexCoord[0]  -> a varying. vitaGL's GLSL translator has no handling
 *     for gl_TexCoord / gl_MultiTexCoord / gl_Vertex; see indexed.vert.
 *
 *  2. sampler1D -> sampler2D, and texture1D(t, x) -> texture2D(t, vec2(x, 0.5)).
 *     The glsl_translator_hdr.h shim provides texture2D/textureCube/texture2DProj
 *     and nothing 1D, and the only sampler kinds vitaGL knows are 2D and cube.
 *     The 0.5 row coordinate samples the centre of the single row that
 *     glTexImage1D creates when vitaGL is built with UNPURE_TEXFORMATS.
 *
 *  3. #version 120 is dropped. vitaGL comments the directive out itself, so
 *     leaving it would be harmless; removing it keeps the file honest about
 *     what it is.
 *
 * The marker must stay: Gl_InsertDefines (src/glrender.c) replaces
 * ////*EB_DEFINES*//// with the engine's #define block, and the variants below
 * (PERSPECTIVE_*, INDEX_*, PALFX_*, PREMUL) select behaviour from it.
 */
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

varying vec4 vTexCoord;

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
  vec4 idxv = texture2D(texunit0, vTexCoord.st);
#elif defined(PERSPECTIVE_STANDARD)
  vec4 idxv = texture2D(texunit0, vTexCoord.st / vTexCoord.q);
#else // PERSPECTIVE_EVEN
  vec4 idxv = texture2D(texunit0, vec2(vTexCoord.s / vTexCoord.q, vTexCoord.t));
#endif
  float idx = idxv.r;

  /*if (mask && idx == 0.0) {
    discard;
  }*/

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