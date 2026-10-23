#version 120
/*
 * Fragment shader of the glTF models (model.c): Ikemen GO's model.frag.glsl
 * for OpenGL 2.1, without image based lighting and mesh outlines. The
 * texture colour is made linear (^2.2), lit by up to 4 punctual lights
 * (PBR, metallic-roughness; shadows from their shadow maps, with Ikemen GO's
 * desktop GL biases) unless unlit, premultiplied and brought back
 * (^1/2.2). The PalFX is the original's (the sprite programs' palfx(): colour
 * balance, inversion, add, mul) on the un-premultiplied colour.
 */

struct Light {
    vec3 direction;
    float range;
    vec3 color;
    float intensity;
    vec3 position;
    float innerConeCos;
    float outerConeCos;
    int type;
    float shadowBias;
    float shadowMapFar;
};

uniform sampler2D tex, normalMap, metallicRoughnessMap, ambientOcclusionMap, emissionMap;
uniform mat3 texTransform, normalMapTransform, metallicRoughnessMapTransform, ambientOcclusionMapTransform,
    emissionMapTransform;
uniform float alphaThreshold, ambientOcclusionStrength;
uniform vec3 cameraPosition, emission;
uniform vec4 baseColorFactor;
uniform vec2 metallicRoughness;
uniform bool unlit, useTexture, useNormalMap, useMetallicRoughnessMap, useEmissionMap, enableAlpha;
uniform Light lights[4];

/* Shadow maps of the lights: 2D for directional and spot lights, cube maps for point lights. */
uniform bool useShadowMap;
uniform sampler2D shadowMap0, shadowMap1, shadowMap2, shadowMap3;
uniform samplerCube shadowCube0, shadowCube1, shadowCube2, shadowCube3;

/* PalFX */
uniform vec3 add;
uniform vec3 mul;
uniform float colorbal;
uniform bool invertall;

varying vec2 texcoord;
varying vec4 vColor;
varying vec3 normal, tangent, bitangent, worldSpacePos;
varying vec4 lightSpacePos[4];

const float PI = 3.14159265358979;
const int LightType_Directional = 0;
const int LightType_Point = 1;
const int LightType_Spot = 2;

float clampedDot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.0, 1.0);
}

vec3 getNormal()
{
    vec2 uv_dx = dFdx(texcoord);
    vec2 uv_dy = dFdy(texcoord);
    if (length(uv_dx) <= 1e-2) {
        uv_dx = vec2(1.0, 0.0);
    }
    if (length(uv_dy) <= 1e-2) {
        uv_dy = vec2(0.0, 1.0);
    }
    vec3 t_ = (uv_dy.y * dFdx(worldSpacePos) - uv_dx.y * dFdy(worldSpacePos)) / (uv_dx.x * uv_dy.y - uv_dy.x * uv_dx.y);
    vec3 n, t, b, ng;
    if (normal.x + normal.y + normal.z != 0.0) {
        if (tangent.x + tangent.y + tangent.z != 0.0) {
            t = normalize(tangent);
            b = normalize(bitangent);
            ng = normalize(normal);
        } else {
            ng = normalize(normal);
            t = normalize(t_ - ng * dot(ng, t_));
            b = cross(ng, t);
        }
    } else {
        ng = normalize(cross(dFdx(worldSpacePos), dFdy(worldSpacePos)));
        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);
    }
    if (!gl_FrontFacing) {
        t *= -1.0;
        b *= -1.0;
        ng *= -1.0;
    }
    if (useNormalMap) {
        return normalize(mat3(t, b, ng) *
                         normalize(texture2D(normalMap, vec2(normalMapTransform * vec3(texcoord, 1.0))).xyz * 2.0 - vec3(1.0)));
    }
    return ng;
}

/* https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md#range-property */
float getRangeAttenuation(float range, float distance)
{
    if (range <= 0.0) {
        /* negative range means unlimited */
        return 1.0 / pow(distance, 2.0);
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

float getSpotAttenuation(vec3 pointToLight, vec3 spotDirection, float outerConeCos, float innerConeCos)
{
    float actualCos = dot(normalize(spotDirection), normalize(-pointToLight));
    if (actualCos > outerConeCos) {
        if (actualCos < innerConeCos) {
            float angularAttenuation = (actualCos - outerConeCos) / (innerConeCos - outerConeCos);
            return angularAttenuation * angularAttenuation;
        }
        return 1.0;
    }
    return 0.0;
}

vec3 getLightIntensity(Light light, vec3 pointToLight)
{
    float rangeAttenuation = 1.0;
    float spotAttenuation = 1.0;

    if (light.type != LightType_Directional) {
        rangeAttenuation = getRangeAttenuation(light.range, length(pointToLight));
    }
    if (light.type == LightType_Spot) {
        spotAttenuation = getSpotAttenuation(pointToLight, light.direction, light.outerConeCos, light.innerConeCos);
    }
    return rangeAttenuation * spotAttenuation * light.intensity * light.color;
}

vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH)
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

/* Smith Joint GGX; Vis = G / (4 * NdotL * NdotV) */
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGX = GGXV + GGXL;
    if (GGX > 0.0) {
        return 0.5 / GGX;
    }
    return 0.0;
}

