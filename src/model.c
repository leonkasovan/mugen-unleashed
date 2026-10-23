/*
 * glTF 2.0 models drawn with OpenGL.
 *
 * mugen-unleashed addition, not part of the original: a port of Ikemen GO's
 * model.go, so that the 3D stages made for Ikemen GO look the same here. The
 * file is read with cgltf; the textures (PNG) with libpng. What is ported:
 * - nodes (translation / rotation / scale; "matrix" nodes too, which Ikemen
 *   GO leaves at the identity), meshes, primitives of every mode;
 * - materials: base colour texture and factor, vertex colours, alphaMode
 *   OPAQUE / MASK / BLEND, doubleSided, KHR_texture_transform;
 * - the node extras "trans" (ADD, SUB, MUL, NONE), "disableZTest",
 *   "disableZWrite", "layerNumber" and "id", the animation extras "id",
 *   "loopCount" and "enabled";
 * - animations of translation, rotation and scale (LINEAR, STEP,
 *   CUBICSPLINE) and skins.
 * Not yet: lights, image based lighting and shadows (models are drawn unlit,
 * as Ikemen GO draws them without lights and environment), morph targets,
 * mesh outlines, KHR_animation_pointer.
 *
 * Quirks of Ikemen GO kept on purpose, as the stages are made against them:
 * - the key of a sampler is the one before the first key time >= the
 *   animation time; past its last key a sampler interpolates (extrapolates)
 *   between its first two keys; STEP takes that previous key's value;
 * - rotations are interpolated with an unnormalised slerp that inverts
 *   (conjugates) the first quaternion when the two point away from each other;
 * - the texture colour is made linear (^2.2), multiplied by the base colour
 *   factor and the vertex colour, premultiplied, and the result brought back
 *   (^1/2.2) before un-premultiplying.
 * The PalFX applied to the model is the stage's, as the sprite programs of
 * the original apply it (colour balance, inversion, add, mul).
 */
#include <GL/glew.h>

/* symbols.h declares a few GLEW pointers with the original's own types: some lack a const. */
#pragma warning(push)
#pragma warning(disable : 4028)
#include "mugen.h"
#pragma warning(pop)

#include <cgltf.h>
#include <stb_image.h>
#include <math.h>

#define MODEL_PI 3.14159265358979323846

/* Vertex attribute locations of the model program. */
enum {
    MODEL_LOC_POSITION = 0,
    MODEL_LOC_UV = 1,
    MODEL_LOC_COLOR = 2,
    MODEL_LOC_JOINTS = 3,
    MODEL_LOC_WEIGHTS = 4,
    MODEL_LOC_NORMAL = 5,
    MODEL_LOC_TANGENT = 6,
    MODEL_LOC_OUTLINE = 7,
    MODEL_LOC_COUNT = 8
};

/* Texture units of the model program. */
enum {
    MODEL_UNIT_BASE = 0,
    MODEL_UNIT_NORMAL = 1,
    MODEL_UNIT_METAL_ROUGH = 2,
    MODEL_UNIT_OCCLUSION = 3,
    MODEL_UNIT_EMISSION = 4
};

/* Uniforms of a light (the shader's Light struct). */
typedef struct ModelLightLocs {
    GLint direction, range, color, intensity, position, inner_cone_cos, outer_cone_cos, type, shadow_bias,
        shadow_map_far;
} ModelLightLocs;

/* The model program and its uniforms. */
typedef struct ModelProgram {
    GLuint program;
    GLint projection, view, model, normal_matrix;
    GLint skinned, joints;
    GLint tex, normal_map, metal_rough_map, occlusion_map, emission_map;
    GLint tex_transform, normal_map_transform, metal_rough_transform, occlusion_transform, emission_transform;
    GLint use_texture, use_normal_map, use_metal_rough_map, use_emission_map;
    GLint base_color, metallic_roughness, occlusion_strength, emission, camera_position, unlit;
    GLint enable_alpha, alpha_threshold;
    GLint add, mul, colorbal, invertall;
    ModelLightLocs lights[MODEL_MAX_LIGHTS];
    GLint light_matrices, use_shadow_map;
    GLint shadow_maps[MODEL_MAX_LIGHTS], shadow_cubes[MODEL_MAX_LIGHTS];
    GLint mesh_outline;
    GLint env_lambertian, env_ggx, env_lut, env_intensity, env_mip_count, env_rotation;
} ModelProgram;

static ModelProgram g_modelProgram;
static int g_modelProgramState;     /* 0 not built yet, 1 built, -1 failed */

/* src/shaders (CMakeLists.txt embeds them). */
#include "model.vert.h"
#include "model.frag.h"
#include "shadow.vert.h"
#include "shadow.frag.h"
#include "env.vert.h"
#include "env_panorama.frag.h"
#include "env_filter.frag.h"

/* Texture units of the environment maps. */
#define MODEL_UNIT_ENV_LAMBERTIAN   13
#define MODEL_UNIT_ENV_GGX          14
#define MODEL_UNIT_ENV_LUT          15

/* Sizes and sample counts of the environment maps, as Ikemen GO's. */
#define MODEL_ENV_SIZE              256
#define MODEL_ENV_LOWEST_MIP        4
#define MODEL_ENV_LUT_SIZE          1024
#define MODEL_ENV_LAMBERTIAN_SAMPLES 2048
#define MODEL_ENV_GGX_SAMPLES       1024
#define MODEL_ENV_LUT_SAMPLES       512

/* The model program is built with image based lighting (model.frag as GLSL 1.30). */
static int g_modelIbl;

/* Image based lighting needs GL 3.0: GLSL 1.30 (textureLod, the filtering shaders) and float textures. */
static int Model_IblSupported(void)
{
    return g_renderMode == RENDER_OPENGL && GLEW_VERSION_3_0 && GLEW_EXT_framebuffer_object;
}

/* First texture units of the shadow maps: 2D (directional, spot), cube maps (point). */
#define MODEL_UNIT_SHADOW_2D    5
#define MODEL_UNIT_SHADOW_CUBE  9

/* Size of the shadow maps, as Ikemen GO's. */
#define MODEL_SHADOW_SIZE 1024

/* The shadow map program and its uniforms. */
typedef struct ShadowProgram {
    GLuint program;
    GLint model, light_matrix, skinned, joints;
    GLint tex, tex_transform, use_texture, enable_alpha, alpha_threshold, base_color;
    GLint directional, light_position, shadow_map_far;
} ShadowProgram;

/*
 * The shadow maps: one framebuffer with a depth renderbuffer, and per light
 * slot a 2D map and a cube map of the stored depth (one float channel),
 * created when first needed.
 */
typedef struct ShadowMaps {
    int state;                          /* 0 not set up yet, 1 ready, -1 unavailable */
    GLenum format;                      /* GL_R32F, or GL_LUMINANCE32F_ARB without ARB_texture_rg */
    GLuint fbo;
    GLuint depth;
    GLuint map2d[MODEL_MAX_LIGHTS];
    GLuint cube[MODEL_MAX_LIGHTS];
    const Model *drawn;                 /* the model they were last drawn for */
} ShadowMaps;

int g_modelShadows = 1;

static ShadowProgram g_shadowProgram;
static ShadowMaps g_shadowMaps;

/* ---- matrices (column-major, as GL) and quaternions (x, y, z, w) ---- */

