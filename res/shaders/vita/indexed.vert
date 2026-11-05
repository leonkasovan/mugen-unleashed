/*
 * Vertex shader for paletted (8-bit indexed) sprites -- vitaGL build.
 *
 * Rewritten from data/indexed.vert for vitaGL, which cannot run the original.
 * vitaGL's GLSL translator handles texture2D/textureCube, gl_FragColor,
 * gl_FragCoord, gl_VertexID and (with HAVE_FFP_SHADER_SUPPORT) the three
 * matrices -- but it has NO handling for gl_Vertex, gl_MultiTexCoordN or
 * gl_TexCoord[N], which is how the original is written. Those three are
 * replaced by explicit attribute/varying declarations here.
 *
 * The marker below must stay: Gl_InsertDefines (src/glrender.c) replaces
 * ////*EB_DEFINES*//// with the engine's #define block, and the engine keys
 * several shader variants off those defines (PERSPECTIVE_*, INDEX_*, PALFX_*,
 * PREMUL).
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
