#version 120
/*
 * Vertex shader of the glTF models (model.c): Ikemen GO's model.vert.glsl
 * for OpenGL 2.1. Morph targets are applied to the vertex buffer on the CPU
 * (model.c); the joint matrices are uniforms (rows of their 3x4 part)
 * instead of a texture. meshOutline: the outline pass pushes the mesh out.
 * lightSpacePos: the position for the shadow map of each light.
 */

uniform mat4 projection, view, model, normalMatrix;
uniform mat4 lightMatrices[4];
uniform bool skinned;
uniform vec4 joints[3 * 64];
uniform float meshOutline;
uniform vec3 cameraPosition;

attribute vec3 position;
attribute vec2 uv;
attribute vec4 vertColor;
attribute vec4 joints_0;
attribute vec4 weights_0;
attribute vec3 normalIn;
attribute vec4 tangentIn;
attribute vec4 outlineAttributeIn;

varying vec2 texcoord;
varying vec4 vColor;
varying vec3 normal, tangent, bitangent, worldSpacePos;
varying vec4 lightSpacePos[4];

mat4 jointMatrix(float index)
{
    int k = int(index) * 3;
    return transpose(mat4(joints[k], joints[k + 1], joints[k + 2], vec4(0.0, 0.0, 0.0, 1.0)));
}

/*
 * The normal matrix of a joint, as its cofactor matrix: the inverse
 * transpose times the determinant, which gives the same normal directions.
 */
mat3 jointNormalMatrix(float index)
{
    int k = int(index) * 3;
    vec3 c0 = vec3(joints[k].x, joints[k + 1].x, joints[k + 2].x);
    vec3 c1 = vec3(joints[k].y, joints[k + 1].y, joints[k + 2].y);
    vec3 c2 = vec3(joints[k].z, joints[k + 1].z, joints[k + 2].z);
    return mat3(cross(c1, c2), cross(c2, c0), cross(c0, c1));
}

/*
 * How far the outline pass pushes a vertex out: along _OUTLINE_ATTRIBUTE
 * when it has a width, else along the normal, wider farther from the camera.
 */
vec3 outlineOffset(vec3 world)
{
    float d = meshOutline * length(cameraPosition - world);
    if (outlineAttributeIn.w > 0.0) {
        return normalize(mat3(normalMatrix) * outlineAttributeIn.xyz) * outlineAttributeIn.w * d;
    }
    return normal * d;
}

void main()
{
    vec4 pos = vec4(position, 1.0);
    vec4 world;

    texcoord = uv;
    vColor = vertColor;
    normal = normalIn;
    tangent = tangentIn.xyz;
    bitangent = vec3(0.0);
    if (skinned && weights_0.x + weights_0.y + weights_0.z + weights_0.w > 0.001) {
        mat4 m = weights_0.x * jointMatrix(joints_0.x) + weights_0.y * jointMatrix(joints_0.y) +
                 weights_0.z * jointMatrix(joints_0.z) + weights_0.w * jointMatrix(joints_0.w);
        mat3 n = weights_0.x * jointNormalMatrix(joints_0.x) + weights_0.y * jointNormalMatrix(joints_0.y) +
                 weights_0.z * jointNormalMatrix(joints_0.z) + weights_0.w * jointNormalMatrix(joints_0.w);
        normal = mat3(normalMatrix) * n * normal;
        world = model * m * pos;
        world.xyz += outlineOffset(world.xyz);
    } else {
        if (normal.x + normal.y + normal.z != 0.0) {
            normal = normalize(mat3(normalMatrix) * normal);
        }
        if (tangent.x + tangent.y + tangent.z != 0.0) {
            tangent = normalize(vec3(model * vec4(tangent, 0.0)));
            bitangent = cross(normal, tangent) * tangentIn.w;
        }
        world = model * pos;
        if (meshOutline != 0.0) {
            world.xyz += outlineOffset(world.xyz);
        }
    }
    worldSpacePos = vec3(world);
    for (int i = 0; i < 4; i++) {
        lightSpacePos[i] = lightMatrices[i] * world;
    }
    gl_Position = projection * view * world;
}
