/* M8 GLES RGBA fragment shader (GLSL ES 1.00).
 *
 * Port of rgba.frag: gl_TexCoord[] -> varying v_texcoord, precision
 * declared, sampler2D only (already 2D upstream). PalFX equations and
 * perspective modes unchanged.
 */
precision mediump float;
////*EB_DEFINES*////
uniform sampler2D texunit0;
uniform bool mask;
uniform vec3 add;
uniform vec3 mul;
uniform bool invertall;
uniform float colorbal;
uniform float src_alphamul;
uniform vec2 texsize;
varying vec4 v_texcoord;


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
#if defined(PERSPECTIVE_NONE)
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