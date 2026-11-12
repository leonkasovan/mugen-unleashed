/* Port addition: intentionally unlit Vita profile; same base/alpha/PalFX math. */
uniform sampler2D tex;
uniform mat3 texTransform;
uniform bool useTexture, enableAlpha, invertall;
uniform float alphaThreshold, colorbal;
uniform vec4 baseColorFactor;
uniform vec3 add, uPalMul;
MODEL_VARYING vec2 texcoord;
MODEL_VARYING vec4 vColor;
void main()
{
    vec4 color = vec4(1.0);
    if (useTexture) {
        color = MODEL_TEXTURE2D(tex, vec2(texTransform * vec3(texcoord, 1.0)));
        color.rgb = pow(color.rgb, vec3(2.2));
    }
    color *= baseColorFactor * vColor;
    color.rgb *= color.a;
    if (!enableAlpha) {
        if (color.a < alphaThreshold) discard;
        color.a = 1.0;
    } else if (color.a <= 0.0) discard;
    vec3 c = pow(color.rgb, vec3(1.0 / 2.2));
    float a = color.a;
    if (a > 0.0) c = clamp(c / a, 0.0, 1.0);
    if (colorbal != 1.0) {
        float grey = c.r * .3125 + c.g * .4375 + c.b * .25;
        c = mix(vec3(grey), c, colorbal);
    }
    if (invertall) c = 1.0 - c;
    c = clamp((c + add) * uPalMul, 0.0, 1.0);
    MODEL_OUTPUT = vec4(c * a, a);
}
