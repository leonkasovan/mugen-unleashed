/* Port addition: intentionally unlit Vita profile; same base/alpha/PalFX math. */
/*
 * Port addition: the gamma conversions have an accurate and an approximate
 * form. MODEL_FAST_GAMMA is set by MUGEN_MODEL_FAST_GAMMA (model.c), which the
 * Vita build enables by default because the two pows cost roughly 15% of the
 * frame rate on the 3D stages; --no-fast-gamma builds the accurate form. The
 * fast form approximates gamma 2.2 with gamma 2.0 and changes colour slightly.
 */
#ifdef MODEL_FAST_GAMMA
#define MODEL_TO_LINEAR(x) ((x) * (x))
#define MODEL_TO_SRGB(x) sqrt(x)
#else
#define MODEL_TO_LINEAR(x) pow((x), vec3(2.2))
#define MODEL_TO_SRGB(x) pow((x), vec3(1.0 / 2.2))
#endif

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
        color.rgb = MODEL_TO_LINEAR(color.rgb);
    }
    color *= baseColorFactor * vColor;
    color.rgb *= color.a;
    if (!enableAlpha) {
        if (color.a < alphaThreshold) discard;
        color.a = 1.0;
    } else if (color.a <= 0.0) discard;
    vec3 c = MODEL_TO_SRGB(color.rgb);
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
