#version 120

/*
 * Desktop GLSL 1.20 vertex shader for 32-bit RGBA sprites.
 *
 * Port addition: the embedded fallback for the game's own data/rgba.vert, see
 * desktop/indexed.vert for the policy. Identical behaviour: pass the texture
 * coordinate through and use the fixed-function projection.
 */
void main()
{
  gl_TexCoord[0] = gl_MultiTexCoord0;

  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