/* Trowbridge-Reitz distribution of the microfacet normals */
float D_GGX(float NdotH, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (PI * f * f);
}

vec3 BRDF_lambertian(vec3 f0, vec3 f90, vec3 diffuseColor, float specularWeight, float VdotH)
{
    return (1.0 - specularWeight * F_Schlick(f0, f90, VdotH)) * (diffuseColor / PI);
}

vec3 BRDF_specularGGX(vec3 f0, vec3 f90, float alphaRoughness, float specularWeight, float VdotH, float NdotL,
                      float NdotV, float NdotH)
{
    vec3 F = F_Schlick(f0, f90, VdotH);
    float Vis = V_GGX(NdotL, NdotV, alphaRoughness);
    float D = D_GGX(NdotH, alphaRoughness);
    return specularWeight * F * Vis * D;
}

/* What shadow map i holds at xy / in direction d (samplers cannot be indexed here). */
float shadowMapDepth(int i, vec2 xy)
{
    if (i == 0) {
        return texture2D(shadowMap0, xy).r;
    } else if (i == 1) {
        return texture2D(shadowMap1, xy).r;
    } else if (i == 2) {
        return texture2D(shadowMap2, xy).r;
    }
    return texture2D(shadowMap3, xy).r;
}

float shadowCubeDepth(int i, vec3 d)
{
    if (i == 0) {
        return textureCube(shadowCube0, d).r;
    } else if (i == 1) {
        return textureCube(shadowCube1, d).r;
    } else if (i == 2) {
        return textureCube(shadowCube2, d).r;
    }
    return textureCube(shadowCube3, d).r;
}

float DirectionalLightShadowCalculation(int index, vec4 lightSpacePos, float NdotL, float shadowBias)
{
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    float epsilon = 1.0 / 1024.0;
    vec2 xy = vec2(clamp(projCoords.x, epsilon, 1.0 - epsilon), clamp(projCoords.y, epsilon, 1.0 - epsilon));
    float closestDepth = shadowMapDepth(index, xy);
    float currentDepth = projCoords.z;
    float slope = sqrt(1.0 - (NdotL * NdotL)) / max(NdotL, 0.0001);
    float bias = max(shadowBias, 0.0005) * clamp(slope, 0.0, 5.0);
    return closestDepth - currentDepth > -bias ? 1.0 : 0.0;
}

/* As Ikemen GO: the map is read at the light space x, y without the perspective divide. */
float SpotLightShadowCalculation(int index, vec3 pointToLight, vec4 lightSpacePos, float NdotL, float farPlane,
                                 float shadowBias)
{
    float epsilon = 1.0 / 1024.0;
    vec2 xy = vec2(clamp(lightSpacePos.x, epsilon, 1.0 - epsilon), clamp(lightSpacePos.y, epsilon, 1.0 - epsilon));
    float closestDepth = shadowMapDepth(index, xy) * farPlane;
    float currentDepth = length(pointToLight);
    float slope = sqrt(1.0 - (NdotL * NdotL)) / max(NdotL, 0.0001);
    float bias = shadowBias * clamp(slope, 0.0, 5.0);
    return currentDepth - closestDepth < bias ? 1.0 : 0.0;
}

float PointLightShadowCalculation(int index, vec3 pointToLight, float NdotL, float farPlane, float shadowBias)
{
    float closestDepth = shadowCubeDepth(index, -pointToLight) * farPlane;
    float currentDepth = length(pointToLight);
    float slope = sqrt(1.0 - (NdotL * NdotL)) / max(NdotL, 0.0001);
    float bias = shadowBias * clamp(slope, 0.0, 5.0);
    return currentDepth - closestDepth < bias ? 1.0 : 0.0;
}