static void Mat4_Identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* out = a * b (out may be a or b). */
static void Mat4_Mul(float *out, const float *a, const float *b)
{
    float r[16];
    int c, k;

    for (c = 0; c < 4; c++) {
        for (k = 0; k < 4; k++) {
            r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void Mat4_Translate(float *m, float x, float y, float z)
{
    Mat4_Identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

static void Mat4_Scale(float *m, float x, float y, float z)
{
    Mat4_Identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
}

/* Rotation about axis 0 (x), 1 (y) or 2 (z), angle in radians. */
static void Mat4_Rotate(float *m, int axis, float angle)
{
    float c = (float)cos(angle), s = (float)sin(angle);
    int i = (axis + 1) % 3, j = (axis + 2) % 3;

    Mat4_Identity(m);
    m[i * 4 + i] = c;
    m[i * 4 + j] = s;
    m[j * 4 + i] = -s;
    m[j * 4 + j] = c;
}

/* The rotation of quaternion q (not normalised first, as mathgl's Quat.Mat4). */
static void Quat_ToMat4(float *m, const float *q)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];

    m[0] = 1 - 2 * y * y - 2 * z * z;
    m[1] = 2 * x * y + 2 * w * z;
    m[2] = 2 * x * z - 2 * w * y;
    m[3] = 0.0f;
    m[4] = 2 * x * y - 2 * w * z;
    m[5] = 1 - 2 * x * x - 2 * z * z;
    m[6] = 2 * y * z + 2 * w * x;
    m[7] = 0.0f;
    m[8] = 2 * x * z + 2 * w * y;
    m[9] = 2 * y * z - 2 * w * x;
    m[10] = 1 - 2 * x * x - 2 * y * y;
    m[11] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

/* out = inverse of m; the zero matrix when m is singular. */
static void Mat4_Invert(float *out, const float *m)
{
    float inv[16];
    double det;
    int i;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = (double)m[0] * inv[0] + (double)m[1] * inv[4] + (double)m[2] * inv[8] + (double)m[3] * inv[12];
    if (det == 0.0) {
        memset(out, 0, 16 * sizeof(float));
        return;
    }
    for (i = 0; i < 16; i++) {
        out[i] = (float)(inv[i] / det);
    }
}

/* Determinant of the upper 3x3 (the sign tells a mirroring transform). */
static float Mat4_Det3(const float *m)
{
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
           m[8] * (m[1] * m[6] - m[5] * m[2]);
}

/* mathgl's Ortho. */
static void Mat4_Ortho(float *m, float l, float r, float b, float t, float n, float f)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

/* mathgl's Perspective, fovy in radians. */
static void Mat4_Perspective(float *m, float fovy, float aspect, float n, float f)
{
    float g = (float)(1.0 / tan(fovy / 2.0));

    memset(m, 0, 16 * sizeof(float));
    m[0] = g / aspect;
    m[5] = g;
    m[10] = (n + f) / (n - f);
    m[11] = -1.0f;
    m[14] = 2.0f * f * n / (n - f);
}

/* mathgl's LookAtV: the view from eye towards center. */
static void Mat4_LookAt(float *m, const float *eye, const float *center, const float *up)
{
    float f[3], s[3], u[3], len;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = center[i] - eye[i];
    }
    len = (float)sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (i = 0; i < 3; i++) {
        f[i] /= len;
    }
    s[0] = f[1] * up[2] - f[2] * up[1];
    s[1] = f[2] * up[0] - f[0] * up[2];
    s[2] = f[0] * up[1] - f[1] * up[0];
    len = (float)sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (i = 0; i < 3; i++) {
        s[i] /= len;
    }
    u[0] = s[1] * f[2] - s[2] * f[1];
    u[1] = s[2] * f[0] - s[0] * f[2];
    u[2] = s[0] * f[1] - s[1] * f[0];
    Mat4_Identity(m);
    for (i = 0; i < 3; i++) {
        m[i * 4 + 0] = s[i];
        m[i * 4 + 1] = u[i];
        m[i * 4 + 2] = -f[i];
    }
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}

static float Quat_Dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/* q normalised, as mathgl's Quat.Normalize. */
static void Quat_Normalize(float *q)
{
    float len = (float)sqrt(Quat_Dot(q, q));
    int i;

    if (fabs(1.0 - len) < 1e-6) {
        return;
    }
    if (len == 0.0f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    for (i = 0; i < 4; i++) {
        q[i] /= len;
    }
}

/* mathgl's QuatSlerp: no shortest path; nlerp when the quaternions are close. */
static void Quat_Slerp(float *out, const float *qa, const float *qb, float t)
{
    float a[4], b[4], rel[4];
    float dot, theta, c, s;
    int i;

    memcpy(a, qa, sizeof(a));
    memcpy(b, qb, sizeof(b));
    Quat_Normalize(a);
    Quat_Normalize(b);
    dot = Quat_Dot(a, b);
    if (dot > 0.9995f) {
        for (i = 0; i < 4; i++) {
            out[i] = a[i] + (b[i] - a[i]) * t;
        }
        Quat_Normalize(out);
        return;
    }
    if (dot < -1.0f) {
        dot = -1.0f;
    } else if (dot > 1.0f) {
        dot = 1.0f;
    }
    theta = (float)acos(dot) * t;
    c = (float)cos(theta);
    s = (float)sin(theta);
    for (i = 0; i < 4; i++) {
        rel[i] = b[i] - a[i] * dot;
    }
    Quat_Normalize(rel);
    for (i = 0; i < 4; i++) {
        out[i] = a[i] * c + rel[i] * s;
    }
}

/* ---- JSON (extras, and what cgltf does not keep) ---- */

static const char *Json_Ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/* The end of the string starting at p (on its opening quote), after the closing one; NULL if unterminated. */
static const char *Json_SkipString(const char *p)
{
    for (p++; *p != '\0' && *p != '"'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
    }
    return *p == '"' ? p + 1 : NULL;
}

/* The end of the value starting at p; NULL on malformed JSON. */
static const char *Json_Skip(const char *p)
{
    int depth = 0;

    p = Json_Ws(p);
    if (*p == '"') {
        return Json_SkipString(p);
    }
    while (*p != '\0') {
        if (*p == '"') {
            p = Json_SkipString(p);
            if (p == NULL) {
                return NULL;
            }
            if (depth == 0) {
                return p;
            }
            continue;
        }
        if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth == 0) {
                return p;
            }
            if (--depth == 0) {
                return p + 1;
            }
        } else if ((*p == ',' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') && depth == 0) {
            return p;
        }
        p++;
    }
    return depth == 0 ? p : NULL;
}

/* The value of member key of the object at p, or NULL. */
static const char *Json_Member(const char *p, const char *key)
{
    size_t key_len = strlen(key);

    if (p == NULL || *(p = Json_Ws(p)) != '{') {
        return NULL;
    }
    p++;
    for (;;) {
        const char *k;
        p = Json_Ws(p);
        if (*p != '"') {
            return NULL;
        }
        k = p + 1;
        p = Json_SkipString(p);
        if (p == NULL) {
            return NULL;
        }
        p = Json_Ws(p);
        if (*p != ':') {
            return NULL;
        }
        p = Json_Ws(p + 1);
        if ((size_t)(p - k) >= key_len + 1 && strncmp(k, key, key_len) == 0 && k[key_len] == '"') {
            return p;
        }
        p = Json_Skip(p);
        if (p == NULL || *(p = Json_Ws(p)) != ',') {
            return NULL;
        }
        p++;
    }
}

/* Element index of the array at p, or NULL. */
static const char *Json_Element(const char *p, int index)
{
    int i;

    if (p == NULL || *(p = Json_Ws(p)) != '[') {
        return NULL;
    }
    p = Json_Ws(p + 1);
    if (*p == ']') {
        return NULL;
    }
    for (i = 0; i < index; i++) {
        p = Json_Skip(p);
        if (p == NULL || *(p = Json_Ws(p)) != ',') {
            return NULL;
        }
        p = Json_Ws(p + 1);
    }
    return p;
}

/*
 * The value at p: a string unquoted (simple escapes only), anything else as
 * its JSON text; *is_string tells which. Returns 0 for NULL.
 */
static int Json_Value(const char *p, char *out, int size, int *is_string)
{
    const char *end;
    int n = 0;

    if (p == NULL) {
        return 0;
    }
    p = Json_Ws(p);
    if (*p == '"') {
        for (p++; *p != '"' && *p != '\0' && n < size - 1; p++) {
            if (*p == '\\' && p[1] != '\0') {
                p++;
            }
            out[n++] = *p;
        }
        *is_string = 1;
    } else {
        end = Json_Skip(p);
        if (end == NULL) {
            return 0;
        }
        n = (int)(end - p);
        if (n > size - 1) {
            n = size - 1;
        }
        memcpy(out, p, n);
        *is_string = 0;
    }
    out[n] = '\0';
    return 1;
}

/* Looks up `key` in the JSON object of an extras block (see Json_Value). */
static int Model_GetExtra(const char *json, const char *key, char *out, int size, int *is_string)
{
    return json != NULL && Json_Value(Json_Member(json, key), out, size, is_string);
}
/*
 * An extra that switches something when present: Ikemen GO takes any value
 * but the strings "0" and "false" as set (a JSON false counts as set).
 */
static int Model_ExtraSet(const char *json, const char *key)
{
    char v[32];
    int is_string;

    if (!Model_GetExtra(json, key, v, sizeof(v), &is_string)) {
        return 0;
    }
    return !(is_string && (strcmp(v, "0") == 0 || strcmp(v, "false") == 0));
}

/* A number extra; 0 when absent or not a number. */
static int Model_ExtraNumber(const char *json, const char *key, double *out)
{
    char v[64];
    int is_string;
    char *end;

    if (!Model_GetExtra(json, key, v, sizeof(v), &is_string) || is_string) {
        return 0;
    }
    *out = strtod(v, &end);
    return end != v;
}

/* ---- loading ---- */

/* Appends n bytes to the vertex data. */
static int Model_AddVertexData(Model *mdl, unsigned int *capacity, const void *data, unsigned int n)
{
    if (mdl->vertex_size + n > *capacity) {
        unsigned int cap = *capacity != 0 ? *capacity : 0x10000;
        unsigned char *p;
        while (mdl->vertex_size + n > cap) {
            cap *= 2;
        }
        p = realloc(mdl->vertex_data, cap);
        if (p == NULL) {
            return 0;
        }
        mdl->vertex_data = p;
        *capacity = cap;
    }
    memcpy(mdl->vertex_data + mdl->vertex_size, data, n);
    mdl->vertex_size += n;
    return 1;
}

/* The values of accessor acc as `comps` floats per element (pad: the value of missing comps), malloc'd. */
static float *Model_ReadFloats(const cgltf_accessor *acc, int count, int comps, float pad)
{
    int n = (int)cgltf_num_components(acc->type);
    float v[16];
    float *buf;
    int i, k;

    buf = malloc((size_t)count * comps * sizeof(float) + 1);
    if (buf == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        memset(v, 0, sizeof(v));
        if (i < (int)acc->count) {
            cgltf_accessor_read_float(acc, i, v, n);
        }
        for (k = 0; k < comps; k++) {
            buf[i * comps + k] = k < n ? v[k] : pad;
        }
    }
    return buf;
}

/* Appends the vertex attribute of accessor acc as `comps` floats per vertex (pad: the value of missing comps). */
static int Model_AddAttribute(Model *mdl, unsigned int *capacity, const cgltf_accessor *acc, int count, int comps,
                              float pad)
{
    float *buf = Model_ReadFloats(acc, count, comps, pad);
    int ok;

    if (buf == NULL) {
        return 0;
    }
    ok = Model_AddVertexData(mdl, capacity, buf, (unsigned int)(count * comps * sizeof(float)));
    free(buf);
    return ok;
}

/* Floats per vertex of the blocks from position to colour of a primitive (what morph targets change). */
static int Model_MorphStride(const ModelPrim *p)
{
    return 3 + ((p->attribs & MODEL_ATTR_UV) ? 2 : 0) + ((p->attribs & MODEL_ATTR_NORMAL) ? 3 : 0) +
           ((p->attribs & MODEL_ATTR_TANGENT) ? 4 : 0) + ((p->attribs & MODEL_ATTR_COLOR) ? 4 : 0);
}

/*
 * The morph targets of a primitive: a copy of its blocks from position to
 * colour, and the deltas of each target (as Ikemen GO reads them: a VEC3
 * colour delta gets an alpha of 1).
 */
static int Model_LoadTargets(Model *mdl, ModelPrim *prim, const cgltf_primitive *p)
{
    static const cgltf_attribute_type types[MODEL_MORPH_ATTRIBS] = {
        cgltf_attribute_type_position, cgltf_attribute_type_normal, cgltf_attribute_type_tangent,
        cgltf_attribute_type_texcoord, cgltf_attribute_type_color
    };
    static const int comps[MODEL_MORPH_ATTRIBS] = { 3, 3, 3, 2, 4 };
    int n = prim->vertex_count;
    cgltf_size t, i;
    int a;

    prim->applied_count = -1;
    prim->target_count = (int)p->targets_count;
    prim->base = malloc((size_t)n * Model_MorphStride(prim) * sizeof(float) + 1);
    prim->deltas = calloc(p->targets_count * MODEL_MORPH_ATTRIBS, sizeof(float *));
    if (prim->base == NULL || prim->deltas == NULL) {
        return 0;
    }
    memcpy(prim->base, mdl->vertex_data + prim->vertex_offset, (size_t)n * Model_MorphStride(prim) * sizeof(float));
    for (t = 0; t < p->targets_count; t++) {
        const cgltf_morph_target *mt = &p->targets[t];
        for (i = 0; i < mt->attributes_count; i++) {
            const cgltf_attribute *at = &mt->attributes[i];
            for (a = 0; a < MODEL_MORPH_ATTRIBS; a++) {
                if (at->type == types[a] && at->index == 0) {
                    float **d = &prim->deltas[t * MODEL_MORPH_ATTRIBS + a];
                    free(*d);
                    *d = Model_ReadFloats(at->data, n, comps[a], a == MODEL_MORPH_COLOR ? 1.0f : 0.0f);
                    if (*d == NULL) {
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

static const cgltf_accessor *Model_FindAttribute(const cgltf_primitive *p, cgltf_attribute_type type, int index)
{
    cgltf_size i;

    for (i = 0; i < p->attributes_count; i++) {
        if (p->attributes[i].type == type && p->attributes[i].index == index) {
            return p->attributes[i].data;
        }
    }
    return NULL;
}

static unsigned int Model_GlMode(cgltf_primitive_type type)
{
    switch (type) {
    case cgltf_primitive_type_points:
        return GL_POINTS;
    case cgltf_primitive_type_lines:
        return GL_LINES;
    case cgltf_primitive_type_line_loop:
        return GL_LINE_LOOP;
    case cgltf_primitive_type_line_strip:
        return GL_LINE_STRIP;
    case cgltf_primitive_type_triangle_strip:
        return GL_TRIANGLE_STRIP;
    case cgltf_primitive_type_triangle_fan:
        return GL_TRIANGLE_FAN;
    default:
        return GL_TRIANGLES;
    }
}

static int Model_LoadPrimitive(Model *mdl, ModelPrim *prim, const cgltf_data *data, const cgltf_primitive *p,
                               unsigned int *vcap, unsigned int *icap)
{
    const cgltf_accessor *pos = Model_FindAttribute(p, cgltf_attribute_type_position, 0);
    const cgltf_accessor *acc;
    unsigned int i;

    memset(prim, 0, sizeof(*prim));
    prim->mode = Model_GlMode(p->type);
    prim->material = p->material != NULL ? (int)(p->material - data->materials) : -1;
    if (pos == NULL) {
        prim->material = -1;
        return 1;
    }
    prim->vertex_count = (int)pos->count;
    prim->vertex_offset = mdl->vertex_size;
    if (!Model_AddAttribute(mdl, vcap, pos, prim->vertex_count, 3, 0.0f)) {
        return 0;
    }
    acc = Model_FindAttribute(p, cgltf_attribute_type_texcoord, 0);
    if (acc != NULL) {
        prim->attribs |= MODEL_ATTR_UV;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 2, 0.0f)) {
            return 0;
        }
    }
    acc = Model_FindAttribute(p, cgltf_attribute_type_normal, 0);
    if (acc != NULL) {
        prim->attribs |= MODEL_ATTR_NORMAL;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 3, 0.0f)) {
            return 0;
        }
    }
    acc = Model_FindAttribute(p, cgltf_attribute_type_tangent, 0);
    if (acc != NULL) {
        prim->attribs |= MODEL_ATTR_TANGENT;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 4, 0.0f)) {
            return 0;
        }
    }
    acc = Model_FindAttribute(p, cgltf_attribute_type_color, 0);
    if (acc != NULL) {
        prim->attribs |= MODEL_ATTR_COLOR;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 4, 1.0f)) {
            return 0;
        }
    }
    if (p->targets_count > 0 && !Model_LoadTargets(mdl, prim, p)) {
        return 0;
    }
    acc = Model_FindAttribute(p, cgltf_attribute_type_joints, 0);
    if (acc != NULL) {
        const cgltf_accessor *w = Model_FindAttribute(p, cgltf_attribute_type_weights, 0);
        if (w == NULL) {
            Log_Printf("Primitive attribute JOINTS_0 is specified but WEIGHTS_0 is not specified.\n");
            return 0;
        }
        prim->attribs |= MODEL_ATTR_SKIN;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 4, 0.0f) ||
            !Model_AddAttribute(mdl, vcap, w, prim->vertex_count, 4, 0.0f)) {
            return 0;
        }
    }
    for (i = 0; i < p->attributes_count; i++) {
        const cgltf_attribute *at = &p->attributes[i];
        if (at->name != NULL && strcmp(at->name, "_OUTLINE_ATTRIBUTE") == 0) {
            prim->attribs |= MODEL_ATTR_OUTLINE;
            if (!Model_AddAttribute(mdl, vcap, at->data, prim->vertex_count, 4, 0.0f)) {
                return 0;
            }
            break;
        }
    }

    /* The indices, made up for a primitive without them. */
    prim->index_count = p->indices != NULL ? (int)p->indices->count : prim->vertex_count;
    prim->index_offset = mdl->index_count * 4;
    if (mdl->index_count + prim->index_count > *icap) {
        unsigned int cap = *icap != 0 ? *icap : 0x4000;
        unsigned int *q;
        while (mdl->index_count + prim->index_count > cap) {
            cap *= 2;
        }
        q = realloc(mdl->index_data, cap * sizeof(unsigned int));
        if (q == NULL) {
            return 0;
        }
        mdl->index_data = q;
        *icap = cap;
    }
    for (i = 0; i < (unsigned int)prim->index_count; i++) {
        mdl->index_data[mdl->index_count + i] =
            p->indices != NULL ? (unsigned int)cgltf_accessor_read_index(p->indices, i) : i;
    }
    mdl->index_count += prim->index_count;
    return 1;
}

/* The matrix of a KHR_texture_transform: translation * rotation (by -angle) * scale. */
static void Model_TexTransform(ModelTexRef *ref)
{
    float c = (float)cos(ref->rotation), s = (float)sin(ref->rotation);

    memset(ref->transform, 0, sizeof(ref->transform));
    ref->transform[0] = c * ref->scale[0];
    ref->transform[1] = -s * ref->scale[0];
    ref->transform[3] = s * ref->scale[1];
    ref->transform[4] = c * ref->scale[1];
    ref->transform[6] = ref->offset[0];
    ref->transform[7] = ref->offset[1];
    ref->transform[8] = 1.0f;
}

/* A texture of a material and its KHR_texture_transform. */
static void Model_LoadTexRef(ModelTexRef *ref, const cgltf_data *data, const cgltf_texture_view *view)
{
    ref->texture = -1;
    ref->offset[0] = ref->offset[1] = 0.0f;
    ref->rotation = 0.0f;
    ref->scale[0] = ref->scale[1] = 1.0f;
    if (view->texture != NULL) {
        ref->texture = (int)(view->texture - data->textures);
        if (view->has_transform) {
            memcpy(ref->offset, view->transform.offset, sizeof(ref->offset));
            ref->rotation = view->transform.rotation;
            memcpy(ref->scale, view->transform.scale, sizeof(ref->scale));
        }
    }
    Model_TexTransform(ref);
}

static void Model_LoadMaterial(ModelMaterial *mat, const cgltf_data *data, const cgltf_material *m)
{
    const cgltf_pbr_metallic_roughness *pbr = &m->pbr_metallic_roughness;

    memset(mat, 0, sizeof(*mat));
    Model_LoadTexRef(&mat->base, data, &pbr->base_color_texture);
    Model_LoadTexRef(&mat->normal, data, &m->normal_texture);
    Model_LoadTexRef(&mat->metal_rough, data, &pbr->metallic_roughness_texture);
    Model_LoadTexRef(&mat->occlusion, data, &m->occlusion_texture);
    Model_LoadTexRef(&mat->emissive, data, &m->emissive_texture);
    if (m->has_pbr_metallic_roughness) {
        memcpy(mat->base_color, pbr->base_color_factor, sizeof(mat->base_color));
        mat->metallic = pbr->metallic_factor;
        mat->roughness = pbr->roughness_factor;
    } else {
        mat->base_color[0] = mat->base_color[1] = mat->base_color[2] = mat->base_color[3] = 1.0f;
        mat->metallic = 1.0f;
        mat->roughness = 1.0f;
    }
    /* no strength without the texture, as Ikemen GO */
    mat->occlusion_strength = mat->occlusion.texture != -1 ? m->occlusion_texture.scale : 0.0f;
    memcpy(mat->emission, m->emissive_factor, sizeof(mat->emission));
    switch (m->alpha_mode) {
    case cgltf_alpha_mode_mask:
        mat->alpha_mode = MODEL_ALPHA_MASK;
        mat->alpha_cutoff = m->alpha_cutoff;
        break;
    case cgltf_alpha_mode_blend:
        mat->alpha_mode = MODEL_ALPHA_BLEND;
        break;
    default:
        mat->alpha_mode = MODEL_ALPHA_OPAQUE;
        break;
    }
    mat->double_sided = m->double_sided != 0;
    mat->unlit = m->unlit != 0;
}

