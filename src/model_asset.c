/* Port addition: CPU-only glTF image decoding; no GL resource ownership. */
#include "mugen.h"
#ifdef MUGEN_MODEL3D
#include <cgltf.h>
#include <stb_image.h>
#include "model_backend.h"

/* Port fix: reject oversized/invalid source graphs before recursive evaluation,
 * count narrowing, expanded vertex allocations or GPU work. */
int ModelAsset_Validate(const cgltf_data *data)
{
    cgltf_size i, j, bytes = 0, primitives = 0;
    if (data->nodes_count > 65536 || data->meshes_count > 65536 || data->materials_count > 65536 ||
        data->textures_count > 65536 || data->scenes_count > 65536 || data->animations_count > 65536 ||
        data->skins_count > 65536 || data->lights_count > 65536) return 0;
    if (data->nodes_count > MODEL_CPU_BUDGET / sizeof(ModelNode) ||
        data->accessors_count > 1048576 || data->buffer_views_count > 1048576 ||
        data->buffers_count > 65536) return 0;
    for (i = 0; i < data->buffers_count; ++i) {
        if (data->buffers[i].size > MODEL_CPU_BUDGET - bytes) return 0;
        bytes += data->buffers[i].size;
    }
    for (i = 0; i < data->accessors_count; ++i)
        if (data->accessors[i].count > MODEL_CPU_BUDGET / 64u) return 0;
    for (i = 0; i < data->nodes_count; ++i) {
        const cgltf_node *node = &data->nodes[i];
        int depth = 0;
        while (node != NULL) {
            if (++depth > 128) return 0; /* cycle or excessive hierarchy depth */
            node = node->parent;
        }
    }
    for (i = 0; i < data->meshes_count; ++i) {
        if (data->meshes[i].primitives_count > 65536 - primitives) return 0;
        primitives += data->meshes[i].primitives_count;
        if (data->meshes[i].weights_count > MODEL_MAX_WEIGHTS) return 0;
        for (j = 0; j < data->meshes[i].primitives_count; ++j)
            if (data->meshes[i].primitives[j].targets_count > MODEL_MAX_WEIGHTS) return 0;
    }
    for (i = 0; i < data->skins_count; ++i)
        if (data->skins[i].joints_count > MODEL_MAX_JOINTS) return 0;
    return 1;
}

/* All four joint lookups are evaluated by the shader, including zero-weight
 * lanes. Reject an out-of-palette index rather than indexing uniform storage. */
int ModelAsset_ValidateSkins(const cgltf_data *data)
{
    cgltf_size n, p, a, v;
    for (n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (node->skin == NULL || node->mesh == NULL) continue;
        for (p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive *prim = &node->mesh->primitives[p];
            for (a = 0; a < prim->attributes_count; ++a) {
                const cgltf_attribute *attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_joints && attr->index == 0) {
                    const cgltf_accessor *acc = attr->data;
                    if (acc->type != cgltf_type_vec4) return 0;
                    for (v = 0; v < acc->count; ++v) {
                        cgltf_uint joints[4];
                        int k;
                        if (!cgltf_accessor_read_uint(acc, v, joints, 4)) return 0;
                        for (k = 0; k < 4; ++k) if (joints[k] >= node->skin->joints_count) return 0;
                    }
                }
            }
        }
    }
    return 1;
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
        if (*size > MODEL_CPU_BUDGET) return NULL;
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
        if (len < 4 || (len % 4) != 0 || len > MODEL_CPU_BUDGET) return NULL;
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
        bytes = n > 0 && (unsigned long)n <= MODEL_CPU_BUDGET ? malloc((size_t)n) : NULL;
        if (bytes != NULL && fread(bytes, 1, (size_t)n, fp) != (size_t)n) {
            free(bytes);
            bytes = NULL;
        }
        fclose(fp);
        *size = (size_t)n;
        return bytes;
    }
}

/* Position of the lowest set bit of a channel mask (0 for an empty one). */
static int Model_LowestBit(unsigned int mask)
{
    int i;

    for (i = 0; i < 32; i++) {
        if (mask & (1u << i)) {
            return i;
        }
    }
    return 0;
}

