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
#include <math.h>

/* Vertex attribute locations of the model program. */
enum {
    MODEL_LOC_POSITION = 0,
    MODEL_LOC_UV = 1,
    MODEL_LOC_COLOR = 2,
    MODEL_LOC_JOINTS = 3,
    MODEL_LOC_WEIGHTS = 4,
    MODEL_LOC_NORMAL = 5,
    MODEL_LOC_TANGENT = 6,
    MODEL_LOC_COUNT = 7
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
} ModelProgram;

static ModelProgram g_modelProgram;
static int g_modelProgramState;     /* 0 not built yet, 1 built, -1 failed */

/* src/shaders/model.vert and model.frag (CMakeLists.txt embeds them). */
#include "model.vert.h"
#include "model.frag.h"

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

/* ---- extras ---- */

/*
 * Looks up `key` in the JSON object of an extras block. On success the value
 * goes to out: a string unquoted (simple escapes only), anything else as its
 * JSON text; *is_string tells which.
 */
static int Model_GetExtra(const char *json, const char *key, char *out, int size, int *is_string)
{
    const char *p = json;
    size_t key_len = strlen(key);

    if (p == NULL) {
        return 0;
    }
    while (*p != '\0' && *p != '{') {
        p++;
    }
    if (*p != '{') {
        return 0;
    }
    p++;
    for (;;) {
        const char *k, *v;
        size_t k_len;
        int depth, n;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') {
            p++;
        }
        if (*p != '"') {
            return 0;
        }
        k = ++p;
        while (*p != '\0' && *p != '"') {
            if (*p == '\\' && p[1] != '\0') {
                p++;
            }
            p++;
        }
        if (*p != '"') {
            return 0;
        }
        k_len = (size_t)(p - k);
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p != ':') {
            return 0;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        v = p;
        if (*p == '"') {
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }
            if (*p != '"') {
                return 0;
            }
            p++;
        } else {
            depth = 0;
            while (*p != '\0') {
                if (*p == '{' || *p == '[') {
                    depth++;
                } else if (*p == '}' || *p == ']') {
                    if (depth == 0) {
                        break;
                    }
                    depth--;
                } else if (*p == ',' && depth == 0) {
                    break;
                } else if (*p == '"') {
                    p++;
                    while (*p != '\0' && *p != '"') {
                        if (*p == '\\' && p[1] != '\0') {
                            p++;
                        }
                        p++;
                    }
                    if (*p == '\0') {
                        return 0;
                    }
                }
                p++;
            }
        }
        if (k_len == key_len && strncmp(k, key, key_len) == 0) {
            if (*v == '"') {
                const char *s = v + 1;
                n = 0;
                while (*s != '"' && n < size - 1) {
                    if (*s == '\\') {
                        s++;
                    }
                    out[n++] = *s++;
                }
                out[n] = '\0';
                *is_string = 1;
            } else {
                n = (int)(p - v);
                while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r' || v[n - 1] == '\n')) {
                    n--;
                }
                if (n > size - 1) {
                    n = size - 1;
                }
                memcpy(out, v, n);
                out[n] = '\0';
                *is_string = 0;
            }
            return 1;
        }
        if (*p != ',') {
            return 0;
        }
    }
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

