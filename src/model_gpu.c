/* Port addition: context-owned GPU residency, independent from CPU asset life. */
#include "mugen.h"
#ifdef MUGEN_MODEL3D
#include "model_backend.h"
#include <limits.h>

/* Engine/render-thread ownership; a GL-free asset load is not a worker API. */
static Model *models;
static unsigned int generation = 1;

void ModelGpu_Register(Model *mdl)
{
    mdl->gpu_next = models;
    models = mdl;
}

static void ModelGpu_Drop(Model *mdl, int destroy)
{
    int i;
    if (destroy) {
        if (mdl->vbo) glDeleteBuffers(1, &mdl->vbo);
        if (mdl->ibo) glDeleteBuffers(1, &mdl->ibo);
        for (i = 0; mdl->textures != NULL && i < mdl->texture_count; ++i)
            if (mdl->textures[i]) glDeleteTextures(1, &mdl->textures[i]);
    }
    if (mdl->env != NULL) {
        if (destroy) {
            if (mdl->env->lambertian) glDeleteTextures(1, &mdl->env->lambertian);
            if (mdl->env->ggx) glDeleteTextures(1, &mdl->env->ggx);
            if (mdl->env->lut) glDeleteTextures(1, &mdl->env->lut);
        }
        mdl->env->lambertian = mdl->env->ggx = mdl->env->lut = 0;
    }
    mdl->vbo = mdl->ibo = 0;
    if (mdl->textures != NULL) memset(mdl->textures, 0, mdl->texture_count * sizeof(*mdl->textures));
    mdl->gpu_generation = mdl->gpu_failed = 0;
    /* Restored VBO starts from the immutable base, so morph caches must replay. */
    for (i = 0; mdl->prims != NULL && i < mdl->prim_count; ++i) mdl->prims[i].applied_count = -1;
}

/*
 * Port addition: name the GL call that failed. One line for the whole upload
 * meant a device log could not distinguish a rejected enum from a missing mip
 * chain from a driver that refused the image; the first failure is recorded and
 * the remaining calls for that texture are skipped.
 */
#define MODEL_GL_STEP(label, call)                                                 \
    do {                                                                           \
        if (ok) {                                                                  \
            GLenum step_error_;                                                    \
            (call);                                                                \
            step_error_ = glGetError();                                            \
            if (step_error_ != GL_NO_ERROR) {                                      \
                ok = 0;                                                            \
                snprintf(reason, sizeof(reason),                                   \
                         "%s -> GL error 0x%04X (texture %d, %dx%d, min 0x%X)",     \
                         label, (unsigned)step_error_, i, image->width,             \
                         image->height, image->min_filter);                        \
            }                                                                      \
        }                                                                          \
    } while (0)