/* The bytes of an image: from its buffer view, a data URI or a file next to the model (or the usual places). */
static unsigned char *Model_ReadImage(const cgltf_image *img, const cgltf_options *options, const char *dir,
                                      size_t *size)
{
    unsigned char *bytes;

    if (img->buffer_view != NULL) {
        const unsigned char *src = cgltf_buffer_view_data(img->buffer_view);
        if (src == NULL) {
            return NULL;
        }
        *size = img->buffer_view->size;
        bytes = malloc(*size);
        if (bytes != NULL) {
            memcpy(bytes, src, *size);
        }
        return bytes;
    }
    if (img->uri == NULL) {
        return NULL;
    }
    if (strncmp(img->uri, "data:", 5) == 0) {
        const char *comma = strchr(img->uri, ',');
        size_t len;
        void *out = NULL;
        if (comma == NULL || strstr(img->uri, ";base64,") == NULL) {
            return NULL;
        }
        len = strlen(comma + 1);
        *size = len / 4 * 3;
        if (len >= 1 && comma[len] == '=') {
            (*size)--;
        }
        if (len >= 2 && comma[len - 1] == '=') {
            (*size)--;
        }
        if (cgltf_load_buffer_base64(options, *size, comma + 1, &out) != cgltf_result_success) {
            return NULL;
        }
        return out;
    } else {
        char rel[0x200];
        char path[0x200];
        FILE *fp;
        long n;

        strncpy(rel, img->uri, sizeof(rel) - 1);
        rel[sizeof(rel) - 1] = '\0';
        cgltf_decode_uri(rel);
        if (!File_Find(rel, path, NULL, dir)) {
            return NULL;
        }
        fp = fopen(path, "rb");
        if (fp == NULL) {
            return NULL;
        }
        fseek(fp, 0, SEEK_END);
        n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        bytes = n > 0 ? malloc((size_t)n) : NULL;
        if (bytes != NULL && fread(bytes, 1, (size_t)n, fp) != (size_t)n) {
            free(bytes);
            bytes = NULL;
        }
        fclose(fp);
        *size = (size_t)n;
        return bytes;
    }
}

/* Creates the GL texture of a glTF texture (Ikemen GO's sampling defaults: nearest, repeat without a sampler). */
static GLuint Model_LoadTexture(const cgltf_texture *t, const cgltf_options *options, const char *dir)
{
    GLint mag = GL_NEAREST, min = GL_NEAREST, wrap_s = GL_REPEAT, wrap_t = GL_REPEAT;
    GLint align;
    unsigned char *bytes, *pixels;
    size_t size;
    int w, h;
    GLuint id;

    if (t->image == NULL) {
        return 0;
    }
    bytes = Model_ReadImage(t->image, options, dir, &size);
    if (bytes == NULL) {
        Log_Printf("Model: can't read image %s\n", t->image->uri != NULL ? t->image->uri : "(embedded)");
        return 0;
    }
    /* PNG through libpng, JPEG through stb_image (the formats Ikemen GO reads besides DDS) */
    pixels = Png_DecodeRGBA(bytes, size, &w, &h);
    if (pixels == NULL && size <= 0x7FFFFFFF) {
        int comps;
        pixels = stbi_load_from_memory(bytes, (int)size, &w, &h, &comps, 4);
    }
    free(bytes);
    if (pixels == NULL) {
        Log_Printf("Model: unsupported image %s (PNG and JPEG are supported)\n",
                   t->image->uri != NULL ? t->image->uri : "(embedded)");
        return 0;
    }
    if (t->sampler != NULL) {
        const cgltf_sampler *s = t->sampler;
        mag = s->mag_filter == GL_NEAREST ? GL_NEAREST : GL_LINEAR;
        switch (s->min_filter) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
        case GL_LINEAR_MIPMAP_LINEAR:
            min = s->min_filter;
            break;
        default:
            min = GL_LINEAR;
            break;
        }
        wrap_s = s->wrap_s == GL_CLAMP_TO_EDGE || s->wrap_s == GL_MIRRORED_REPEAT ? s->wrap_s : GL_REPEAT;
        wrap_t = s->wrap_t == GL_CLAMP_TO_EDGE || s->wrap_t == GL_MIRRORED_REPEAT ? s->wrap_t : GL_REPEAT;
    }
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glGenTextures(1, &id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    free(pixels);
    return id;
}

/* The "shadowMapNear" ... "shadowMapBias" number extras of a light or node. */
static void Model_ShadowExtras(const char *json, float *out)
{
    static const char *const keys[MODEL_SHADOW_PARAMS] = { "shadowMapNear", "shadowMapFar",  "shadowMapBottom",
                                                           "shadowMapTop",  "shadowMapLeft", "shadowMapRight",
                                                           "shadowMapBias" };
    double v;
    int i;

    for (i = 0; i < MODEL_SHADOW_PARAMS; i++) {
        if (Model_ExtraNumber(json, keys[i], &v)) {
            out[i] = (float)v;
        }
    }
}

/* A KHR_lights_punctual light, with Ikemen GO's defaults. */
static void Model_LoadLight(ModelLight *light, const cgltf_light *l)
{
    memset(light, 0, sizeof(*light));
    switch (l->type) {
    case cgltf_light_type_point:
        light->type = MODEL_LIGHT_POINT;
        break;
    case cgltf_light_type_spot:
        light->type = MODEL_LIGHT_SPOT;
        break;
    default:
        light->type = MODEL_LIGHT_DIRECTIONAL;
        break;
    }
    memcpy(light->color, l->color, sizeof(light->color));
    light->intensity = l->intensity;
    light->range = l->range;
    light->inner_cone = l->spot_inner_cone_angle;
    light->outer_cone = l->spot_outer_cone_angle;
    if (l->extras.data != NULL) {
        Model_ShadowExtras(l->extras.data, light->shadow);
    }
}

/* Adds the light nodes under node index to a scene, children before their parent. */
static void Model_AddSceneLights(Model *mdl, ModelScene *scene, int index)
{
    const ModelNode *n = &mdl->nodes[index];
    int i;

    for (i = 0; i < n->child_count; i++) {
        Model_AddSceneLights(mdl, scene, n->children[i]);
    }
    if (n->light != -1 && scene->light_count < mdl->node_count) {
        scene->light_nodes[scene->light_count++] = index;
    }
}

static void Model_LoadNode(ModelNode *node, const cgltf_data *data, const cgltf_node *n)
{
    double v;
    cgltf_size i;
    char trans[16];
    int is_string;

    memset(node, 0, sizeof(*node));
    node->rotation[3] = 1.0f;
    node->scale[0] = node->scale[1] = node->scale[2] = 1.0f;
    if (n->has_translation) {
        memcpy(node->translation, n->translation, sizeof(node->translation));
    }
    if (n->has_rotation) {
        memcpy(node->rotation, n->rotation, sizeof(node->rotation));
    }
    if (n->has_scale) {
        memcpy(node->scale, n->scale, sizeof(node->scale));
    }
    if (n->has_matrix) {
        node->has_matrix = 1;
        memcpy(node->matrix, n->matrix, sizeof(node->matrix));
    }
    node->changed = 1;
    node->mesh = n->mesh != NULL ? (int)(n->mesh - data->meshes) : -1;
    node->skin = n->skin != NULL ? (int)(n->skin - data->skins) : -1;
    node->child_count = (int)n->children_count;
    if (node->child_count > 0) {
        node->children = malloc(node->child_count * sizeof(int));
        for (i = 0; node->children != NULL && i < n->children_count; i++) {
            node->children[i] = (int)(n->children[i] - data->nodes);
        }
        if (node->children == NULL) {
            node->child_count = 0;
        }
    }
    node->trans = MODEL_TRANS_NONE;
    node->ztest = 1;
    node->zwrite = 1;
    node->layer = MODEL_LAYER_DEFAULT;
    node->visible = 1;
    node->cast_shadow = 1;
    node->light = n->light != NULL ? (int)(n->light - data->lights) : -1;
    {
        /* the morph target weights: the node's, else its mesh's (as Ikemen GO) */
        const cgltf_float *w = n->weights;
        cgltf_size count = n->weights_count;
        if (count == 0 && n->mesh != NULL) {
            w = n->mesh->weights;
            count = n->mesh->weights_count;
            node->mesh_weights = 1;
        }
        if (count > MODEL_MAX_WEIGHTS) {
            count = MODEL_MAX_WEIGHTS;
        }
        if (count > 0) {
            node->weights = malloc(count * sizeof(float));
            node->anim_weights = calloc(count, sizeof(float));
            if (node->weights != NULL && node->anim_weights != NULL) {
                memcpy(node->weights, w, count * sizeof(float));
                node->weight_count = (int)count;
            }
        }
    }
    if (n->extras.data != NULL) {
        const char *json = n->extras.data;
        char cast[16];
        if (Model_GetExtra(json, "castShadow", cast, sizeof(cast), &is_string) && is_string &&
            (strcmp(cast, "0") == 0 || strcmp(cast, "false") == 0)) {
            node->cast_shadow = 0;
        }
        Model_ShadowExtras(json, node->shadow);
        if (Model_ExtraNumber(json, "meshOutline", &v)) {
            node->outline = (float)v;
        }
        if (Model_GetExtra(json, "trans", trans, sizeof(trans), &is_string) && is_string) {
            if (strcmp(trans, "ADD") == 0) {
                node->trans = MODEL_TRANS_ADD;
            } else if (strcmp(trans, "SUB") == 0) {
                node->trans = MODEL_TRANS_SUB;
            } else if (strcmp(trans, "MUL") == 0) {
                node->trans = MODEL_TRANS_MUL;
            }
        }
        if (Model_ExtraSet(json, "disableZTest")) {
            node->ztest = 0;
        }
        if (Model_ExtraSet(json, "disableZWrite")) {
            node->zwrite = 0;
        }
        if (Model_ExtraNumber(json, "id", &v)) {
            node->id = (int)v;
        }
        if (Model_ExtraNumber(json, "layerNumber", &v)) {
            node->layer = (int)v;
        }
    }
}

/*
 * The target of a KHR_animation_pointer (Ikemen GO's parseAnimationPointer):
 * node translation / rotation / scale / weights (or one weight), mesh
 * weights, material alphaCutoff, emissiveFactor, baseColorFactor,
 * metallicFactor, roughnessFactor, occlusion strength, the
 * KHR_texture_transform of the normal, emissive and occlusion textures and
 * Ikemen GO's pbrMetallicRoughness/extensions/KHR_texture_transform (the
 * metallic-roughness texture's), light color, intensity, range and spot
 * cone angles. Returns 0 for any other (Ikemen GO then refuses the model).
 */