/* Shadow of light i on this fragment, 1 = lit. */
float shadowFactor(int i, vec3 pointToLight, float NdotL)
{
    if (!useShadowMap) {
        return 1.0;
    }
    if (lights[i].type == LightType_Point) {
        return PointLightShadowCalculation(i, pointToLight, NdotL, lights[i].shadowMapFar, lights[i].shadowBias);
    } else if (lights[i].type == LightType_Directional) {
        return DirectionalLightShadowCalculation(i, lightSpacePos[i], NdotL, lights[i].shadowBias);
    }
    return SpotLightShadowCalculation(i, pointToLight, lightSpacePos[i], NdotL, lights[i].shadowMapFar,
                                      lights[i].shadowBias);
}

/* The lights on the fragment, in linear space. */
vec3 pbr(vec3 v, vec3 n, vec3 albedo, float metallic, float roughness, float ao)
{
    vec3 f0 = vec3(0.04) + (albedo - vec3(0.04)) * metallic;
    vec3 f90 = vec3(1.0);
    float specularWeight = 1.0;
    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);
    vec3 c_diff = albedo * (1.0 - metallic);

    for (int i = 0; i < 4; ++i) {
        if (lights[i].color.r + lights[i].color.g + lights[i].color.b > 0.0) {
            vec3 pointToLight;
            if (lights[i].type == LightType_Directional) {
                pointToLight = -lights[i].direction;
            } else {
                pointToLight = lights[i].position - worldSpacePos;
            }
            vec3 l = normalize(pointToLight);
            vec3 h = normalize(l + v);
            float NdotL = clampedDot(n, l);
            float NdotV = clampedDot(n, v);
            float NdotH = clampedDot(n, h);
            float VdotH = clampedDot(v, h);
            if (NdotL > 0.0 || NdotV > 0.0) {
                vec3 intensity = getLightIntensity(lights[i], pointToLight);
                vec3 l_diffuse = intensity * NdotL * BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
                vec3 l_specular = intensity * NdotL *
                                  BRDF_specularGGX(f0, f90, roughness * roughness, specularWeight, VdotH, NdotL, NdotV, NdotH);
                float shadow = shadowFactor(i, pointToLight, NdotL);
                f_diffuse += l_diffuse * shadow;
                f_specular += l_specular * shadow;
            }
        }
    }
    return f_diffuse + f_specular;
}

void main()
{
    vec4 color = vec4(1.0);

    if (useTexture) {
        color = texture2D(tex, vec2(texTransform * vec3(texcoord, 1.0)));
        color.rgb = pow(color.rgb, vec3(2.2));
    }
    color *= baseColorFactor;
    color *= vColor;
    if (!unlit) {
        vec3 normalF = normal;
        vec2 metallicRoughnessF = metallicRoughness;
        float ambientOcclusion = 1.0;
        if (useNormalMap) {
            normalF = getNormal();
        }
        if (useMetallicRoughnessMap) {
            metallicRoughnessF =
                texture2D(metallicRoughnessMap, vec2(metallicRoughnessMapTransform * vec3(texcoord, 1.0))).bg;
        }
        if (ambientOcclusionStrength > 0.0) {
            ambientOcclusion = 1.0 + ambientOcclusionStrength *
                                         (texture2D(ambientOcclusionMap, vec2(ambientOcclusionMapTransform * vec3(texcoord, 1.0))).r - 1.0);
        }
        color.rgb = pbr(normalize(cameraPosition - worldSpacePos), normalize(normalF), color.rgb, metallicRoughnessF[0],
                        metallicRoughnessF[1], ambientOcclusion);
        if (useEmissionMap) {
            color.rgb += emission * pow(texture2D(emissionMap, vec2(emissionMapTransform * vec3(texcoord, 1.0))).rgb, vec3(2.2));
        } else {
            color.rgb += emission;
        }
    }
    color.rgb *= color.a;
    if (!enableAlpha) {
        if (color.a < alphaThreshold) {
            discard;
        }
        color.a = 1.0;
    } else if (color.a <= 0.0) {
        discard;
    }

    vec3 c = pow(color.rgb, vec3(1.0 / 2.2));
    float a = color.a;
    if (a > 0.0) {
        c = clamp(c / a, 0.0, 1.0);
    }
    if (colorbal != 1.0) {
        float grey = c.r * .3125 + c.g * .4375 + c.b * .25;
        c = mix(vec3(grey), c, colorbal);
    }
    if (invertall) {
        c = 1.0 - c;
    }
    c = clamp((c + add) * mul, 0.0, 1.0);
    gl_FragColor = vec4(c * a, a);
}
