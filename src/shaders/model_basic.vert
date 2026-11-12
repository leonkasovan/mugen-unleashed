/* Vita basic profile: rigid/morphed meshes with CPU node animation. */
uniform mat4 projection, view, model;
MODEL_ATTRIBUTE vec3 position;
MODEL_ATTRIBUTE vec2 uv;
MODEL_ATTRIBUTE vec4 vertColor;
MODEL_VARYING vec2 texcoord;
MODEL_VARYING vec4 vColor;
void main()
{
    texcoord = uv;
    vColor = vertColor;
    gl_Position = projection * view * model * vec4(position, 1.0);
}