/* Appends the vertex attribute of accessor acc as `comps` floats per vertex (pad: the value of missing comps). */
static int Model_AddAttribute(Model *mdl, unsigned int *capacity, const cgltf_accessor *acc, int count, int comps,
                              float pad)
{
    int n = (int)cgltf_num_components(acc->type);
    float v[16];
    float *buf;
    int i, k;

    buf = malloc((size_t)count * comps * sizeof(float));
    if (buf == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        memset(v, 0, sizeof(v));
        cgltf_accessor_read_float(acc, i, v, n);
        for (k = 0; k < comps; k++) {
            buf[i * comps + k] = k < n ? v[k] : pad;
        }
    }
    k = Model_AddVertexData(mdl, capacity, buf, (unsigned int)(count * comps * sizeof(float)));
    free(buf);
    return k;
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
    acc = Model_FindAttribute(p, cgltf_attribute_type_joints, 0);
    if (acc != NULL) {
        const cgltf_accessor *w = Model_FindAttribute(p, cgltf_attribute_type_weights, 0);
        if (w == NULL) {
            MatchLog_Printf(3, "Primitive attribute JOINTS_0 is specified but WEIGHTS_0 is not specified.\n");
            return 0;
        }
        prim->attribs |= MODEL_ATTR_SKIN;
        if (!Model_AddAttribute(mdl, vcap, acc, prim->vertex_count, 4, 0.0f) ||
            !Model_AddAttribute(mdl, vcap, w, prim->vertex_count, 4, 0.0f)) {
            return 0;
        }
    }
    if (p->targets_count > 0) {
        MatchLog_Printf(3, "Model: morph targets are not supported yet, ignored\n");
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

/* A texture of a material and its KHR_texture_transform: translation * rotation (by -angle) * scale. */
static void Model_LoadTexRef(ModelTexRef *ref, const cgltf_data *data, const cgltf_texture_view *view)
{
    memset(ref->transform, 0, sizeof(ref->transform));
    ref->transform[0] = ref->transform[4] = ref->transform[8] = 1.0f;
    ref->texture = -1;
    if (view->texture == NULL) {
        return;
    }
    ref->texture = (int)(view->texture - data->textures);
    if (view->has_transform) {
        const cgltf_texture_transform *t = &view->transform;
        float c = (float)cos(t->rotation), s = (float)sin(t->rotation);
        ref->transform[0] = c * t->scale[0];
        ref->transform[1] = -s * t->scale[0];
        ref->transform[3] = s * t->scale[1];
        ref->transform[4] = c * t->scale[1];
        ref->transform[6] = t->offset[0];
        ref->transform[7] = t->offset[1];
    }
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
        MatchLog_Printf(3, "Model: can't read image %s\n", t->image->uri != NULL ? t->image->uri : "(embedded)");
        return 0;
    }
    pixels = Png_DecodeRGBA(bytes, size, &w, &h);
    free(bytes);
    if (pixels == NULL) {
        MatchLog_Printf(3, "Model: unsupported image %s (only PNG is supported)\n",
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
        memcpy(node->local, n->matrix, sizeof(node->local));
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
    if (n->extras.data != NULL) {
        const char *json = n->extras.data;
        char cast[16];
        if (Model_GetExtra(json, "castShadow", cast, sizeof(cast), &is_string) && is_string &&
            (strcmp(cast, "0") == 0 || strcmp(cast, "false") == 0)) {
            node->cast_shadow = 0;
        }
        Model_ShadowExtras(json, node->shadow);
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

static int Model_LoadAnim(ModelAnim *anim, const cgltf_data *data, const cgltf_animation *a)
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
        if (c->target_node == NULL || c->sampler == NULL) {
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
        default:
            continue;
        }
        mc->node = (int)(c->target_node - data->nodes);
        mc->sampler = (int)(c->sampler - a->samplers);
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
        MatchLog_Printf(3, "Model: a skin has %i joints, only the first %i are used\n", skin->joint_count,
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
    Model *mdl;
    cgltf_size i, j;

    memset(&options, 0, sizeof(options));
    r = cgltf_parse_file(&options, path, &data);
    if (r == cgltf_result_success) {
        r = cgltf_load_buffers(&options, data, path);
    }
    if (r != cgltf_result_success) {
        MatchLog_Printf(3, "Can't load model %s (cgltf error %i)\n", path, (int)r);
        cgltf_free(data);
        return NULL;
    }
    Path_Split(path, dir, NULL);
    mdl = calloc(1, sizeof(Model));
    if (mdl == NULL) {
        cgltf_free(data);
        return NULL;
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
        if (!Model_LoadAnim(&mdl->anims[i], data, &data->animations[i])) {
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
    cgltf_free(data);
    return mdl;

fail:
    MatchLog_Printf(3, "Error loading model %s\n", path);
    cgltf_free(data);
    Model_Free(mdl);
    return NULL;
}

void Model_Free(Model *mdl)
{
    int i, k;

    if (mdl == NULL) {
        return;
    }
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
    }
    free(mdl->nodes);
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
            float v[4];
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
            }
            node->changed = 1;
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
    if (n->has_matrix && n->animated == 0) {
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
                                                          "weights_0", "normalIn", "tangentIn" };
    static const char *const fields[] = { "direction",    "range",        "color", "intensity",  "position",
                                          "innerConeCos", "outerConeCos", "type",  "shadowBias", "shadowMapFar" };
    const char *src[2] = { g_shader_model_vert, g_shader_model_frag };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    ModelProgram *p = &g_modelProgram;
    GLuint shaders[2];
    char log[0x800];
    char name[64];
    GLint ok;
    int i, k;

    p->program = glCreateProgram();
    for (i = 0; i < 2; i++) {
        shaders[i] = glCreateShader(types[i]);
        glShaderSource(shaders[i], 1, &src[i], NULL);
        glCompileShader(shaders[i]);
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(shaders[i], sizeof(log), NULL, log);
            MatchLog_Printf(3, "Model: shader compile error:\n%s\n", log);
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
        MatchLog_Printf(3, "Model: shader link error:\n%s\n", log);
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
    }
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
static void Model_DrawNode(Model *mdl, int index, int layer, int default_layer, int blended, int flipped, int unlit)
{
    const ModelProgram *prog = &g_modelProgram;
    ModelNode *n = &mdl->nodes[index];
    const ModelMesh *mesh;
    int node_layer = n->layer != MODEL_LAYER_DEFAULT ? n->layer : default_layer;
    int i;

    for (i = 0; i < n->child_count; i++) {
        Model_DrawNode(mdl, n->children[i], layer, default_layer, blended, flipped, unlit);
    }
    if (n->mesh == -1 || !n->visible || node_layer != layer) {
        return;
    }
    mesh = &mdl->meshes[n->mesh];
    for (i = 0; i < mesh->prim_count; i++) {
        const ModelPrim *p = &mdl->prims[mesh->first_prim + i];
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
    }
}

/* The parameters of the lights of a scene (the first MODEL_MAX_LIGHTS); returns how many there are. */
static int Model_SetLights(const Model *mdl, const ModelScene *sc)
{
    const ModelProgram *prog = &g_modelProgram;
    int i;

    for (i = 0; i < MODEL_MAX_LIGHTS; i++) {
        glUniform3f(prog->lights[i].color, 0.0f, 0.0f, 0.0f);
    }
    for (i = 0; i < sc->light_count && i < MODEL_MAX_LIGHTS; i++) {
        const ModelNode *n = &mdl->nodes[sc->light_nodes[i]];
        const ModelLight *l = &mdl->lights[n->light];
        const ModelLightLocs *loc = &prog->lights[i];
        float far_z = 50.0f, bias = 0.02f;

        /* the light's extras, then the node's, override the shadow map defaults */
        if (l->shadow[MODEL_SHADOW_FAR] != 0.0f) {
            far_z = l->shadow[MODEL_SHADOW_FAR];
        }
        if (l->shadow[MODEL_SHADOW_BIAS] != 0.0f) {
            bias = l->shadow[MODEL_SHADOW_BIAS];
        }
        if (n->shadow[MODEL_SHADOW_FAR] != 0.0f) {
            far_z = n->shadow[MODEL_SHADOW_FAR];
        }
        if (n->shadow[MODEL_SHADOW_BIAS] != 0.0f) {
            bias = n->shadow[MODEL_SHADOW_BIAS];
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
    return sc->light_count;
}

/*
 * Draws the nodes of layer `layer` of scene `scene` (nodes without a
 * layerNumber are on default_layer) with the screen projection proj and the
 * view matrix view, over a cleared depth buffer (Ikemen GO's Model.draw).
 * The scene is lit by its lights; without any it is drawn unlit. Only with
 * the OpenGL renderer.
 */
void Model_Draw(Model *mdl, int scene, int layer, int default_layer, const float *proj, const float *view,
                const ModelFx *fx)
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
    unlit = Model_SetLights(mdl, sc) == 0;
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < sc->node_count; i++) {
            Model_DrawNode(mdl, sc->nodes[i], layer, default_layer, pass, flipped, unlit);
        }
    }
    for (i = 0; i < MODEL_LOC_COUNT; i++) {
        glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (i = MODEL_UNIT_EMISSION; i >= MODEL_UNIT_BASE; i--) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glUseProgram(0);
    g_glCurrentProgram = 0;
    glPopAttrib();
}
