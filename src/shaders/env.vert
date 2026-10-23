#version 130
/*
 * Vertex shader of the environment map passes (model.c): Ikemen GO's
 * ident.vert.glsl, a quad over the whole target.
 */

in vec2 VertCoord;
out vec2 texcoord;

void main()
{
    gl_Position = vec4(VertCoord, 0.0, 1.0);
    texcoord = (VertCoord + 1.0) / 2.0;
}
