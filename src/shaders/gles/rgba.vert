/*
 * GLES vertex shader (GLSL ES 1.00) for 32-bit RGBA sprites.
 *
 * Lives in src/shaders/gles/ and is embedded into the binary at build time;
 * src/gles_render.c reads it as the compiled-in literal g_shader_gles_rgba_vert.
 *
 * The desktop indexed.vert/rgba.vert use GLSL 120 built-ins
 * (gl_MultiTexCoord0, gl_ModelViewProjectionMatrix, gl_TexCoord) that do
 * not exist in GLES. This dialect takes explicit attributes and an MVP
 * uniform instead; the submitter (VBO path, plan section 12) supplies
 * position + texcoord per vertex. Perspective modes are preserved through
 * the Q component of the texcoord (PERSPECTIVE_* defines work the same).
 */
////*EB_DEFINES*////
attribute vec4 a_position;
varying vec4 v_texcoord;
#ifdef SPRITE_ATLAS
attribute mat4 a_instance_mvp;
attribute vec4 a_atlas_rect;
attribute vec4 a_atlas_params;
attribute vec4 a_atlas_add;
attribute vec4 a_atlas_mul;
attribute vec4 a_atlas_fx;
varying highp vec4 v_atlas_rect;
varying highp vec4 v_atlas_params;
varying highp vec4 v_atlas_add;
varying highp vec4 v_atlas_mul;
varying highp vec4 v_atlas_fx;
#else
attribute vec4 a_texcoord;
uniform mat4 u_mvp;
#endif
void main()
{
#ifdef SPRITE_ATLAS
    v_texcoord = vec4(a_position.xy, 0.0, 1.0);
    v_atlas_rect = a_atlas_rect;
    v_atlas_params = a_atlas_params;
    v_atlas_add = a_atlas_add;
    v_atlas_mul = a_atlas_mul;
    v_atlas_fx = a_atlas_fx;
    gl_Position = a_instance_mvp * vec4(a_position.xy * a_atlas_rect.zw, 0.0, 1.0);
#else
    v_texcoord = a_texcoord;
    gl_Position = u_mvp * a_position;
#endif
}