int ModelGpu_Ensure(Model *mdl)
{
    ModelPass state;
    GLint max_texture;
    char reason[160];
    GLenum gl_error;
    int i, ok = 1;
    if (mdl == NULL || g_renderMode != RENDER_OPENGL || !g_glInitialized) return 0;
    if (mdl->gpu_generation == generation) return !mdl->gpu_failed;
#ifdef MUGEN_MODEL_BASIC
    if (mdl->skin_count != 0) {
        Log_Printf("Model: basic profile rejects skinned assets (skinning not qualified)\n");
        mdl->gpu_generation = generation; mdl->gpu_failed = 1;
        return 0;
    }
    /*
     * Port addition (basic profile): the decoded RGBA is dropped once it is on
     * the GPU (see the release below), so a model whose textures are gone but
     * whose GL names are not cannot be rebuilt. Say so once instead of binding
     * texture 0 and drawing whatever that is.
     */
    if (mdl->cpu_textures_released && mdl->textures[0] == 0) {
        Log_Printf("Model: CPU textures were released after upload; a context loss cannot "
                   "rebuild this model on the basic profile\n");
        mdl->gpu_generation = generation; mdl->gpu_failed = 1;
        return 0;
    }
#endif
    if (mdl->vertex_size > (unsigned int)INT_MAX || mdl->index_count > INT_MAX / 4u) return 0;
    ModelPass_Begin(&state);
    /*
     * Port addition: an error left pending by the engine's own drawing must not
     * be attributed to this upload -- without this the check below failed on a
     * stale GL_INVALID_OPERATION and every model looked un-uploadable.
     */
    while (glGetError() != GL_NO_ERROR) {
    }
    reason[0] = '\0';
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    glGenBuffers(1, &mdl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBufferData(GL_ARRAY_BUFFER, mdl->vertex_size, mdl->vertex_data, GL_STATIC_DRAW);
    glGenBuffers(1, &mdl->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)mdl->index_count * 4, mdl->index_data, GL_STATIC_DRAW);
    if (mdl->vbo == 0 || mdl->ibo == 0) {
        ok = 0;
        snprintf(reason, sizeof(reason), "vertex/index buffers (vbo=%u ibo=%u)",
                 mdl->vbo, mdl->ibo);
    }
    /* Any error from the buffer setup would otherwise be blamed on texture 0. */
    gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        ok = 0;
        snprintf(reason, sizeof(reason), "vertex/index buffer setup -> GL error 0x%04X",
                 (unsigned)gl_error);
    }
    for (i = 0; ok && i < mdl->texture_count; ++i) {
        const ModelImage *image = &mdl->images[i];
        if (image->pixels == NULL) continue;
        if (image->width > max_texture || image->height > max_texture) {
            ok = 0;
            snprintf(reason, sizeof(reason),
                     "texture %d is %dx%d, GL_MAX_TEXTURE_SIZE is %d", i,
                     image->width, image->height, (int)max_texture);
            break;
        }
        glGenTextures(1, &mdl->textures[i]);
        if (mdl->textures[i] == 0) {
            ok = 0;
            snprintf(reason, sizeof(reason), "glGenTextures returned 0 for texture %d", i);
            break;
        }
        MODEL_GL_STEP("glActiveTexture", glActiveTexture(GL_TEXTURE0));
        MODEL_GL_STEP("glBindTexture", glBindTexture(GL_TEXTURE_2D, mdl->textures[i]));
#ifndef MUGEN_VITA_GL
        /*
         * Port fix: the pinned vitaGL implements only GL_UNPACK_ROW_LENGTH in
         * glPixelStorei and answers GL_INVALID_ENUM for anything else
         * (third_party/vitagl/src/source/textures.c), which rejected every
         * model upload. Nothing is lost by not asking: these textures are RGBA8,
         * so a row is width*4 bytes and always 4-byte aligned -- the default
         * alignment is already what the data needs.
         */
        MODEL_GL_STEP("glPixelStorei", glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
#endif
#ifndef MODEL_PORTABLE
        /* Preserve the reference driver's legacy mip generation behavior. */
        MODEL_GL_STEP("GL_GENERATE_MIPMAP",
                      glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE));
#endif
        MODEL_GL_STEP("glTexImage2D",
                      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0,
                                   GL_RGBA, GL_UNSIGNED_BYTE, image->pixels));
        MODEL_GL_STEP("mag filter",
                      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, image->mag_filter));
        MODEL_GL_STEP("min filter",
                      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, image->min_filter));
        MODEL_GL_STEP("wrap S",
                      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, image->wrap_s));
        MODEL_GL_STEP("wrap T",
                      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, image->wrap_t));
#ifdef MODEL_PORTABLE
        if (image->min_filter != GL_LINEAR && image->min_filter != GL_NEAREST) {
#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL)
            MODEL_GL_STEP("glGenerateMipmap", glGenerateMipmap(GL_TEXTURE_2D));
#else
            MODEL_GL_STEP("glGenerateMipmapEXT", glGenerateMipmapEXT(GL_TEXTURE_2D));
#endif
        }
#endif
        if (!ok) break;
    }
    if (ok && mdl->env != NULL && mdl->env->lambertian == 0) {
        char source[sizeof(mdl->env->source)];
        float intensity = mdl->env->intensity;
        memcpy(source, mdl->env->source, sizeof(source));
        ok = Model_LoadEnvironment(mdl, source, intensity);
        if (!ok) snprintf(reason, sizeof(reason), "environment map");
    }
    if (!ok) {
        ModelGpu_Drop(mdl, 1);
        Log_Printf("Model: GPU upload failed (%s); preserving CPU asset for next context\n",
                   reason[0] != '\0' ? reason : "unreported");
    } else {
#ifdef MUGEN_MODEL_BASIC
        /*
         * Port addition (basic profile): free every decoded RGBA once it is on
         * the GPU. The CPU copy exists to rebuild the model after a context
         * loss, which this profile cannot do anyway, and holding it costs the
         * whole decoded set at once: LegendsRing is 47 MiB of pixels against a
         * 64 MiB heap and a 16 MiB budget (src/model_backend.h), which is why
         * it never loaded. Peak use is now one texture plus geometry.
         */
        if (mdl->images != NULL) {
            for (i = 0; i < mdl->texture_count; ++i) {
                free(mdl->images[i].pixels);
                mdl->images[i].pixels = NULL;
            }
            mdl->cpu_textures_released = 1;
        }
#endif
    }
    mdl->gpu_generation = generation;
    mdl->gpu_failed = !ok;
    ModelPass_End(&state);
    return ok;
}

void ModelGpu_Release(Model *mdl)
{
    Model **link = &models;
    while (*link != NULL && *link != mdl) link = &(*link)->gpu_next;
    if (*link == mdl) *link = mdl->gpu_next;
    ModelGpu_Drop(mdl, g_glInitialized && mdl->gpu_generation == generation);
}

void ModelGpu_Reset(int destroy)
{
    Model *mdl;
    for (mdl = models; mdl != NULL; mdl = mdl->gpu_next) ModelGpu_Drop(mdl, destroy);
    ModelPass_Reset(destroy);
    if (++generation == 0) generation = 1;
}
#endif