static int Model_ParsePointer(Model *mdl, const char *pointer, ModelChannel *mc)
{
    char buf[256];
    char *parts[8];
    int n = 0, index;
    float *target = NULL;
    int comps = 0;
    ModelTexRef *tex = NULL;
    const char *tex_field = NULL;
    char *s;

    strncpy(buf, pointer, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (s = strtok(buf, "/"); s != NULL && n < 8; s = strtok(NULL, "/")) {
        parts[n++] = s;
    }
    if (n < 3) {
        return 0;
    }
    index = atoi(parts[1]);
    if (strcmp(parts[0], "nodes") == 0) {
        if (index < 0 || index >= mdl->node_count) {
            return 0;
        }
        mc->node = index;
        if (strcmp(parts[2], "translation") == 0) {
            mc->path = MODEL_PATH_TRANSLATION;
        } else if (strcmp(parts[2], "rotation") == 0) {
            mc->path = MODEL_PATH_ROTATION;
        } else if (strcmp(parts[2], "scale") == 0) {
            mc->path = MODEL_PATH_SCALE;
        } else if (strcmp(parts[2], "weights") == 0) {
            mc->path = MODEL_PATH_WEIGHTS;
            mc->elem = n > 3 ? atoi(parts[3]) : -1;
        } else {
            return 0;
        }
        return 1;
    }
    if (strcmp(parts[0], "meshes") == 0) {
        if (index < 0 || index >= mdl->mesh_count || strcmp(parts[2], "weights") != 0) {
            return 0;
        }
        mc->path = MODEL_PATH_MESH_WEIGHTS;
        mc->mesh = index;
        mc->elem = n > 3 ? atoi(parts[3]) : -1;
        return 1;
    }
    if (strcmp(parts[0], "materials") == 0) {
        ModelMaterial *mat;
        if (index < 0 || index >= mdl->material_count) {
            return 0;
        }
        mat = &mdl->materials[index];
        if (strcmp(parts[2], "alphaCutoff") == 0) {
            target = &mat->alpha_cutoff;
            comps = 1;
        } else if (strcmp(parts[2], "emissiveFactor") == 0) {
            target = mat->emission;
            comps = 3;
        } else if (strcmp(parts[2], "normalTexture") == 0) {
            tex = &mat->normal;
        } else if (strcmp(parts[2], "emissiveTexture") == 0) {
            tex = &mat->emissive;
        } else if (strcmp(parts[2], "occlusionTexture") == 0) {
            if (n == 4 && strcmp(parts[3], "strength") == 0) {
                target = &mat->occlusion_strength;
                comps = 1;
            } else {
                tex = &mat->occlusion;
            }
        } else if (strcmp(parts[2], "pbrMetallicRoughness") == 0 && n >= 4) {
            if (strcmp(parts[3], "baseColorFactor") == 0) {
                target = mat->base_color;
                comps = 4;
            } else if (strcmp(parts[3], "metallicFactor") == 0) {
                target = &mat->metallic;
                comps = 1;
            } else if (strcmp(parts[3], "roughnessFactor") == 0) {
                target = &mat->roughness;
                comps = 1;
            } else if (n == 6 && strcmp(parts[3], "extensions") == 0 && strcmp(parts[4], "KHR_texture_transform") == 0) {
                tex = &mat->metal_rough;
                tex_field = parts[5];
            }
        }
        if (tex != NULL && tex_field == NULL) {
            if (n != 6 || strcmp(parts[3], "extensions") != 0 || strcmp(parts[4], "KHR_texture_transform") != 0) {
                return 0;
            }
            tex_field = parts[5];
        }
        if (tex != NULL) {
            if (strcmp(tex_field, "offset") == 0) {
                target = tex->offset;
                comps = 2;
            } else if (strcmp(tex_field, "scale") == 0) {
                target = tex->scale;
                comps = 2;
            } else if (strcmp(tex_field, "rotation") == 0) {
                target = &tex->rotation;
                comps = 1;
            }
        }
    } else if (strcmp(parts[0], "extensions") == 0 && n >= 5 && strcmp(parts[1], "KHR_lights_punctual") == 0 &&
               strcmp(parts[2], "lights") == 0) {
        ModelLight *light;
        index = atoi(parts[3]);
        if (index < 0 || index >= mdl->light_count) {
            return 0;
        }
        light = &mdl->lights[index];
        if (strcmp(parts[4], "color") == 0) {
            target = light->color;
            comps = 3;
        } else if (strcmp(parts[4], "intensity") == 0) {
            target = &light->intensity;
            comps = 1;
        } else if (strcmp(parts[4], "range") == 0) {
            target = &light->range;
            comps = 1;
        } else if (n == 6 && strcmp(parts[4], "spot") == 0 && strcmp(parts[5], "innerConeAngle") == 0) {
            target = &light->inner_cone;
            comps = 1;
        } else if (n == 6 && strcmp(parts[4], "spot") == 0 && strcmp(parts[5], "outerConeAngle") == 0) {
            target = &light->outer_cone;
            comps = 1;
        }
    }
    if (target == NULL) {
        return 0;
    }
    mc->path = MODEL_PATH_FLOATS;
    mc->target = target;
    mc->comps = comps;
    memcpy(mc->rest, target, comps * sizeof(float));
    return 1;
}

static int Model_LoadAnim(Model *mdl, ModelAnim *anim, const cgltf_data *data, const cgltf_animation *a,
                          const char *json, int anim_index)
{
    double v;
    cgltf_size i;
    int k;

    memset(anim, 0, sizeof(*anim));
    anim->enabled = 1;
    anim->default_enabled = 1;
    anim->loop_count = -1;
    anim->samplers = calloc(a->samplers_count + 1, sizeof(ModelSampler));
    anim->channels = calloc(a->channels_count + 1, sizeof(ModelChannel));
    if (anim->samplers == NULL || anim->channels == NULL) {
        return 0;
    }
    anim->sampler_count = (int)a->samplers_count;
    for (i = 0; i < a->samplers_count; i++) {
        const cgltf_animation_sampler *s = &a->samplers[i];
        ModelSampler *ms = &anim->samplers[i];
        int out_count;
        ms->key_count = (int)s->input->count;
        ms->comps = (int)cgltf_num_components(s->output->type);
        ms->interp = s->interpolation == cgltf_interpolation_type_step   ? MODEL_INTERP_STEP
                     : s->interpolation == cgltf_interpolation_type_cubic_spline ? MODEL_INTERP_CUBIC
                                                                                 : MODEL_INTERP_LINEAR;
        out_count = (int)s->output->count * ms->comps;
        ms->times = malloc((ms->key_count + 1) * sizeof(float));
        ms->values = malloc((out_count + 1) * sizeof(float));
        if (ms->times == NULL || ms->values == NULL) {
            return 0;
        }
        cgltf_accessor_unpack_floats(s->input, ms->times, ms->key_count);
        cgltf_accessor_unpack_floats(s->output, ms->values, out_count);
        if (ms->key_count > 0 && anim->duration < ms->times[ms->key_count - 1]) {
            anim->duration = ms->times[ms->key_count - 1];
        }
    }
    k = 0;
    for (i = 0; i < a->channels_count; i++) {
        const cgltf_animation_channel *c = &a->channels[i];
        ModelChannel *mc = &anim->channels[k];
        ModelSampler *ms;
        const char *ptr;
        char pointer[256];
        int is_string;

        if (c->sampler == NULL) {
            continue;
        }
        memset(mc, 0, sizeof(*mc));
        mc->sampler = (int)(c->sampler - a->samplers);
        mc->elem = -1;
        mc->mesh = -1;
        ms = &anim->samplers[mc->sampler];
        /* KHR_animation_pointer (cgltf does not keep it): its target goes before the path */
        ptr = Json_Member(Json_Element(Json_Member(Json_Element(Json_Member(json, "animations"), anim_index), "channels"),
                                       (int)i),
                          "target");
        ptr = Json_Member(Json_Member(Json_Member(ptr, "extensions"), "KHR_animation_pointer"), "pointer");
        if (json != NULL && Json_Value(ptr, pointer, sizeof(pointer), &is_string) && is_string) {
            if (!Model_ParsePointer(mdl, pointer, mc)) {
                Log_Printf("Model: unsupported animation pointer %s, ignored\n", pointer);
                continue;
            }
        } else {
            if (c->target_node == NULL) {
                continue;
            }
            switch (c->target_path) {
            case cgltf_animation_path_type_translation:
                mc->path = MODEL_PATH_TRANSLATION;
                break;
            case cgltf_animation_path_type_rotation:
                mc->path = MODEL_PATH_ROTATION;
                break;
            case cgltf_animation_path_type_scale:
                mc->path = MODEL_PATH_SCALE;
                break;
            case cgltf_animation_path_type_weights:
                mc->path = MODEL_PATH_WEIGHTS;
                break;
            default:
                continue;
            }
            mc->node = (int)(c->target_node - data->nodes);
        }
        if ((mc->path == MODEL_PATH_WEIGHTS || mc->path == MODEL_PATH_MESH_WEIGHTS) && mc->elem < 0) {
            /* a key holds a weight per target of the node (mesh) */
            int count = mc->path == MODEL_PATH_WEIGHTS ? mdl->nodes[mc->node].weight_count
                                                       : mdl->meshes[mc->mesh].weight_count;
            if (count == 0) {
                continue;
            }
            ms->comps = count;
        } else if (mc->path == MODEL_PATH_FLOATS && ms->comps != mc->comps) {
            Log_Printf("Model: animation pointer %s has %i values per key, not %i, ignored\n", pointer, ms->comps,
                       mc->comps);
            continue;
        }
        k++;
    }
    anim->channel_count = k;
    if (a->extras.data != NULL) {
        if (Model_ExtraNumber(a->extras.data, "id", &v)) {
            anim->id = (int)v;
        }
        if (Model_ExtraNumber(a->extras.data, "loopCount", &v)) {
            anim->loop_count = (int)v;
        }
        {
            char e[32];
            int is_string;
            if (Model_GetExtra(a->extras.data, "enabled", e, sizeof(e), &is_string)) {
                anim->enabled = !(is_string && (strcmp(e, "0") == 0 || strcmp(e, "false") == 0));
                anim->default_enabled = anim->enabled;
            }
        }
    }
    return 1;
}

static int Model_LoadSkin(ModelSkin *skin, const cgltf_data *data, const cgltf_skin *s)
{
    cgltf_size i;

    memset(skin, 0, sizeof(*skin));
    skin->joint_count = (int)s->joints_count;
    if (skin->joint_count > MODEL_MAX_JOINTS) {
        Log_Printf("Model: a skin has %i joints, only the first %i are used\n", skin->joint_count,
                        MODEL_MAX_JOINTS);
    }
    skin->joints = malloc((skin->joint_count + 1) * sizeof(int));
    skin->inverse_bind = malloc((skin->joint_count + 1) * 16 * sizeof(float));
    skin->matrices = calloc((skin->joint_count + 1) * 12, sizeof(float));
    if (skin->joints == NULL || skin->inverse_bind == NULL || skin->matrices == NULL) {
        return 0;
    }
    for (i = 0; i < s->joints_count; i++) {
        skin->joints[i] = (int)(s->joints[i] - data->nodes);
        Mat4_Identity(skin->inverse_bind + i * 16);
        if (s->inverse_bind_matrices != NULL) {
            cgltf_accessor_read_float(s->inverse_bind_matrices, i, skin->inverse_bind + i * 16, 16);
        }
    }
    return 1;
}

/* Uploads the buffers and textures. */
static int Model_Upload(Model *mdl, const cgltf_data *data, const cgltf_options *options, const char *dir)
{
    cgltf_size i;

    if (g_renderMode != RENDER_OPENGL || !g_glInitialized) {
        return 1;
    }
    mdl->textures = calloc(data->textures_count + 1, sizeof(unsigned int));
    if (mdl->textures == NULL) {
        return 0;
    }
    mdl->texture_count = (int)data->textures_count;
    for (i = 0; i < data->textures_count; i++) {
        mdl->textures[i] = Model_LoadTexture(&data->textures[i], options, dir);
    }
    glGenBuffers(1, &mdl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBufferData(GL_ARRAY_BUFFER, mdl->vertex_size, mdl->vertex_data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &mdl->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mdl->index_count * 4, mdl->index_data, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    free(mdl->vertex_data);
    mdl->vertex_data = NULL;
    free(mdl->index_data);
    mdl->index_data = NULL;
    return 1;
}

/*
 * Loads a glTF (.gltf) or binary glTF (.glb) model. Returns NULL with a
 * match log message on errors. Without the OpenGL renderer the model is
 * loaded but has nothing to draw.
 */
Model *Model_Load(const char *path)
{
    cgltf_options options;
    cgltf_data *data = NULL;
    cgltf_result r;
    unsigned int vcap = 0, icap = 0;
    char dir[0x200];
    char *json = NULL;
    Model *mdl;
    cgltf_size i, j;

    memset(&options, 0, sizeof(options));
    r = cgltf_parse_file(&options, path, &data);
    if (r == cgltf_result_success) {
        r = cgltf_load_buffers(&options, data, path);
    }
    if (r != cgltf_result_success) {
        Log_Printf("Can't load model %s (cgltf error %i)\n", path, (int)r);
        cgltf_free(data);
        return NULL;
    }
    Path_Split(path, dir, NULL);
    mdl = calloc(1, sizeof(Model));
    if (mdl == NULL) {
        cgltf_free(data);
        return NULL;
    }
    /* the JSON, terminated, for what cgltf does not keep (KHR_animation_pointer) */
    if (data->json != NULL) {
        json = malloc(data->json_size + 1);
        if (json != NULL) {
            memcpy(json, data->json, data->json_size);
            json[data->json_size] = '\0';
        }
    }

    mdl->material_count = (int)data->materials_count;
    mdl->materials = calloc(data->materials_count + 1, sizeof(ModelMaterial));
    mdl->mesh_count = (int)data->meshes_count;
    mdl->meshes = calloc(data->meshes_count + 1, sizeof(ModelMesh));
    for (i = 0; i < data->meshes_count; i++) {
        mdl->prim_count += (int)data->meshes[i].primitives_count;
    }
    mdl->prims = calloc(mdl->prim_count + 1, sizeof(ModelPrim));
    mdl->node_count = (int)data->nodes_count;
    mdl->nodes = calloc(data->nodes_count + 1, sizeof(ModelNode));
    mdl->anim_count = (int)data->animations_count;
    mdl->anims = calloc(data->animations_count + 1, sizeof(ModelAnim));
    mdl->skin_count = (int)data->skins_count;
    mdl->skins = calloc(data->skins_count + 1, sizeof(ModelSkin));
    mdl->scene_count = (int)data->scenes_count;
    mdl->scenes = calloc(data->scenes_count + 1, sizeof(ModelScene));
    mdl->light_count = (int)data->lights_count;
    mdl->lights = calloc(data->lights_count + 1, sizeof(ModelLight));
    if (mdl->materials == NULL || mdl->meshes == NULL || mdl->prims == NULL || mdl->nodes == NULL ||
        mdl->anims == NULL || mdl->skins == NULL || mdl->scenes == NULL || mdl->lights == NULL) {
        goto fail;
    }
    for (i = 0; i < data->lights_count; i++) {
        Model_LoadLight(&mdl->lights[i], &data->lights[i]);
    }
    for (i = 0; i < data->materials_count; i++) {
        Model_LoadMaterial(&mdl->materials[i], data, &data->materials[i]);
    }
    {
        int k = 0;
        for (i = 0; i < data->meshes_count; i++) {
            const cgltf_mesh *m = &data->meshes[i];
            mdl->meshes[i].first_prim = k;
            mdl->meshes[i].prim_count = (int)m->primitives_count;
            mdl->meshes[i].weight_count = m->weights_count < MODEL_MAX_WEIGHTS ? (int)m->weights_count : MODEL_MAX_WEIGHTS;
            for (j = 0; j < m->primitives_count; j++, k++) {
                if (!Model_LoadPrimitive(mdl, &mdl->prims[k], data, &m->primitives[j], &vcap, &icap)) {
                    goto fail;
                }
            }
        }
    }
    for (i = 0; i < data->nodes_count; i++) {
        Model_LoadNode(&mdl->nodes[i], data, &data->nodes[i]);
    }
    for (i = 0; i < data->animations_count; i++) {
        if (!Model_LoadAnim(mdl, &mdl->anims[i], data, &data->animations[i], json, (int)i)) {
            goto fail;
        }
    }
    for (i = 0; i < data->skins_count; i++) {
        if (!Model_LoadSkin(&mdl->skins[i], data, &data->skins[i])) {
            goto fail;
        }
    }
    for (i = 0; i < data->scenes_count; i++) {
        const cgltf_scene *s = &data->scenes[i];
        ModelScene *ms = &mdl->scenes[i];
        ms->node_count = (int)s->nodes_count;
        ms->nodes = malloc((s->nodes_count + 1) * sizeof(int));
        if (ms->nodes == NULL) {
            goto fail;
        }
        for (j = 0; j < s->nodes_count; j++) {
            ms->nodes[j] = (int)(s->nodes[j] - data->nodes);
        }
        ms->light_nodes = malloc((data->nodes_count + 1) * sizeof(int));
        if (ms->light_nodes == NULL) {
            goto fail;
        }
        for (j = 0; j < s->nodes_count; j++) {
            Model_AddSceneLights(mdl, ms, ms->nodes[j]);
        }
    }
    if (!Model_Upload(mdl, data, &options, dir)) {
        goto fail;
    }
    free(json);
    cgltf_free(data);
    return mdl;

fail:
    Log_Printf("Error loading model %s\n", path);
    free(json);
    cgltf_free(data);
    Model_Free(mdl);
    return NULL;
}

static void Model_FreeEnvironment(ModelEnv *env);

void Model_Free(Model *mdl)
{
    int i, k;

    if (mdl == NULL) {
        return;
    }
    Model_FreeEnvironment(mdl->env);
    if (mdl->vbo != 0) {
        glDeleteBuffers(1, &mdl->vbo);
    }
    if (mdl->ibo != 0) {
        glDeleteBuffers(1, &mdl->ibo);
    }
    for (i = 0; i < mdl->texture_count; i++) {
        if (mdl->textures[i] != 0) {
            glDeleteTextures(1, &mdl->textures[i]);
        }
    }
    free(mdl->textures);
    for (i = 0; mdl->nodes != NULL && i < mdl->node_count; i++) {
        free(mdl->nodes[i].children);
        free(mdl->nodes[i].weights);
        free(mdl->nodes[i].anim_weights);
    }
    free(mdl->nodes);
    for (i = 0; mdl->prims != NULL && i < mdl->prim_count; i++) {
        ModelPrim *p = &mdl->prims[i];
        for (k = 0; p->deltas != NULL && k < p->target_count * MODEL_MORPH_ATTRIBS; k++) {
            free(p->deltas[k]);
        }
        free(p->deltas);
        free(p->base);
    }
    for (i = 0; mdl->anims != NULL && i < mdl->anim_count; i++) {
        ModelAnim *a = &mdl->anims[i];
        for (k = 0; a->samplers != NULL && k < a->sampler_count; k++) {
            free(a->samplers[k].times);
            free(a->samplers[k].values);
        }
        free(a->samplers);
        free(a->channels);
    }
    free(mdl->anims);
    for (i = 0; mdl->skins != NULL && i < mdl->skin_count; i++) {
        free(mdl->skins[i].joints);
        free(mdl->skins[i].inverse_bind);
        free(mdl->skins[i].matrices);
    }
    free(mdl->skins);
    for (i = 0; mdl->scenes != NULL && i < mdl->scene_count; i++) {
        free(mdl->scenes[i].nodes);
        free(mdl->scenes[i].light_nodes);
    }
    free(mdl->scenes);
    free(mdl->meshes);
    free(mdl->prims);
    free(mdl->materials);
    free(mdl->lights);
    free(mdl->vertex_data);
    free(mdl->index_data);
    free(mdl);
}

/* ---- animation ---- */

/*
 * The value of a sampler at time t (Ikemen GO's calculateAnimInterpolation /
 * calculateAnimQuatInterpolation): prev is the key before t, -1 before the
 * first key.
 */
static void Model_Sample(const ModelSampler *s, float t, int prev, int quat, float *out)
{
    int n = s->comps;
    int i;

    if (s->interp == MODEL_INTERP_STEP || prev == -1 || s->key_count == 1) {
        if (prev == -1) {
            prev = 0;
        }
        for (i = 0; i < n; i++) {
            out[i] = s->interp == MODEL_INTERP_CUBIC ? s->values[prev * 3 * n + n + i] : s->values[prev * n + i];
        }
        return;
    }
    if (s->interp == MODEL_INTERP_LINEAR) {
        float rate = (t - s->times[prev]) / (s->times[prev + 1] - s->times[prev]);
        if (!quat) {
            for (i = 0; i < n; i++) {
                out[i] = s->values[prev * n + i] * (1 - rate) + s->values[(prev + 1) * n + i] * rate;
            }
        } else {
            float q1[4], q2[4];
            memcpy(q1, s->values + prev * 4, sizeof(q1));
            memcpy(q2, s->values + (prev + 1) * 4, sizeof(q2));
            if (Quat_Dot(q1, q2) < 0) {
                /* mathgl's Quat.Inverse: the conjugate over the squared length */
                float d = Quat_Dot(q1, q1);
                q1[0] = -q1[0] / d;
                q1[1] = -q1[1] / d;
                q1[2] = -q1[2] / d;
                q1[3] = q1[3] / d;
            }
            Quat_Slerp(out, q1, q2, rate);
        }
        return;
    }
    {
        float delta = s->times[prev + 1] - s->times[prev];
        float rate = (t - s->times[prev]) / delta;
        float rate2 = rate * rate;
        float rate3 = rate2 * rate;
        const float *k0 = s->values + prev * 3 * n;
        const float *k1 = s->values + (prev + 1) * 3 * n;
        for (i = 0; i < n; i++) {
            out[i] = (2 * rate3 - 3 * rate2 + 1) * k0[n + i] + delta * (rate3 - 2 * rate2 + rate) * k0[2 * n + i] +
                     (-2 * rate3 + 3 * rate2) * k1[n + i] + delta * (rate3 - rate2) * k1[i];
        }
        if (quat) {
            Quat_Normalize(out);
        }
    }
}

/* The weights of an animation on a node: all of them, or weight elem (the others staying). */
static void Model_AnimateWeights(ModelNode *node, int elem, const float *v)
{
    if (node->weight_count == 0) {
        return;
    }
    if (elem < 0) {
        memcpy(node->anim_weights, v, node->weight_count * sizeof(float));
    } else if (elem < node->weight_count) {
        if (!(node->animated & MODEL_ANIMATED_W)) {
            memcpy(node->anim_weights, node->weights, node->weight_count * sizeof(float));
        }
        node->anim_weights[elem] = v[0];
    }
    node->animated |= MODEL_ANIMATED_W;
}

/* One tick (1/60 s) of the enabled animations (Ikemen GO's Model.step). */
void Model_Step(Model *mdl)
{
    int i, k;

    if (mdl == NULL) {
        return;
    }
    for (i = 0; i < mdl->anim_count; i++) {
        ModelAnim *a = &mdl->anims[i];
        double t60;

        if (!a->enabled) {
            continue;
        }
        a->time += 1.0f / 60.0f;
        while (a->time >= a->duration && a->duration > 0 && (a->loop_count < 0 || a->loop < a->loop_count)) {
            a->time -= a->duration;
            a->loop++;
        }
        /* snap to whole ticks against the rounding of the float steps */
        t60 = 60.0 * a->time;
        if (fabs(t60 - floor(t60)) < 0.001) {
            a->time = (float)(floor(t60) / 60.0);
        } else if (fabs(t60 - ceil(t60)) < 0.001) {
            a->time = (float)(ceil(t60) / 60.0);
        }
        if (a->time >= a->duration && a->duration > 0) {
            a->time = a->duration;
        }
        for (k = 0; k < a->channel_count; k++) {
            const ModelChannel *c = &a->channels[k];
            const ModelSampler *s = &a->samplers[c->sampler];
            ModelNode *node = &mdl->nodes[c->node];
            float v[MODEL_MAX_WEIGHTS];
            int prev = 0;
            int j;

            if (s->key_count == 0) {
                continue;
            }
            for (j = 0; j < s->key_count; j++) {
                if (a->time > 0 && a->time <= s->times[j]) {
                    prev = j - 1;
                    break;
                }
            }
            switch (c->path) {
            case MODEL_PATH_TRANSLATION:
                Model_Sample(s, a->time, prev, 0, v);
                memcpy(node->anim_translation, v, 3 * sizeof(float));
                node->animated |= MODEL_ANIMATED_T;
                break;
            case MODEL_PATH_SCALE:
                Model_Sample(s, a->time, prev, 0, v);
                memcpy(node->anim_scale, v, 3 * sizeof(float));
                node->animated |= MODEL_ANIMATED_S;
                break;
            case MODEL_PATH_ROTATION:
                Model_Sample(s, a->time, prev, 1, v);
                memcpy(node->anim_rotation, v, 4 * sizeof(float));
                node->animated |= MODEL_ANIMATED_R;
                break;
            case MODEL_PATH_WEIGHTS:
                Model_Sample(s, a->time, prev, 0, v);
                Model_AnimateWeights(node, c->elem, v);
                continue;
            case MODEL_PATH_MESH_WEIGHTS:
                Model_Sample(s, a->time, prev, 0, v);
                for (j = 0; j < mdl->node_count; j++) {
                    if (mdl->nodes[j].mesh == c->mesh && mdl->nodes[j].mesh_weights) {
                        Model_AnimateWeights(&mdl->nodes[j], c->elem, v);
                    }
                }
                continue;
            case MODEL_PATH_FLOATS:
                Model_Sample(s, a->time, prev, 0, v);
                memcpy(c->target, v, c->comps * sizeof(float));
                continue;
            }
            node->changed = 1;
        }
    }
}

/*
 * Switches animation index on (from its start, every time) or off (the
 * nodes it animated go back to their rest values), as Ikemen GO's
 * GLTFAnimation.toggle.
 */
void Model_EnableAnim(Model *mdl, int index, int enabled)
{
    ModelAnim *a = &mdl->anims[index];
    int i;

    if (enabled) {
        a->enabled = 1;
        a->time = 0.0f;
    } else if (a->enabled) {
        a->enabled = 0;
        for (i = 0; i < a->channel_count; i++) {
            const ModelChannel *c = &a->channels[i];
            ModelNode *n = &mdl->nodes[c->node];
            int j;
            if (c->path == MODEL_PATH_FLOATS) {
                memcpy(c->target, c->rest, c->comps * sizeof(float));
                continue;
            }
            if (c->path == MODEL_PATH_MESH_WEIGHTS) {
                for (j = 0; j < mdl->node_count; j++) {
                    if (mdl->nodes[j].mesh == c->mesh && mdl->nodes[j].mesh_weights) {
                        mdl->nodes[j].animated &= ~MODEL_ANIMATED_W;
                    }
                }
                continue;
            }
            switch (c->path) {
            case MODEL_PATH_TRANSLATION:
                n->animated &= ~MODEL_ANIMATED_T;
                break;
            case MODEL_PATH_ROTATION:
                n->animated &= ~MODEL_ANIMATED_R;
                break;
            case MODEL_PATH_SCALE:
                n->animated &= ~MODEL_ANIMATED_S;
                break;
            case MODEL_PATH_WEIGHTS:
                n->animated &= ~MODEL_ANIMATED_W;
                break;
            }
            n->changed = 1;
        }
    }
}

/* The animations back at their start, all nodes visible (Ikemen GO's Model.reset). */
void Model_Reset(Model *mdl)
{
    int i;

    if (mdl == NULL) {
        return;
    }
    for (i = 0; i < mdl->anim_count; i++) {
        mdl->anims[i].time = 0.0f;
        mdl->anims[i].enabled = mdl->anims[i].default_enabled;
        mdl->anims[i].loop = 0;
    }
    for (i = 0; i < mdl->node_count; i++) {
        mdl->nodes[i].visible = 1;
    }
}

/* ---- drawing ---- */

static void Model_LocalTransform(ModelNode *n)
{
    float m[16];
    const float *t, *r, *s;

    if (!n->changed) {
        return;
    }
    n->changed = 0;
    if (n->has_matrix && (n->animated & (MODEL_ANIMATED_T | MODEL_ANIMATED_R | MODEL_ANIMATED_S)) == 0) {
        memcpy(n->local, n->matrix, sizeof(n->local));
        return;
    }
    t = (n->animated & MODEL_ANIMATED_T) ? n->anim_translation : n->translation;
    r = (n->animated & MODEL_ANIMATED_R) ? n->anim_rotation : n->rotation;
    s = (n->animated & MODEL_ANIMATED_S) ? n->anim_scale : n->scale;
    Mat4_Translate(n->local, t[0], t[1], t[2]);
    Quat_ToMat4(m, r);
    Mat4_Mul(n->local, n->local, m);
    Mat4_Scale(m, s[0], s[1], s[2]);
    Mat4_Mul(n->local, n->local, m);
}

/*
 * The world transforms of node index and its children; mesh nodes get their
 * normal matrix, light nodes the direction of their light (-z of the
 * rotation, as Ikemen GO's calculateWorldTransform).
 */
static void Model_WorldTransform(Model *mdl, int index, const float *parent)
{
    ModelNode *n = &mdl->nodes[index];
    int i;

    Model_LocalTransform(n);
    Mat4_Mul(n->world, parent, n->local);
    if (n->mesh != -1) {
        float inv[16];
        int r, c;
        Mat4_Invert(inv, n->world);
        for (c = 0; c < 4; c++) {
            for (r = 0; r < 4; r++) {
                n->normal_matrix[c * 4 + r] = inv[r * 4 + c];
            }
        }
    }
    if (n->light != -1) {
        float len = (float)sqrt(n->world[8] * n->world[8] + n->world[9] * n->world[9] + n->world[10] * n->world[10]);
        if (len != 0.0f) {
            n->light_dir[0] = -n->world[8] / len;
            n->light_dir[1] = -n->world[9] / len;
            n->light_dir[2] = -n->world[10] / len;
        }
    }
    for (i = 0; i < n->child_count; i++) {
        Model_WorldTransform(mdl, n->children[i], n->world);
    }
}

/* The joint matrices of the skins of the mesh nodes under node index, children first. */
static void Model_SkinMatrices(Model *mdl, int index)
{
    ModelNode *n = &mdl->nodes[index];
    ModelSkin *skin;
    float inv[16], m[16];
    int i, r;

    for (i = 0; i < n->child_count; i++) {
        Model_SkinMatrices(mdl, n->children[i]);
    }
    if (n->mesh == -1 || n->skin == -1) {
        return;
    }
    skin = &mdl->skins[n->skin];
    Mat4_Invert(inv, n->world);
    for (i = 0; i < skin->joint_count && i < MODEL_MAX_JOINTS; i++) {
        Mat4_Mul(m, mdl->nodes[skin->joints[i]].world, skin->inverse_bind + i * 16);
        Mat4_Mul(m, inv, m);
        for (r = 0; r < 3; r++) {
            skin->matrices[i * 12 + r * 4 + 0] = m[r];
            skin->matrices[i * 12 + r * 4 + 1] = m[4 + r];
            skin->matrices[i * 12 + r * 4 + 2] = m[8 + r];
            skin->matrices[i * 12 + r * 4 + 3] = m[12 + r];
        }
    }
}

static GLint Model_Uniform(const char *name)
{
    return glGetUniformLocation(g_modelProgram.program, name);
}

static int Model_BuildProgram(void)
{
    static const char *const attribs[MODEL_LOC_COUNT] = { "position",  "uv",       "vertColor", "joints_0",
                                                          "weights_0", "normalIn", "tangentIn", "outlineAttributeIn" };
    static const char *const fields[] = { "direction",    "range",        "color", "intensity",  "position",
                                          "innerConeCos", "outerConeCos", "type",  "shadowBias", "shadowMapFar" };
    /* model.frag has no #version line: it comes first, with what image based lighting needs */
    static const char frag_head[] = "#version 120\n";
    static const char frag_head_ibl[] = "#version 130\n#define MODEL_IBL\n";
    const char *src[2][2];
    GLsizei parts[2] = { 1, 2 };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    ModelProgram *p = &g_modelProgram;
    GLuint shaders[2];
    char log[0x800];
    char name[64];
    GLint ok;
    int i, k;

    g_modelIbl = Model_IblSupported();
    src[0][0] = g_shader_model_vert;
    src[1][0] = g_modelIbl ? frag_head_ibl : frag_head;
    src[1][1] = g_shader_model_frag;
    p->program = glCreateProgram();
    for (i = 0; i < 2; i++) {
        shaders[i] = glCreateShader(types[i]);
        glShaderSource(shaders[i], parts[i], src[i], NULL);
        glCompileShader(shaders[i]);
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(shaders[i], sizeof(log), NULL, log);
            Log_Printf("Model: shader compile error:\n%s\n", log);
            return 0;
        }
        glAttachShader(p->program, shaders[i]);
    }
    for (i = 0; i < MODEL_LOC_COUNT; i++) {
        glBindAttribLocation(p->program, i, attribs[i]);
    }
    glLinkProgram(p->program);
    glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p->program, sizeof(log), NULL, log);
        Log_Printf("Model: shader link error:\n%s\n", log);
        return 0;
    }
    p->projection = Model_Uniform("projection");
    p->view = Model_Uniform("view");
    p->model = Model_Uniform("model");
    p->normal_matrix = Model_Uniform("normalMatrix");
    p->skinned = Model_Uniform("skinned");
    p->joints = Model_Uniform("joints");
    p->tex = Model_Uniform("tex");
    p->normal_map = Model_Uniform("normalMap");
    p->metal_rough_map = Model_Uniform("metallicRoughnessMap");
    p->occlusion_map = Model_Uniform("ambientOcclusionMap");
    p->emission_map = Model_Uniform("emissionMap");
    p->tex_transform = Model_Uniform("texTransform");
    p->normal_map_transform = Model_Uniform("normalMapTransform");
    p->metal_rough_transform = Model_Uniform("metallicRoughnessMapTransform");
    p->occlusion_transform = Model_Uniform("ambientOcclusionMapTransform");
    p->emission_transform = Model_Uniform("emissionMapTransform");
    p->use_texture = Model_Uniform("useTexture");
    p->use_normal_map = Model_Uniform("useNormalMap");
    p->use_metal_rough_map = Model_Uniform("useMetallicRoughnessMap");
    p->use_emission_map = Model_Uniform("useEmissionMap");
    p->base_color = Model_Uniform("baseColorFactor");
    p->metallic_roughness = Model_Uniform("metallicRoughness");
    p->occlusion_strength = Model_Uniform("ambientOcclusionStrength");
    p->emission = Model_Uniform("emission");
    p->camera_position = Model_Uniform("cameraPosition");
    p->unlit = Model_Uniform("unlit");
    p->enable_alpha = Model_Uniform("enableAlpha");
    p->alpha_threshold = Model_Uniform("alphaThreshold");
    p->add = Model_Uniform("add");
    p->mul = Model_Uniform("mul");
    p->colorbal = Model_Uniform("colorbal");
    p->invertall = Model_Uniform("invertall");
    for (i = 0; i < MODEL_MAX_LIGHTS; i++) {
        GLint *locs = &p->lights[i].direction;
        for (k = 0; k < 10; k++) {
            sprintf(name, "lights[%i].%s", i, fields[k]);
            locs[k] = Model_Uniform(name);
        }
        sprintf(name, "shadowMap%i", i);
        p->shadow_maps[i] = Model_Uniform(name);
        sprintf(name, "shadowCube%i", i);
        p->shadow_cubes[i] = Model_Uniform(name);
    }
    p->light_matrices = Model_Uniform("lightMatrices");
    p->use_shadow_map = Model_Uniform("useShadowMap");
    p->mesh_outline = Model_Uniform("meshOutline");
    p->env_lambertian = Model_Uniform("lambertianEnvSampler");
    p->env_ggx = Model_Uniform("GGXEnvSampler");
    p->env_lut = Model_Uniform("GGXLUT");
    p->env_intensity = Model_Uniform("environmentIntensity");
    p->env_mip_count = Model_Uniform("mipCount");
    p->env_rotation = Model_Uniform("environmentRotation");
    return 1;
}

