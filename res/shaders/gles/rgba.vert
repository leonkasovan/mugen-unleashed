/*
 * GLES vertex shader (GLSL ES 1.00) for 32-bit RGBA sprites.
 *
 * Lives in res/shaders/gles/ and is deployed next to the game data as
 * data/gles/rgba.vert, which is where src/gles_render.c loads it from.
 *
 * The desktop indexed.vert/rgba.vert use GLSL 120 built-ins
 * (gl_MultiTexCoord0, gl_ModelViewProjectionMatrix, gl_TexCoord) that do
 * not exist in GLES. This dialect takes explicit attributes and an MVP
 * uniform instead; the submitter (VBO path, plan section 12) supplies
 * position + texcoord per vertex. Perspective modes are preserved through
 * the Q component of the texcoord (PERSPECTIVE_* defines work the same).
 */
attribute vec4 a_position;
attribute vec4 a_texcoord;
uniform mat4 u_mvp;
varying vec4 v_texcoord;

////*EB_DEFINES*////

void main()
{
    v_texcoord = a_texcoord;
    gl_Position = u_mvp * a_position;
}
