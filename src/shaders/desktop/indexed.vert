#version 120

/*
 * Desktop GLSL 1.20 vertex shader for paletted (8-bit indexed) sprites.
 *
 * Port addition: src/glrender.c loads the game's own data/indexed.vert when it
 * is present (that file is Elecbyte content and stays untracked in install/).
 * This is the embedded fallback used only when that file is missing, and it
 * reproduces the original's behaviour exactly: pass texture coordinate 0
 * through unchanged and project with the fixed-function matrices.
 */
void main()
{
  gl_TexCoord[0] = gl_MultiTexCoord0;

  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