/* Points an attribute at the next block of a primitive's vertex data, or gives it a constant value. */
static void Model_SetAttribute(int loc, int present, int comps, unsigned int *offset, unsigned int n, float x, float y,
                               float z, float w)
{
    if (present) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, comps, GL_FLOAT, GL_FALSE, 0, (const void *)(size_t)*offset);
        *offset += comps * 4 * n;
    } else {
        glDisableVertexAttribArray(loc);
        glVertexAttrib4f(loc, x, y, z, w);
    }
}

/* Points the vertex attributes at a primitive's blocks (in Model_LoadPrimitive's order). */
static void Model_SetAttributes(const ModelPrim *p)
{
    unsigned int offset = p->vertex_offset;
    unsigned int n = (unsigned int)p->vertex_count;
    int skin = (p->attribs & MODEL_ATTR_SKIN) != 0;

    Model_SetAttribute(MODEL_LOC_POSITION, 1, 3, &offset, n, 0.0f, 0.0f, 0.0f, 1.0f);
    Model_SetAttribute(MODEL_LOC_UV, (p->attribs & MODEL_ATTR_UV) != 0, 2, &offset, n, 0.0f, 0.0f, 0.0f, 1.0f);
    Model_SetAttribute(MODEL_LOC_NORMAL, (p->attribs & MODEL_ATTR_NORMAL) != 0, 3, &offset, n, 0.0f, 0.0f, 0.0f,
                       1.0f);
    Model_SetAttribute(MODEL_LOC_TANGENT, (p->attribs & MODEL_ATTR_TANGENT) != 0, 4, &offset, n, 0.0f, 0.0f, 0.0f,
                       0.0f);
    Model_SetAttribute(MODEL_LOC_COLOR, (p->attribs & MODEL_ATTR_COLOR) != 0, 4, &offset, n, 1.0f, 1.0f, 1.0f, 1.0f);
    Model_SetAttribute(MODEL_LOC_JOINTS, skin, 4, &offset, n, 0.0f, 0.0f, 0.0f, 0.0f);
    Model_SetAttribute(MODEL_LOC_WEIGHTS, skin, 4, &offset, n, 0.0f, 0.0f, 0.0f, 0.0f);
    Model_SetAttribute(MODEL_LOC_OUTLINE, (p->attribs & MODEL_ATTR_OUTLINE) != 0, 4, &offset, n, 0.0f, 0.0f, 0.0f,
                       0.0f);
}

