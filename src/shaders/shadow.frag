#version 120
/*
 * Fragment shader of the shadow maps of the glTF models (model.c): Ikemen
 * GO's shadow.frag.glsl for OpenGL 2.1. The map stores in its colour what
 * Ikemen GO writes as the depth: the window depth for a directional light,
 * the distance to the light over shadowMapFar for the others (along a ray,
 * the depth test keeps the same nearest fragment).
 */

uniform sampler2D tex;
uniform mat3 texTransform;
uniform bool enableAlpha;
uniform bool useTexture;
uniform float alphaThreshold;
uniform vec4 baseColorFactor;
uniform bool directional;
uniform vec3 lightPosition;
uniform float shadowMapFar;

varying vec4 FragPos;
varying float vColor;
varying vec2 texcoord;

void main()
{
    vec4 color = baseColorFactor;
    if (useTexture) {
        color = color * texture2D(tex, vec2(texTransform * vec3(texcoord, 1.0)));
    }
    color.a *= vColor;
    if ((enableAlpha && color.a <= 0.0) || color.a < alphaThreshold) {
        discard;
    }
    if (!directional) {
        gl_FragColor = vec4(length(FragPos.xyz - lightPosition) / shadowMapFar);
    } else {
        float fcw = gl_FragCoord.w;
        if (abs(fcw) < 0.00001) {
            fcw = 1.0;
        }
        gl_FragColor = vec4(gl_FragCoord.z / fcw);
    }
}