static unsigned int ModelAsset_LE32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/*
 * Decodes a DDS image as Ikemen GO's decoder (lukegb/dds) does: only
 * uncompressed images with RGB and alpha bit masks (no FourCC), their first
 * level, rows in file order; each channel is masked and shifted down, not
 * rescaled. Returns malloc'd RGBA, NULL for anything else.
 */
static unsigned char *Model_DecodeDds(const unsigned char *data, size_t size, int *w_out, int *h_out)
{
    enum { DDPF_ALPHAPIXELS = 0x1, DDPF_RGB = 0x40 };
    unsigned int height, width, flags, bits, masks[4];
    unsigned char *pixels;
    size_t stride, pitch;
    int shifts[4];
    unsigned int x, y;
    int c, k;

    if (size < 128 || memcmp(data, "DDS ", 4) != 0) {
        return NULL;
    }
    height = ModelAsset_LE32(data + 12);
    width = ModelAsset_LE32(data + 16);
    flags = ModelAsset_LE32(data + 80);
    bits = ModelAsset_LE32(data + 88);
    for (c = 0; c < 4; c++) {
        masks[c] = ModelAsset_LE32(data + 92 + c * 4);
        shifts[c] = Model_LowestBit(masks[c]);
    }
    if (flags != (DDPF_ALPHAPIXELS | DDPF_RGB) ||
        (bits != 8 && bits != 16 && bits != 24 && bits != 32) || width == 0 || height == 0) {
        return NULL;
    }
    stride = (bits + 7) / 8;
    pitch = ((size_t)width * bits + 7) / 8;
    if (width > MODEL_CPU_BUDGET / 4u || height > MODEL_CPU_BUDGET / (width * 4u) ||
        pitch > (size - 128) / height) {
        return NULL;
    }
    pixels = malloc((size_t)width * height * 4);
    if (pixels == NULL) {
        return NULL;
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const unsigned char *p = data + 128 + pitch * y + stride * x;
            unsigned int v = 0;
            for (k = 0; k < (int)stride; k++) {
                v |= (unsigned int)p[k] << (8 * k);
            }
            for (c = 0; c < 4; c++) {
                pixels[((size_t)y * width + x) * 4 + c] = (unsigned char)((v & masks[c]) >> shifts[c]);
            }
        }
    }
    *w_out = (int)width;
    *h_out = (int)height;
    return pixels;
}

