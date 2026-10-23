#version 120
/*
 * Vertex shader of the shadow maps of the glTF models (model.c): Ikemen GO's
 * shadow.vert.glsl for OpenGL 2.1, without morph targets. One light (and
 * one cube face) per pass instead of a geometry shader.
 */

uniform mat4 model;
uniform mat4 lightMatrix;
uniform bool skinned;
uniform vec4 joints[3 * 64];

attribute vec3 position;
attribute vec2 uv;
attribute vec4 vertColor;
attribute vec4 joints_0;
attribute vec4 weights_0;

varying vec4 FragPos;
varying float vColor;
varying vec2 texcoord;

mat4 jointMatrix(float index)
{
    int k = int(index) * 3;
    return transpose(mat4(joints[k], joints[k + 1], joints[k + 2], vec4(0.0, 0.0, 0.0, 1.0)));
}

void main()
{
    vec4 pos = vec4(position, 1.0);

    texcoord = uv;
    vColor = vertColor.a;
    if (skinned && weights_0.x + weights_0.y + weights_0.z + weights_0.w > 0.0) {
        mat4 m = weights_0.x * jointMatrix(joints_0.x) + weights_0.y * jointMatrix(joints_0.y) +
                 weights_0.z * jointMatrix(joints_0.z) + weights_0.w * jointMatrix(joints_0.w);
        FragPos = model * m * pos;
    } else {
        FragPos = model * pos;
    }
    gl_Position = lightMatrix * FragPos;
}