/*
 * Morphs a primitive's vertex blocks for the weights of the node drawing it
 * (Ikemen GO does it in the vertex shader): the targets with a weight, their
 * positions, then normals, tangents, uvs and colours, the first
 * MODEL_MAX_MORPH_SLOTS of those only. The buffer is written when the
 * weights differ from the ones it holds. The vertex buffer must be bound.
 */
static void Model_Morph(ModelPrim *p, const ModelNode *n)
{
    static const int comps[MODEL_MORPH_ATTRIBS] = { 3, 3, 3, 2, 4 };
    const float *weights;
    int count, active[MODEL_MAX_WEIGHTS];
    int stride, active_count = 0, slots = 0;
    int offsets[MODEL_MORPH_ATTRIBS], strides[MODEL_MORPH_ATTRIBS];
    int vn = p->vertex_count;
    float *buf;
    int a, i, k, v, c, off;

    if (p->target_count == 0) {
        return;
    }
    weights = (n->animated & MODEL_ANIMATED_W) ? n->anim_weights : n->weights;
    count = n->weight_count < p->target_count ? n->weight_count : p->target_count;
    if (p->applied_count == count && (count == 0 || memcmp(p->applied, weights, count * sizeof(float)) == 0)) {
        return;
    }
    for (i = 0; i < count; i++) {
        if (weights[i] != 0.0f) {
            active[active_count++] = i;
        }
    }

    /* where each morphed attribute is in the blocks (-1: the primitive lacks it) */
    off = 3 * vn;
    offsets[MODEL_MORPH_POSITION] = 0;
    strides[MODEL_MORPH_POSITION] = 3;
    offsets[MODEL_MORPH_UV] = (p->attribs & MODEL_ATTR_UV) ? off : -1;
    strides[MODEL_MORPH_UV] = 2;
    off += (p->attribs & MODEL_ATTR_UV) ? 2 * vn : 0;
    offsets[MODEL_MORPH_NORMAL] = (p->attribs & MODEL_ATTR_NORMAL) ? off : -1;
    strides[MODEL_MORPH_NORMAL] = 3;
    off += (p->attribs & MODEL_ATTR_NORMAL) ? 3 * vn : 0;
    offsets[MODEL_MORPH_TANGENT] = (p->attribs & MODEL_ATTR_TANGENT) ? off : -1;
    strides[MODEL_MORPH_TANGENT] = 4;
    off += (p->attribs & MODEL_ATTR_TANGENT) ? 4 * vn : 0;
    offsets[MODEL_MORPH_COLOR] = (p->attribs & MODEL_ATTR_COLOR) ? off : -1;
    strides[MODEL_MORPH_COLOR] = 4;

    stride = Model_MorphStride(p);
    buf = malloc((size_t)vn * stride * sizeof(float) + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, p->base, (size_t)vn * stride * sizeof(float));
    for (a = 0; a < MODEL_MORPH_ATTRIBS; a++) {
        for (k = 0; k < active_count; k++) {
            const float *d = p->deltas[active[k] * MODEL_MORPH_ATTRIBS + a];
            float w = weights[active[k]];
            float *dst;
            if (d == NULL) {
                continue;
            }
            if (slots++ >= MODEL_MAX_MORPH_SLOTS || offsets[a] < 0) {
                continue;
            }
            dst = buf + offsets[a];
            for (v = 0; v < vn; v++) {
                for (c = 0; c < comps[a]; c++) {
                    dst[v * strides[a] + c] += w * d[v * comps[a] + c];
                }
            }
        }
    }
    glBufferSubData(GL_ARRAY_BUFFER, p->vertex_offset, (GLsizeiptr)vn * stride * sizeof(float), buf);
    free(buf);
    if (count > 0) {
        memcpy(p->applied, weights, count * sizeof(float));
    }
    p->applied_count = count;
}

/* The blending of a node (Ikemen GO's drawNode without PalFX inversion). */
static void Model_SetBlend(int trans)
{
    switch (trans) {
    case MODEL_TRANS_ADD:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case MODEL_TRANS_SUB:
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case MODEL_TRANS_MUL:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

/* Binds a material texture to its unit; returns whether there is one. */
static int Model_BindTexture(const Model *mdl, const ModelTexRef *ref, int unit, GLint transform_loc)
{
    glUniformMatrix3fv(transform_loc, 1, GL_FALSE, ref->transform);
    if (ref->texture == -1 || mdl->textures[ref->texture] == 0) {
        return 0;
    }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, mdl->textures[ref->texture]);
    return 1;
}

/*
 * Draws the meshes of node index and its children (children first) on layer
 * `layer`: the opaque ones (blended 0) or the blended and "trans" ones.
 */
static void Model_DrawNode(Model *mdl, int index, int layer, int default_layer, int blended, int flipped, int unlit,
                           float outline)
{
    const ModelProgram *prog = &g_modelProgram;
    ModelNode *n = &mdl->nodes[index];
    const ModelMesh *mesh;
    int node_layer = n->layer != MODEL_LAYER_DEFAULT ? n->layer : default_layer;
    int i;

    for (i = 0; i < n->child_count; i++) {
        Model_DrawNode(mdl, n->children[i], layer, default_layer, blended, flipped, unlit, outline);
    }
    if (n->mesh == -1 || !n->visible || node_layer != layer) {
        return;
    }
    mesh = &mdl->meshes[n->mesh];
    for (i = 0; i < mesh->prim_count; i++) {
        ModelPrim *p = &mdl->prims[mesh->first_prim + i];
        const ModelMaterial *mat;
        int is_blended;

        if (p->material == -1 || p->index_count == 0) {
            continue;
        }
        mat = &mdl->materials[p->material];
        is_blended = mat->alpha_mode == MODEL_ALPHA_BLEND || n->trans != MODEL_TRANS_NONE;
        if (is_blended != blended) {
            continue;
        }
        if (n->ztest) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(n->zwrite ? GL_TRUE : GL_FALSE);
        glFrontFace(((Mat4_Det3(n->world) < 0) != flipped) ? GL_CW : GL_CCW);
        if (mat->double_sided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        Model_SetBlend(n->trans);
        Model_Morph(p, n);
        Model_SetAttributes(p);
        glUniformMatrix4fv(prog->model, 1, GL_FALSE, n->world);
        glUniformMatrix4fv(prog->normal_matrix, 1, GL_FALSE, n->normal_matrix);
        if (n->skin != -1 && (p->attribs & MODEL_ATTR_SKIN)) {
            const ModelSkin *skin = &mdl->skins[n->skin];
            int count = skin->joint_count < MODEL_MAX_JOINTS ? skin->joint_count : MODEL_MAX_JOINTS;
            glUniform1i(prog->skinned, 1);
            glUniform4fv(prog->joints, count * 3, skin->matrices);
        } else {
            glUniform1i(prog->skinned, 0);
        }
        glUniform4fv(prog->base_color, 1, mat->base_color);
        glUniform2f(prog->metallic_roughness, mat->metallic, mat->roughness);
        glUniform1f(prog->occlusion_strength, mat->occlusion_strength);
        glUniform3fv(prog->emission, 1, mat->emission);
        glUniform1i(prog->unlit, unlit || mat->unlit);
        glUniform1i(prog->enable_alpha, mat->alpha_mode == MODEL_ALPHA_BLEND);
        glUniform1f(prog->alpha_threshold, mat->alpha_cutoff);
        glUniform1i(prog->use_texture, Model_BindTexture(mdl, &mat->base, MODEL_UNIT_BASE, prog->tex_transform));
        glUniform1i(prog->use_normal_map,
                    Model_BindTexture(mdl, &mat->normal, MODEL_UNIT_NORMAL, prog->normal_map_transform));
        glUniform1i(prog->use_metal_rough_map,
                    Model_BindTexture(mdl, &mat->metal_rough, MODEL_UNIT_METAL_ROUGH, prog->metal_rough_transform));
        Model_BindTexture(mdl, &mat->occlusion, MODEL_UNIT_OCCLUSION, prog->occlusion_transform);
        glUniform1i(prog->use_emission_map,
                    Model_BindTexture(mdl, &mat->emissive, MODEL_UNIT_EMISSION, prog->emission_transform));
        glDrawElements(p->mode, p->index_count, GL_UNSIGNED_INT, (const void *)(size_t)p->index_offset);
        if (n->outline > 0.0f) {
            /* the outline: the mesh pushed out along its normals, back faces only, in black (Ikemen GO) */
            glFrontFace(((Mat4_Det3(n->world) < 0) != flipped) ? GL_CCW : GL_CW);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glUniform1f(prog->mesh_outline, n->outline * outline);
            glDrawElements(p->mode, p->index_count, GL_UNSIGNED_INT, (const void *)(size_t)p->index_offset);
            glUniform1f(prog->mesh_outline, 0.0f);
        }
    }
}

/*
 * The shadow map parameters of a light node: Ikemen GO's defaults (near
 * 0.1, -20 for a directional light, far 50, -20..20 wide and high, bias
 * 0.02), overridden by the light's extras, then by the node's.
 */
static void Model_ShadowParams(const Model *mdl, const ModelNode *n, float *p)
{
    const ModelLight *l = &mdl->lights[n->light];
    int i;

    p[MODEL_SHADOW_NEAR] = l->type == MODEL_LIGHT_DIRECTIONAL ? -20.0f : 0.1f;
    p[MODEL_SHADOW_FAR] = 50.0f;
    p[MODEL_SHADOW_BOTTOM] = -20.0f;
    p[MODEL_SHADOW_TOP] = 20.0f;
    p[MODEL_SHADOW_LEFT] = -20.0f;
    p[MODEL_SHADOW_RIGHT] = 20.0f;
    p[MODEL_SHADOW_BIAS] = 0.02f;
    for (i = 0; i < MODEL_SHADOW_PARAMS; i++) {
        if (l->shadow[i] != 0.0f) {
            p[i] = l->shadow[i];
        }
    }
    for (i = 0; i < MODEL_SHADOW_PARAMS; i++) {
        if (n->shadow[i] != 0.0f) {
            p[i] = n->shadow[i];
        }
    }
}

/*
 * The projection of a light's shadow map (face `face` of a point light's
 * cube): as Ikemen GO, a directional or spot light looks from the node's
 * local position along its direction, a point light from its world position
 * along the 6 axes; directional lights are orthographic, the others have a
 * 90 degree perspective.
 */
static void Model_LightMatrix(const Model *mdl, const ModelNode *n, const float *p, int face, float *out)
{
    static const float axes[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const float ups[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
    static const float up_y[3] = { 0.0f, 1.0f, 0.0f };
    const ModelLight *l = &mdl->lights[n->light];
    float proj[16], view[16], eye[3], center[3];
    int i;

    if (l->type == MODEL_LIGHT_POINT) {
        for (i = 0; i < 3; i++) {
            eye[i] = n->world[12 + i];
            center[i] = eye[i] + axes[face][i];
        }
        Mat4_Perspective(proj, (float)(MODEL_PI / 2.0), 1.0f, p[MODEL_SHADOW_NEAR], p[MODEL_SHADOW_FAR]);
        Mat4_LookAt(view, eye, center, ups[face]);
    } else {
        for (i = 0; i < 3; i++) {
            eye[i] = n->local[12 + i];
            center[i] = eye[i] + n->light_dir[i];
        }
        if (l->type == MODEL_LIGHT_DIRECTIONAL) {
            Mat4_Ortho(proj, p[MODEL_SHADOW_LEFT], p[MODEL_SHADOW_RIGHT], p[MODEL_SHADOW_BOTTOM], p[MODEL_SHADOW_TOP],
                       p[MODEL_SHADOW_NEAR], p[MODEL_SHADOW_FAR]);
        } else {
            Mat4_Perspective(proj, (float)(MODEL_PI / 2.0), 1.0f, p[MODEL_SHADOW_NEAR], p[MODEL_SHADOW_FAR]);
        }
        Mat4_LookAt(view, eye, center, up_y);
    }
    Mat4_Mul(out, proj, view);
}

/* The parameters of the lights of a scene (the first MODEL_MAX_LIGHTS); returns how many there are. */
static int Model_SetLights(const Model *mdl, const ModelScene *sc)
{
    const ModelProgram *prog = &g_modelProgram;
    float matrices[MODEL_MAX_LIGHTS][16];
    int i;

    for (i = 0; i < MODEL_MAX_LIGHTS; i++) {
        glUniform3f(prog->lights[i].color, 0.0f, 0.0f, 0.0f);
        Mat4_Identity(matrices[i]);
    }
    for (i = 0; i < sc->light_count && i < MODEL_MAX_LIGHTS; i++) {
        const ModelNode *n = &mdl->nodes[sc->light_nodes[i]];
        const ModelLight *l = &mdl->lights[n->light];
        const ModelLightLocs *loc = &prog->lights[i];
        float params[MODEL_SHADOW_PARAMS];
        float far_z, bias;

        Model_ShadowParams(mdl, n, params);
        far_z = params[MODEL_SHADOW_FAR];
        bias = params[MODEL_SHADOW_BIAS];
        if (l->type != MODEL_LIGHT_POINT) {
            Model_LightMatrix(mdl, n, params, 0, matrices[i]);
        }
        glUniform1i(loc->type, l->type);
        glUniform1f(loc->intensity, l->intensity);
        glUniform1f(loc->inner_cone_cos, (float)cos(l->inner_cone));
        glUniform1f(loc->outer_cone_cos, (float)cos(l->outer_cone));
        glUniform1f(loc->range, l->range);
        glUniform3fv(loc->color, 1, l->color);
        glUniform3f(loc->position, n->world[12], n->world[13], n->world[14]);
        glUniform1f(loc->shadow_map_far, far_z);
        glUniform1f(loc->shadow_bias, bias);
        if (l->type != MODEL_LIGHT_POINT) {
            glUniform3fv(loc->direction, 1, n->light_dir);
        }
    }
    glUniformMatrix4fv(prog->light_matrices, MODEL_MAX_LIGHTS, GL_FALSE, matrices[0]);
    return sc->light_count;
}

/* ---- shadow maps ---- */

static int Model_BuildShadowProgram(void)
{
    static const char *const attribs[MODEL_LOC_COUNT] = { "position",  "uv",       "vertColor", "joints_0",
                                                          "weights_0", "normalIn", "tangentIn", "outlineAttributeIn" };
    const char *src[2] = { g_shader_shadow_vert, g_shader_shadow_frag };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    ShadowProgram *p = &g_shadowProgram;
    char log[0x800];
    GLuint shader;
    GLint ok;
    int i;

    p->program = glCreateProgram();
    for (i = 0; i < 2; i++) {
        shader = glCreateShader(types[i]);
        glShaderSource(shader, 1, &src[i], NULL);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(shader, sizeof(log), NULL, log);
            Log_Printf("Model: shadow shader compile error:\n%s\n", log);
            return 0;
        }
        glAttachShader(p->program, shader);
    }
    for (i = 0; i < MODEL_LOC_COUNT; i++) {
        glBindAttribLocation(p->program, i, attribs[i]);
    }
    glLinkProgram(p->program);
    glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p->program, sizeof(log), NULL, log);
        Log_Printf("Model: shadow shader link error:\n%s\n", log);
        return 0;
    }
    p->model = glGetUniformLocation(p->program, "model");
    p->light_matrix = glGetUniformLocation(p->program, "lightMatrix");
    p->skinned = glGetUniformLocation(p->program, "skinned");
    p->joints = glGetUniformLocation(p->program, "joints");
    p->tex = glGetUniformLocation(p->program, "tex");
    p->tex_transform = glGetUniformLocation(p->program, "texTransform");
    p->use_texture = glGetUniformLocation(p->program, "useTexture");
    p->enable_alpha = glGetUniformLocation(p->program, "enableAlpha");
    p->alpha_threshold = glGetUniformLocation(p->program, "alphaThreshold");
    p->base_color = glGetUniformLocation(p->program, "baseColorFactor");
    p->directional = glGetUniformLocation(p->program, "directional");
    p->light_position = glGetUniformLocation(p->program, "lightPosition");
    p->shadow_map_far = glGetUniformLocation(p->program, "shadowMapFar");
    return 1;
}

/* Sets up the shadow maps once: the framebuffer, its depth buffer and the program. Returns 0 without shadows. */
static int Model_ShadowSetup(void)
{
    ShadowMaps *sm = &g_shadowMaps;

    if (sm->state != 0) {
        return sm->state == 1;
    }
    sm->state = -1;
    if (!GLEW_EXT_framebuffer_object) {
        Log_Printf("Model: no shadows (EXT_framebuffer_object missing)\n");
        return 0;
    }
    if (GLEW_VERSION_3_0 || GLEW_ARB_texture_rg) {
        sm->format = GL_R32F;
    } else if (GLEW_ARB_texture_float) {
        sm->format = GL_LUMINANCE32F_ARB;
    } else {
        Log_Printf("Model: no shadows (float textures are not supported)\n");
        return 0;
    }
    if (!Model_BuildShadowProgram()) {
        return 0;
    }
    glGenFramebuffersEXT(1, &sm->fbo);
    glGenRenderbuffersEXT(1, &sm->depth);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, sm->depth);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, MODEL_SHADOW_SIZE, MODEL_SHADOW_SIZE);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
    sm->state = 1;
    return 1;
}

