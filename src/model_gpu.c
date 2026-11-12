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

int ModelGpu_Ensure(Model *mdl)
{
    ModelPass state;
    GLint max_texture;
    int i, ok = 1;
    if (mdl == NULL || g_renderMode != RENDER_OPENGL || !g_glInitialized) return 0;
    if (mdl->gpu_generation == generation) return !mdl->gpu_failed;
#ifdef MUGEN_MODEL_BASIC
    if (mdl->skin_count != 0) {
        Log_Printf("Model: basic profile rejects skinned assets (skinning not qualified)\n");
        mdl->gpu_generation = generation; mdl->gpu_failed = 1;
        return 0;
    }
#endif
    if (mdl->vertex_size > (unsigned int)INT_MAX || mdl->index_count > INT_MAX / 4u) return 0;
    ModelPass_Begin(&state);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    glGenBuffers(1, &mdl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vbo);
    glBufferData(GL_ARRAY_BUFFER, mdl->vertex_size, mdl->vertex_data, GL_STATIC_DRAW);
    glGenBuffers(1, &mdl->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)mdl->index_count * 4, mdl->index_data, GL_STATIC_DRAW);
    for (i = 0; i < mdl->texture_count; ++i) {
        const ModelImage *image = &mdl->images[i];
        if (image->pixels == NULL) continue;
        if (image->width > max_texture || image->height > max_texture) { ok = 0; break; }
        glGenTextures(1, &mdl->textures[i]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mdl->textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifndef MODEL_PORTABLE
        /* Preserve the reference driver's legacy mip generation behavior. */
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
#endif
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, image->mag_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, image->min_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, image->wrap_s);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, image->wrap_t);
#ifdef MODEL_PORTABLE
        if (image->min_filter != GL_LINEAR && image->min_filter != GL_NEAREST)
#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL)
            glGenerateMipmap(GL_TEXTURE_2D);
#else
            glGenerateMipmapEXT(GL_TEXTURE_2D);
#endif
#endif
        if (mdl->textures[i] == 0) ok = 0;
    }
    if (glGetError() != GL_NO_ERROR || mdl->vbo == 0 || mdl->ibo == 0) ok = 0;
    if (ok && mdl->env != NULL && mdl->env->lambertian == 0) {
        char source[sizeof(mdl->env->source)];
        float intensity = mdl->env->intensity;
        memcpy(source, mdl->env->source, sizeof(source));
        ok = Model_LoadEnvironment(mdl, source, intensity);
    }
    if (!ok) {
        ModelGpu_Drop(mdl, 1);
        Log_Printf("Model: GPU upload failed; preserving CPU asset for next context\n");
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
