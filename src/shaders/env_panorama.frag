#version 130
/*
 * A face of the environment cube map from the equirectangular panorama
 * (model.c): Ikemen GO's panoramaToCubeMap.frag.glsl.
 */

#define MATH_PI 3.1415926535897932384626433832795

in vec2 texcoord;
uniform int currentFace;
uniform sampler2D panorama;

vec3 uvToXYZ(int face, vec2 uv)
{
    if (face == 0) {
        return vec3(1.0, uv.y, -uv.x);
    } else if (face == 1) {
        return vec3(-1.0, uv.y, uv.x);
    } else if (face == 2) {
        return vec3(+uv.x, -1.0, +uv.y);
    } else if (face == 3) {
        return vec3(+uv.x, 1.0, -uv.y);
    } else if (face == 4) {
        return vec3(+uv.x, uv.y, 1.0);
    }
    return vec3(-uv.x, +uv.y, -1.0);
}

vec2 dirToUV(vec3 dir)
{
    return vec2(0.5 + 0.5 * atan(dir.z, dir.x) / MATH_PI, 1.0 - acos(dir.y) / MATH_PI);
}

void main()
{
    vec3 direction = normalize(uvToXYZ(currentFace, texcoord * 2.0 - 1.0));
    gl_FragColor = vec4(texture(panorama, dirToUV(direction)).rgb, 1.0);
}