/* A shadow map texture: GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP, one float channel, nearest, clamped. */
static GLuint Model_ShadowTexture(GLenum target)
{
    GLenum format = g_shadowMaps.format == GL_R32F ? GL_RED : GL_LUMINANCE;
    GLuint id;
    int i;

    glGenTextures(1, &id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, id);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target == GL_TEXTURE_2D) {
        glTexImage2D(GL_TEXTURE_2D, 0, g_shadowMaps.format, MODEL_SHADOW_SIZE, MODEL_SHADOW_SIZE, 0, format, GL_FLOAT,
                     NULL);
    } else {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        for (i = 0; i < 6; i++) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, g_shadowMaps.format, MODEL_SHADOW_SIZE,
                         MODEL_SHADOW_SIZE, 0, format, GL_FLOAT, NULL);
        }
    }
    glBindTexture(target, 0);
    return id;
}

/*
 * Draws the meshes of node index and its children into a shadow map (Ikemen
 * GO's drawNodeShadow): not the "trans" nodes, the nodes without depth test
 * or depth writes, nor those with a castShadow extra off.
 */
static void Model_DrawNodeShadow(Model *mdl, int index, int blended)
{
    const ShadowProgram *prog = &g_shadowProgram;
    const ModelNode *n = &mdl->nodes[index];
    const ModelMesh *mesh;
    int i;

    for (i = 0; i < n->child_count; i++) {
        Model_DrawNodeShadow(mdl, n->children[i], blended);
    }
    if (n->mesh == -1 || !n->visible) {
        return;
    }
    if (n->trans != MODEL_TRANS_NONE || !n->ztest || !n->zwrite || !n->cast_shadow) {
        return;
    }
    mesh = &mdl->meshes[n->mesh];
    for (i = 0; i < mesh->prim_count; i++) {
        ModelPrim *p = &mdl->prims[mesh->first_prim + i];
        const ModelMaterial *mat;

        if (p->material == -1 || p->index_count == 0) {
            continue;
        }
        mat = &mdl->materials[p->material];
        if ((mat->alpha_mode == MODEL_ALPHA_BLEND) != blended) {
            continue;
        }
        if (mat->base_color[3] == 0.0f && mat->alpha_mode == MODEL_ALPHA_BLEND) {
            continue;
        }
        Model_Morph(p, n);
        glFrontFace(Mat4_Det3(n->world) < 0 ? GL_CW : GL_CCW);
        if (mat->double_sided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        Model_SetAttributes(p);
        glUniformMatrix4fv(prog->model, 1, GL_FALSE, n->world);
        if (n->skin != -1 && (p->attribs & MODEL_ATTR_SKIN)) {
            const ModelSkin *skin = &mdl->skins[n->skin];
            int count = skin->joint_count < MODEL_MAX_JOINTS ? skin->joint_count : MODEL_MAX_JOINTS;
            glUniform1i(prog->skinned, 1);
            glUniform4fv(prog->joints, count * 3, skin->matrices);
        } else {
            glUniform1i(prog->skinned, 0);
        }
        glUniform1i(prog->enable_alpha, mat->alpha_mode == MODEL_ALPHA_BLEND);
        glUniform1f(prog->alpha_threshold, mat->alpha_cutoff);
        glUniform4fv(prog->base_color, 1, mat->base_color);
        glUniform1i(prog->use_texture, Model_BindTexture(mdl, &mat->base, MODEL_UNIT_BASE, prog->tex_transform));
        glDrawElements(p->mode, p->index_count, GL_UNSIGNED_INT, (const void *)(size_t)p->index_offset);
    }
}

/*
 * Draws the shadow maps of the lights of the model's first scene (Ikemen
 * GO's Model.drawShadow, done before its layer -1), cast by the nodes of
 * that scene. Nothing with [Video] EnableModelShadow = 0, without lights or
 * when the hardware cannot.
 */
void Model_DrawShadows(Model *mdl)
{
    ShadowMaps *sm = &g_shadowMaps;
    const ShadowProgram *prog = &g_shadowProgram;
    const ModelScene *sc;
    float ident[16], params[MODEL_SHADOW_PARAMS], m[16];
    GLint prev_fbo;
    int i, face, pass, k;

    sm->drawn = NULL;
    if (!g_modelShadows || mdl == NULL || mdl->vbo == 0 || mdl->scene_count == 0 || g_renderMode != RENDER_OPENGL) {
        return;
    }
    sc = &mdl->scenes[0];
    if (sc->light_count == 0 || !Model_ShadowSetup()) {
        return;
    }
    Mat4_Identity(ident);
    for (i = 0; i < sc->node_count; i++) {
        Model_WorldTransform(mdl, sc->nodes[i], ident);
        Model_SkinMatrices(mdl, sc->nodes[i]);
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prev_fbo);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT |
                 GL_VIEWPORT_BIT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, sm->fbo);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, sm->depth);
    glViewport(0, 0, MODEL_SHADOW_SIZE, MODEL_SHADOW_SIZE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glUseProgram(prog->program);
    g_glCurrentProgram = prog->program;
    glUniform1i(prog->tex, MODEL_UNIT_BASE);
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    for (i = 0; i < sc->light_count && i < MODEL_MAX_LIGHTS && sm->state == 1; i++) {
        const ModelNode *ln = &mdl->nodes[sc->light_nodes[i]];
        const ModelLight *l = &mdl->lights[ln->light];
        int point = l->type == MODEL_LIGHT_POINT;
        GLuint *tex = point ? &sm->cube[i] : &sm->map2d[i];

        if (*tex == 0) {
            *tex = Model_ShadowTexture(point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
        }
        Model_ShadowParams(mdl, ln, params);
        glUniform1i(prog->directional, l->type == MODEL_LIGHT_DIRECTIONAL);
        glUniform3f(prog->light_position, ln->world[12], ln->world[13], ln->world[14]);
        glUniform1f(prog->shadow_map_far, params[MODEL_SHADOW_FAR]);
        for (face = 0; face < (point ? 6 : 1); face++) {
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                      point ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D, *tex, 0);
            if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
                Log_Printf("Model: shadow framebuffer incomplete, no shadows\n");
                sm->state = -1;
                break;
            }
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            Model_LightMatrix(mdl, ln, params, face, m);
            glUniformMatrix4fv(prog->light_matrix, 1, GL_FALSE, m);
            for (pass = 0; pass < 2; pass++) {
                for (k = 0; k < sc->node_count; k++) {
                    Model_DrawNodeShadow(mdl, sc->nodes[k], pass);
                }
            }
        }
    }
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, 0, 0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prev_fbo);
    for (i = 0; i < MODEL_LOC_COUNT; i++) {
        glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    g_glCurrentProgram = 0;
    glPopAttrib();
    if (sm->state == 1) {
        sm->drawn = mdl;
    }
}

/* ---- environment maps (image based lighting) ---- */



/* One RGBE pixel as floats (as Radiance and mdouchement/hdr decode it). */
static void Model_Rgbe(const unsigned char *rgbe, float *out)
{
    if (rgbe[3] == 0) {
        out[0] = out[1] = out[2] = 0.0f;
    } else {
        float f = (float)ldexp(1.0, rgbe[3] - (128 + 8));
        out[0] = rgbe[0] * f;
        out[1] = rgbe[1] * f;
        out[2] = rgbe[2] * f;
    }
    out[3] = 1.0f;
}

/*
 * Reads a Radiance HDR (.hdr) image ("-Y h +X w", flat or run length
 * encoded scanlines) as RGBA floats, top row first. Returns NULL on errors.
 */
