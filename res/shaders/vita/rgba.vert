/*
 * Vertex shader for 32-bit RGBA sprites -- vitaGL build.
 *
 * Rewritten from data/rgba.vert for vitaGL; see indexed.vert for why gl_Vertex,
 * gl_MultiTexCoord0 and gl_TexCoord are replaced by explicit attributes and
 * varyings. Keep the marker below for Gl_InsertDefines.
 */
////*EB_DEFINES*////

attribute vec4 aPosition;
attribute vec4 aTexCoord;

varying vec4 vTexCoord;

void main()
{
  vTexCoord = aTexCoord;
  gl_Position = gl_ModelViewProjectionMatrix * aPosition;
}
