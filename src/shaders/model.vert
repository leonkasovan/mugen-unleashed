/*
 * Vertex shader of the glTF models (model.c): Ikemen GO's model.vert.glsl
 * for OpenGL 2.1. Morph targets are applied to the vertex buffer on the CPU
 * (model.c); the joint matrices are uniforms (rows of their 3x4 part)
 * instead of a texture. meshOutline: the outline pass pushes the mesh out.
 * lightSpacePos: the position for the shadow map of each light.
 */

uniform mat4 projection, view, model, normalMatrix;
#ifndef MODEL_NO_SHADOW
uniform mat4 lightMatrices[4];
#endif
uniform bool skinned;
uniform vec4 joints[3 * 64];
uniform float meshOutline;
uniform vec3 cameraPosition;

MODEL_ATTRIBUTE vec3 position;
MODEL_ATTRIBUTE vec2 uv;
MODEL_ATTRIBUTE vec4 vertColor;
MODEL_ATTRIBUTE vec4 joints_0;
MODEL_ATTRIBUTE vec4 weights_0;
MODEL_ATTRIBUTE vec3 normalIn;
MODEL_ATTRIBUTE vec4 tangentIn;
MODEL_ATTRIBUTE vec4 outlineAttributeIn;

MODEL_VARYING vec2 texcoord;
MODEL_VARYING vec4 vColor;
MODEL_VARYING vec3 normal, tangent, bitangent, worldSpacePos;
#ifndef MODEL_NO_SHADOW
MODEL_VARYING vec4 lightSpacePos[4];
#endif

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
        /*
         * Port fix (normal mapping of skinned meshes): the tangent is skinned
         * like the normal, orthogonalized against it -- skinning shears the
         * basis -- and the bitangent is rebuilt from tangentIn.w. This branch
         * used to leave the tangent untouched and the bitangent at zero, and
         * the fragment shader then normalized that zero vector, so every
         * normal-mapped skinned surface rendered black.
         */
        if (dot(normal, normal) > 0.0) {
            vec3 skinned_normal = mat3(normalMatrix) * n * normal;
            if (dot(skinned_normal, skinned_normal) > 0.0) {
                normal = normalize(skinned_normal);
            }
        }
        if (dot(tangent, tangent) > 0.0) {
            vec3 skinned_tangent = normalize(mat3(model) * vec3(m * vec4(tangent, 0.0)));
            vec3 orthogonal = skinned_tangent - normal * dot(normal, skinned_tangent);
            if (dot(orthogonal, orthogonal) > 0.0) {
                tangent = normalize(orthogonal);
                bitangent = cross(normal, tangent) * tangentIn.w;
            }
        }
        world = model * m * pos;
        world.xyz += outlineOffset(world.xyz);
    } else {
        /* Port fix: a squared-length test detects a nonzero vector; x+y+z does
         * not, and (-1,0,1) is a perfectly valid direction that sums to zero. */
        if (dot(normal, normal) > 0.0) {
            normal = normalize(mat3(normalMatrix) * normal);
        }
        if (dot(tangent, tangent) > 0.0) {
            tangent = normalize(vec3(model * vec4(tangent, 0.0)));
            bitangent = cross(normal, tangent) * tangentIn.w;
        }
        world = model * pos;
        if (meshOutline != 0.0) {
            world.xyz += outlineOffset(world.xyz);
        }
    }
    worldSpacePos = vec3(world);
#ifndef MODEL_NO_SHADOW
    for (int i = 0; i < 4; i++) {
        lightSpacePos[i] = lightMatrices[i] * world;
    }
#endif
    gl_Position = projection * view * world;
}