static float *Model_LoadHdr(const char *path, int *w_out, int *h_out)
{
    unsigned char *data, *p, *end, *line = NULL;
    float *pixels = NULL;
    FILE *fp;
    long size;
    int w, h, x, y, c;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = size > 0 ? malloc((size_t)size + 1) : NULL;
    if (data == NULL || fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    data[size] = '\0';
    end = data + size;
    if (size < 2 || data[0] != '#' || data[1] != '?') {
        goto fail;
    }
    /* the header ends with an empty line, then comes the resolution line */
    for (p = data; p + 1 < end && !(p[0] == '\n' && p[1] == '\n'); p++) {
    }
    p += 2;
    if (p >= end || sscanf((const char *)p, "-Y %d +X %d", &h, &w) != 2 || w <= 0 || h <= 0) {
        goto fail;
    }
    while (p < end && *p != '\n') {
        p++;
    }
    p++;
    pixels = malloc((size_t)w * h * 4 * sizeof(float));
    line = malloc((size_t)w * 4);
    if (pixels == NULL || line == NULL) {
        goto fail;
    }
    for (y = 0; y < h; y++) {
        if (w >= 8 && w < 0x8000 && p + 4 <= end && p[0] == 2 && p[1] == 2 && !(p[2] & 0x80)) {
            /* run length encoded, each channel apart */
            if (((p[2] << 8) | p[3]) != w) {
                goto fail;
            }
            p += 4;
            for (c = 0; c < 4; c++) {
                x = 0;
                while (x < w) {
                    int count;
                    if (p >= end) {
                        goto fail;
                    }
                    count = *p++;
                    if (count > 128) {
                        count -= 128;
                        if (p >= end || x + count > w) {
                            goto fail;
                        }
                        while (count-- > 0) {
                            line[(x++) * 4 + c] = *p;
                        }
                        p++;
                    } else {
                        if (count == 0 || p + count > end || x + count > w) {
                            goto fail;
                        }
                        while (count-- > 0) {
                            line[(x++) * 4 + c] = *p++;
                        }
                    }
                }
            }
        } else {
            if (p + (size_t)w * 4 > end) {
                goto fail;
            }
            memcpy(line, p, (size_t)w * 4);
            p += (size_t)w * 4;
        }
        for (x = 0; x < w; x++) {
            Model_Rgbe(line + x * 4, pixels + ((size_t)y * w + x) * 4);
        }
    }
    free(line);
    free(data);
    *w_out = w;
    *h_out = h;
    return pixels;

fail:
    free(line);
    free(pixels);
    free(data);
    return NULL;
}

/* Compiles and links a program of the environment passes (attribute 0: VertCoord). */
static GLuint Model_EnvProgram(const char *frag)
{
    const char *src[2] = { g_shader_env_vert, frag };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    char log[0x800];
    GLuint program, shader;
    GLint ok;
    int i;

    program = glCreateProgram();
    for (i = 0; i < 2; i++) {
        shader = glCreateShader(types[i]);
        glShaderSource(shader, 1, &src[i], NULL);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(shader, sizeof(log), NULL, log);
            Log_Printf("Model: environment shader compile error:\n%s\n", log);
            glDeleteShader(shader);
            glDeleteProgram(program);
            return 0;
        }
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glBindAttribLocation(program, 0, "VertCoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        Log_Printf("Model: environment shader link error:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

/* A float RGBA cube map of size x size, with a mip chain when mipmapped (Ikemen GO's newCubeMapTexture). */
static GLuint Model_EnvCube(int size, int mipmapped)
{
    GLuint id;
    int i;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    for (i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT, NULL);
    }
    if (mipmapped) {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmapEXT(GL_TEXTURE_CUBE_MAP);
    } else {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

/* Sets the uniforms of a filtering pass (Ikemen GO's RenderFilteredCubeMap / RenderLUT). */
static void Model_EnvFilterUniforms(GLuint prog, int distribution, int samples, int width, float roughness, int lut)
{
    glUniform1i(glGetUniformLocation(prog, "cubeMap"), 0);
    glUniform1i(glGetUniformLocation(prog, "sampleCount"), samples);
    glUniform1i(glGetUniformLocation(prog, "distribution"), distribution);
    glUniform1i(glGetUniformLocation(prog, "width"), width);
    glUniform1f(glGetUniformLocation(prog, "roughness"), roughness);
    glUniform1f(glGetUniformLocation(prog, "intensityScale"), 1.0f);
    glUniform1i(glGetUniformLocation(prog, "isLUT"), lut);
}

/* Draws the 6 faces of mip level `level` of cube map tex with the current program. */
static void Model_EnvFaces(GLuint prog, GLuint tex, int level, int size)
{
    int i;

    glViewport(0, 0, size, size);
    for (i = 0; i < 6; i++) {
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tex,
                                  level);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1i(glGetUniformLocation(prog, "currentFace"), i);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

/*
 * Gives a model the environment map of Radiance HDR panorama path (Ikemen
 * GO's loadEnvironment): the panorama goes to a 256 cube map, filtered
 * into the Lambertian cube map, the GGX cube map (one mip level per
 * roughness step) and the BRDF table. intensity scales its light. Returns 0
 * (logged) when it cannot be loaded or the hardware cannot do it.
 */
int Model_LoadEnvironment(Model *mdl, const char *path, float intensity)
{
    static const float quad[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    ModelEnv *env;
    float *img, *flipped;
    GLuint panorama, source, fbo, vbo, prog_pano, prog_filter;
    GLint prev_fbo, align;
    int w, h, i, n;

    if (mdl == NULL || !Model_IblSupported()) {
        Log_Printf("Model: environment maps need OpenGL 3.0\n");
        return 0;
    }
    img = Model_LoadHdr(path, &w, &h);
    if (img == NULL) {
        Log_Printf("Model: can't read environment %s (only Radiance .hdr is supported)\n", path);
        return 0;
    }
    /* as Ikemen GO, the pixels go up in reverse order: bottom row first, right to left */
    n = w * h;
    flipped = malloc((size_t)n * 4 * sizeof(float));
    if (flipped == NULL) {
        free(img);
        return 0;
    }
    for (i = 0; i < n; i++) {
        memcpy(flipped + i * 4, img + (size_t)(n - 1 - i) * 4, 4 * sizeof(float));
    }
    free(img);
    prog_pano = Model_EnvProgram(g_shader_env_panorama_frag);
    prog_filter = Model_EnvProgram(g_shader_env_filter_frag);
    env = calloc(1, sizeof(ModelEnv));
    if (prog_pano == 0 || prog_filter == 0 || env == NULL) {
        free(flipped);
        free(env);
        if (prog_pano != 0) {
            glDeleteProgram(prog_pano);
        }
        if (prog_filter != 0) {
            glDeleteProgram(prog_filter);
        }
        return 0;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prev_fbo);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT |
                 GL_VIEWPORT_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);

    /* the panorama (Ikemen GO's newHDRTexture) */
    glGenTextures(1, &panorama);
    glBindTexture(GL_TEXTURE_2D, panorama);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, flipped);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    free(flipped);

    source = Model_EnvCube(MODEL_ENV_SIZE, 1);
    env->lambertian = Model_EnvCube(MODEL_ENV_SIZE, 0);
    env->ggx = Model_EnvCube(MODEL_ENV_SIZE, 1);
    glGenTextures(1, &env->lut);
    glBindTexture(GL_TEXTURE_2D, env->lut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, MODEL_ENV_LUT_SIZE, MODEL_ENV_LUT_SIZE, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    /* the panorama on the cube, then its mip chain */
    glUseProgram(prog_pano);
    glUniform1i(glGetUniformLocation(prog_pano, "panorama"), 0);
    glBindTexture(GL_TEXTURE_2D, panorama);
    Model_EnvFaces(prog_pano, source, 0, MODEL_ENV_SIZE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, source);
    glGenerateMipmapEXT(GL_TEXTURE_CUBE_MAP);

    /* the Lambertian cube map, the GGX levels (roughness 0 to 1), the BRDF table */
    glUseProgram(prog_filter);
    Model_EnvFilterUniforms(prog_filter, 0, MODEL_ENV_LAMBERTIAN_SAMPLES, MODEL_ENV_SIZE, 0.0f, 0);
    Model_EnvFaces(prog_filter, env->lambertian, 0, MODEL_ENV_SIZE);
    env->mip_count = (int)floor(log((double)MODEL_ENV_SIZE) / log(2.0)) + 1 - MODEL_ENV_LOWEST_MIP;
    for (i = 0; i < env->mip_count; i++) {
        Model_EnvFilterUniforms(prog_filter, 1, MODEL_ENV_GGX_SAMPLES, MODEL_ENV_SIZE,
                                (float)i / (float)(env->mip_count - 1), 0);
        Model_EnvFaces(prog_filter, env->ggx, i, MODEL_ENV_SIZE >> i);
    }
    Model_EnvFilterUniforms(prog_filter, 1, MODEL_ENV_LUT_SAMPLES, MODEL_ENV_LUT_SIZE, 0.0f, 1);
    glUniform1i(glGetUniformLocation(prog_filter, "currentFace"), 0);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, env->lut, 0);
    glViewport(0, 0, MODEL_ENV_LUT_SIZE, MODEL_ENV_LUT_SIZE);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, 0, 0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prev_fbo);
    glDeleteFramebuffersEXT(1, &fbo);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDeleteTextures(1, &panorama);
    glDeleteTextures(1, &source);
    glUseProgram(0);
    g_glCurrentProgram = 0;
    glDeleteProgram(prog_pano);
    glDeleteProgram(prog_filter);
    glPopAttrib();

    env->intensity = intensity;
    mdl->env = env;
    return 1;
}

/* Frees a model's environment map. */
static void Model_FreeEnvironment(ModelEnv *env)
{
    if (env == NULL) {
        return;
    }
    glDeleteTextures(1, &env->lambertian);
    glDeleteTextures(1, &env->ggx);
    glDeleteTextures(1, &env->lut);
    free(env);
}

/* Binds a model's environment maps (or unbinds, for NULL) and sets their uniforms. */
static void Model_BindEnvironment(const ModelEnv *env)
{
    const ModelProgram *prog = &g_modelProgram;
    float rx[9], ry[9], rot[9];
    int c, r;

    if (!g_modelIbl) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + MODEL_UNIT_ENV_LAMBERTIAN);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env != NULL ? env->lambertian : 0);
    glActiveTexture(GL_TEXTURE0 + MODEL_UNIT_ENV_GGX);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env != NULL ? env->ggx : 0);
    glActiveTexture(GL_TEXTURE0 + MODEL_UNIT_ENV_LUT);
    glBindTexture(GL_TEXTURE_2D, env != NULL ? env->lut : 0);
    if (env == NULL) {
        return;
    }
    glUniform1i(prog->env_lambertian, MODEL_UNIT_ENV_LAMBERTIAN);
    glUniform1i(prog->env_ggx, MODEL_UNIT_ENV_GGX);
    glUniform1i(prog->env_lut, MODEL_UNIT_ENV_LUT);
    glUniform1f(prog->env_intensity, env->intensity);
    glUniform1i(prog->env_mip_count, env->mip_count);
    /* mathgl's Rotate3DX(pi) * Rotate3DY(pi / 2), column-major */
    memset(rx, 0, sizeof(rx));
    rx[0] = 1.0f;
    rx[4] = (float)cos(MODEL_PI);
    rx[5] = (float)sin(MODEL_PI);
    rx[7] = (float)-sin(MODEL_PI);
    rx[8] = (float)cos(MODEL_PI);
    memset(ry, 0, sizeof(ry));
    ry[0] = (float)cos(MODEL_PI / 2);
    ry[2] = (float)-sin(MODEL_PI / 2);
    ry[4] = 1.0f;
    ry[6] = (float)sin(MODEL_PI / 2);
    ry[8] = (float)cos(MODEL_PI / 2);
    for (c = 0; c < 3; c++) {
        for (r = 0; r < 3; r++) {
            rot[c * 3 + r] = rx[r] * ry[c * 3] + rx[3 + r] * ry[c * 3 + 1] + rx[6 + r] * ry[c * 3 + 2];
        }
    }
    glUniformMatrix3fv(prog->env_rotation, 1, GL_FALSE, rot);
}

/* Binds (or unbinds, for NULL) the shadow maps to their units. */
static void Model_BindShadowMaps(const ShadowMaps *sm)
{
    int i;

    for (i = 0; i < MODEL_MAX_LIGHTS; i++) {
        glActiveTexture(GL_TEXTURE0 + MODEL_UNIT_SHADOW_2D + i);
        glBindTexture(GL_TEXTURE_2D, sm != NULL ? sm->map2d[i] : 0);
        glActiveTexture(GL_TEXTURE0 + MODEL_UNIT_SHADOW_CUBE + i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, sm != NULL ? sm->cube[i] : 0);
    }
}

/*
 * Draws the nodes of layer `layer` of scene `scene` (nodes without a
 * layerNumber are on default_layer) with the screen projection proj and the
 * view matrix view, over a cleared depth buffer (Ikemen GO's Model.draw).
 * The scene is lit by its lights; without any it is drawn unlit. Only with
 * the OpenGL renderer.
 */
void Model_Draw(Model *mdl, int scene, int layer, int default_layer, const float *proj, const float *view,
                const ModelFx *fx, float outline)
{
    const ModelProgram *prog = &g_modelProgram;
    const ModelScene *sc;
    float ident[16], p[16], flip[16], inv_view[16];
    int flipped, unlit;
    int i, pass;

    if (mdl == NULL || mdl->vbo == 0 || scene < 0 || scene >= mdl->scene_count || g_renderMode != RENDER_OPENGL) {
        return;
    }
    if (g_modelProgramState == 0) {
        g_modelProgramState = Model_BuildProgram() ? 1 : -1;
    }
    if (g_modelProgramState != 1) {
        return;
    }
    sc = &mdl->scenes[scene];
    Mat4_Identity(ident);
    for (i = 0; i < sc->node_count; i++) {
        Model_WorldTransform(mdl, sc->nodes[i], ident);
        Model_SkinMatrices(mdl, sc->nodes[i]);
    }
    /* texture transforms may be animated (Ikemen GO's calculateTextureTransform) */
    for (i = 0; i < mdl->material_count; i++) {
        ModelMaterial *mat = &mdl->materials[i];
        Model_TexTransform(&mat->base);
        Model_TexTransform(&mat->normal);
        Model_TexTransform(&mat->metal_rough);
        Model_TexTransform(&mat->occlusion);
        Model_TexTransform(&mat->emissive);
    }

    /* A framebuffer target is upside down (Gl_SetupView): flip y, and with it the winding. */
    flipped = g_glBoundFramebuffer != 0;
    memcpy(p, proj, sizeof(p));
    if (flipped) {
        Mat4_Scale(flip, 1.0f, -1.0f, 1.0f);
        Mat4_Mul(p, flip, p);
    }
    Mat4_Invert(inv_view, view);

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_1D);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glUseProgram(prog->program);
    g_glCurrentProgram = prog->program;
    glUniform1i(prog->tex, MODEL_UNIT_BASE);
    glUniform1i(prog->normal_map, MODEL_UNIT_NORMAL);
    glUniform1i(prog->metal_rough_map, MODEL_UNIT_METAL_ROUGH);
    glUniform1i(prog->occlusion_map, MODEL_UNIT_OCCLUSION);
    glUniform1i(prog->emission_map, MODEL_UNIT_EMISSION);
    glUniformMatrix4fv(prog->projection, 1, GL_FALSE, p);
    glUniformMatrix4fv(prog->view, 1, GL_FALSE, view);
    /* the camera in model space: where the view puts the eye */
    glUniform3f(prog->camera_position, inv_view[12], inv_view[13], inv_view[14]);
    glUniform3fv(prog->add, 1, fx->add);
    glUniform3fv(prog->mul, 1, fx->mul);
    glUniform1f(prog->colorbal, fx->colorbal);
    glUniform1i(prog->invertall, fx->invertall != 0);
    /* unlit without lights and without an environment map, as Ikemen GO */
    unlit = Model_SetLights(mdl, sc) == 0 && mdl->env == NULL;
    glUniform1f(prog->env_intensity, 0.0f);
    Model_BindEnvironment(mdl->env);
    for (i = 0; i < MODEL_MAX_LIGHTS; i++) {
        glUniform1i(prog->shadow_maps[i], MODEL_UNIT_SHADOW_2D + i);
        glUniform1i(prog->shadow_cubes[i], MODEL_UNIT_SHADOW_CUBE + i);
    }
    glUniform1i(prog->use_shadow_map, g_shadowMaps.drawn == mdl);
    glUniform1f(prog->mesh_outline, 0.0f);
    if (g_shadowMaps.drawn == mdl) {
        Model_BindShadowMaps(&g_shadowMaps);
    }
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < sc->node_count; i++) {
            Model_DrawNode(mdl, sc->nodes[i], layer, default_layer, pass, flipped, unlit, outline);
        }
    }
    for (i = 0; i < MODEL_LOC_COUNT; i++) {
        glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (g_shadowMaps.drawn == mdl) {
        Model_BindShadowMaps(NULL);
    }
    if (mdl->env != NULL) {
        Model_BindEnvironment(NULL);
    }
    for (i = MODEL_UNIT_EMISSION; i >= MODEL_UNIT_BASE; i--) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glUseProgram(0);
    g_glCurrentProgram = 0;
    glPopAttrib();
}
