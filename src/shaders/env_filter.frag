#version 130
/*
 * The filtered environment cube maps and the BRDF table of image based
 * lighting (model.c): Ikemen GO's cubemapFiltering.frag.glsl (from the
 * Khronos glTF sample viewer). distribution 0: the Lambertian (diffuse)
 * cube map; 1: a mip level of the GGX (specular) cube map at `roughness`,
 * or with isLUT the GGX BRDF table.
 */

#define MATH_PI 3.1415926535897932384626433832795

in vec2 texcoord;
uniform samplerCube cubeMap;
uniform int sampleCount, distribution, width, currentFace;
uniform float roughness, intensityScale;
uniform bool isLUT;

const int cLambertian = 0;
const int cGGX = 1;

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

float saturate(float v)
{
    return clamp(v, 0.0, 1.0);
}

struct MicrofacetDistributionSample {
    float pdf;
    float cosTheta;
    float sinTheta;
    float phi;
};

float D_GGX(float NdotH, float roughness)
{
    float a = NdotH * roughness;
    float k = roughness / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / MATH_PI);
}

/* GGX microfacet distribution; the pdf of the half vector over 4 (V = N) */
MicrofacetDistributionSample GGX(vec2 xi, float roughness)
{
    MicrofacetDistributionSample ggx;
    float alpha = roughness * roughness;
    ggx.cosTheta = saturate(sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y)));
    ggx.sinTheta = sqrt(1.0 - ggx.cosTheta * ggx.cosTheta);
    ggx.phi = 2.0 * MATH_PI * xi.x;
    ggx.pdf = D_GGX(ggx.cosTheta, alpha);
    ggx.pdf /= 4.0;
    return ggx;
}

/* Cosine weighted hemisphere sampling */
MicrofacetDistributionSample Lambertian(vec2 xi, float roughness)
{
    MicrofacetDistributionSample lambertian;
    lambertian.cosTheta = sqrt(1.0 - xi.y);
    lambertian.sinTheta = sqrt(xi.y);
    lambertian.phi = 2.0 * MATH_PI * xi.x;
    lambertian.pdf = lambertian.cosTheta / MATH_PI;
    return lambertian;
}

/* Hammersley points on the hemisphere (Holger Dammertz, CC BY 3.0) */
float radicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley2d(int i, int N)
{
    return vec2(float(i) / float(N), radicalInverse_VdC(uint(i)));
}

/* A tangent frame around the (normalized) normal */
mat3 generateTBN(vec3 normal)
{
    vec3 bitangent = vec3(0.0, 1.0, 0.0);
    float NdotUp = dot(normal, vec3(0.0, 1.0, 0.0));
    float epsilon = 0.0000001;
    if (1.0 - abs(NdotUp) <= epsilon) {
        if (NdotUp > 0.0) {
            bitangent = vec3(0.0, 0.0, 1.0);
        } else {
            bitangent = vec3(0.0, 0.0, -1.0);
        }
    }
    vec3 tangent = normalize(cross(bitangent, normal));
    bitangent = cross(normal, tangent);
    return mat3(tangent, bitangent, normal);
}

/* An importance sample direction, with its pdf in w */
vec4 getImportanceSample(int sampleIndex, vec3 N, float roughness)
{
    vec2 xi = hammersley2d(sampleIndex, sampleCount);
    MicrofacetDistributionSample importanceSample;
    if (distribution == cLambertian) {
        importanceSample = Lambertian(xi, roughness);
    } else {
        importanceSample = GGX(xi, roughness);
    }
    vec3 localSpaceDirection = normalize(vec3(importanceSample.sinTheta * cos(importanceSample.phi),
                                              importanceSample.sinTheta * sin(importanceSample.phi),
                                              importanceSample.cosTheta));
    mat3 TBN = generateTBN(N);
    return vec4(TBN * localSpaceDirection, importanceSample.pdf);
}

/* Mipmap filtered samples (GPU Gems 3, 20.4), Krivanek & Colbert's formulation for cube maps */
float computeLod(float pdf)
{
    return 0.5 * log2(6.0 * float(width) * float(width) / (float(sampleCount) * pdf));
}

vec3 filterColor(vec3 N)
{
    vec3 color = vec3(0.0);
    float weight = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        vec4 importanceSample = getImportanceSample(i, N, roughness);
        vec3 H = vec3(importanceSample.xyz);
        float pdf = importanceSample.w;
        float lod = computeLod(pdf);

        if (distribution == cLambertian) {
            color += textureLod(cubeMap, H, lod).rgb * intensityScale;
        } else {
            vec3 V = N;
            vec3 L = normalize(reflect(-V, H));
            float NdotL = dot(N, L);
            if (NdotL > 0.0) {
                if (roughness == 0.0) {
                    lod = 0.0;
                }
                color += textureLod(cubeMap, L, lod).rgb * intensityScale * NdotL;
                weight += NdotL;
            }
        }
    }
    if (weight != 0.0) {
        color /= weight;
    } else {
        color /= float(sampleCount);
    }
    return color.rgb;
}

/* Filament's correlated Smith GGX visibility */
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness)
{
    float a2 = pow(roughness, 4.0);
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

/* The GGX BRDF table: scale (r) and bias (g) of F0 (Karis, s2013 notes) */
vec3 LUT(float NdotV, float roughness)
{
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);
    float A = 0.0;
    float B = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        vec4 importanceSample = getImportanceSample(i, N, roughness);
        vec3 H = importanceSample.xyz;
        vec3 L = normalize(reflect(-V, H));
        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));
        if (NdotL > 0.0 && distribution == cGGX) {
            float V_pdf = V_SmithGGXCorrelated(NdotV, NdotL, roughness) * VdotH * NdotL / NdotH;
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * V_pdf;
            B += Fc * V_pdf;
        }
    }
    return vec3(4.0 * A, 4.0 * B, 0.0) / float(sampleCount);
}

void main()
{
    if (!isLUT) {
        vec3 direction = normalize(uvToXYZ(currentFace, texcoord * 2.0 - 1.0));
        direction.y = -direction.y;
        gl_FragColor = vec4(filterColor(direction), 1.0);
    } else {
        gl_FragColor = vec4(LUT(texcoord.x, texcoord.y), 1.0);
    }
}