/* Creates the GL texture of a glTF texture (Ikemen GO's sampling defaults: nearest, repeat without a sampler). */
static int Model_DecodeTexture(ModelImage *image, const cgltf_texture *t, const cgltf_options *options, const char *dir)
{
    GLint mag = GL_NEAREST, min = GL_NEAREST, wrap_s = GL_REPEAT, wrap_t = GL_REPEAT;
    unsigned char *bytes, *pixels;
    size_t size;
    int w, h;

    if (t->image == NULL) {
        return 0;
    }
    bytes = Model_ReadImage(t->image, options, dir, &size);
    if (bytes == NULL) {
        Log_Printf("Model: can't read image %s\n", t->image->uri != NULL ? t->image->uri : "(embedded)");
        return 0;
    }
    /* Port fix: reject decoded dimensions before allocating PNG/JPEG pixels. */
    if (size >= 24 && memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0) {
        unsigned w0 = ((unsigned)bytes[16] << 24) | ((unsigned)bytes[17] << 16) | ((unsigned)bytes[18] << 8) | bytes[19];
        unsigned h0 = ((unsigned)bytes[20] << 24) | ((unsigned)bytes[21] << 16) | ((unsigned)bytes[22] << 8) | bytes[23];
        if (!w0 || !h0 || w0 > MODEL_CPU_BUDGET / 4 || h0 > MODEL_CPU_BUDGET / (w0 * 4)) {
            /* Port addition: this used to reject silently, and the caller only
             * reported "Error loading model" with no reason. */
            Log_Printf("Model: image %s is %ux%u, over the %u MiB texture budget\n",
                       t->image->uri != NULL ? t->image->uri : "(embedded)",
                       w0, h0, MODEL_CPU_BUDGET / (1024u * 1024u));
            free(bytes); return 0;
        }
    } else if (size <= 0x7fffffff) {
        int components;
        if (stbi_info_from_memory(bytes, (int)size, &w, &h, &components) &&
            (w <= 0 || h <= 0 || (unsigned)w > MODEL_CPU_BUDGET / 4 ||
             (unsigned)h > MODEL_CPU_BUDGET / ((unsigned)w * 4))) {
            Log_Printf("Model: image %s is %dx%d, over the %u MiB texture budget\n",
                       t->image->uri != NULL ? t->image->uri : "(embedded)",
                       w, h, MODEL_CPU_BUDGET / (1024u * 1024u));
            free(bytes); return 0;
        }
    }
    /* the formats Ikemen GO reads: PNG (libpng), JPEG (stb_image), uncompressed DDS */
    pixels = Png_DecodeRGBA(bytes, size, &w, &h);
    if (pixels == NULL) {
        pixels = Model_DecodeDds(bytes, size, &w, &h);
    }
    if (pixels == NULL && size <= 0x7FFFFFFF) {
        int comps;
        pixels = stbi_load_from_memory(bytes, (int)size, &w, &h, &comps, 4);
    }
    free(bytes);
    if (pixels == NULL) {
        Log_Printf("Model: unsupported image %s (PNG, JPEG and uncompressed RGBA DDS are supported)\n",
                   t->image->uri != NULL ? t->image->uri : "(embedded)");
        return 0;
    }
    if (w <= 0 || h <= 0 || (unsigned)w > MODEL_CPU_BUDGET / 4u ||
        (unsigned)h > MODEL_CPU_BUDGET / ((unsigned)w * 4u)) {
        free(pixels); return 0;
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
    image->pixels = pixels;
    image->width = w; image->height = h;
    image->mag_filter = mag; image->min_filter = min;
    image->wrap_s = wrap_s; image->wrap_t = wrap_t;
    return 1;
}

int ModelAsset_LoadImages(Model *mdl, const cgltf_data *data, const cgltf_options *options, const char *dir)
{
    cgltf_size i;
    size_t total = mdl->vertex_size + (size_t)mdl->index_count * 4;
    size_t decoded = 0;
    int largest_w = 0, largest_h = 0, decoded_count = 0;
    mdl->texture_count = (int)data->textures_count;
    mdl->images = calloc(data->textures_count + 1, sizeof(ModelImage));
    mdl->textures = calloc(data->textures_count + 1, sizeof(unsigned int));
    if (mdl->images == NULL || mdl->textures == NULL) return 0;
    for (i = 0; i < data->textures_count; ++i) {
        if (!Model_DecodeTexture(&mdl->images[i], &data->textures[i], options, dir) && data->textures[i].image != NULL)
            return 0;
        if (mdl->images[i].pixels != NULL) {
            size_t bytes = (size_t)mdl->images[i].width * mdl->images[i].height * 4;
            if (total > MODEL_CPU_BUDGET || bytes > MODEL_CPU_BUDGET - total) {
                Log_Printf("Model: decoded geometry/textures exceed CPU budget (%u bytes)\n", MODEL_CPU_BUDGET);
                return 0;
            }
            total += bytes;
            decoded += bytes;
            decoded_count++;
            if (mdl->images[i].width > largest_w) largest_w = mdl->images[i].width;
            if (mdl->images[i].height > largest_h) largest_h = mdl->images[i].height;
        }
    }
    /*
     * Port addition: what the textures cost, against the budget that rejects
     * them. Without this a stage that fails on textures looked identical to one
     * that failed to parse.
     */
    Log_Printf("Model: textures: %d of %d decoded, %.2f MiB, largest %dx%d, "
               "budget %u MiB total (geometry %.2f MiB)\n",
               decoded_count, (int)data->textures_count, (double)decoded / (1024.0 * 1024.0),
               largest_w, largest_h, MODEL_CPU_BUDGET / (1024u * 1024u),
               (double)(mdl->vertex_size + (size_t)mdl->index_count * 4) / (1024.0 * 1024.0));
    return 1;
}

#endif
