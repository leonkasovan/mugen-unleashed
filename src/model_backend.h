/* Port addition: CPU assets and context-owned residency have separate lifetimes. */
#ifndef MUGEN_MODEL_BACKEND_H
#define MUGEN_MODEL_BACKEND_H
#ifdef MUGEN_MODEL3D
#include <cgltf.h>

#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL) || defined(MUGEN_MODEL_BASIC)
#define MODEL_PORTABLE 1
#endif
#if defined(MUGEN_VITA_GL)
#define MUGEN_MODEL_BASIC 1
#endif

/* The pass saves the GL state touched by model upload/draw, including attributes
 * that desktop glPushAttrib does not cover. No original engine layout changes. */
typedef struct ModelPass {
    GLint program, array_buffer, element_buffer, active_texture, unpack;
    GLint viewport[4], scissor[4], depth_func, cull_mode, front_face;
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_eq_rgb, blend_eq_alpha, framebuffer, renderbuffer;
    GLboolean blend, depth, cull, scissor_test, depth_mask, color_mask[4];
    GLfloat clear_color[4], clear_depth;
#ifndef MUGEN_GLES
    GLboolean alpha_test;
#endif
    GLint texture_2d[16], texture_cube[16], units, attrs;
#ifdef MUGEN_GLES
    GLint vao, read_framebuffer;
#else
    struct {
        GLint enabled, buffer, size, type, normalized, stride;
        void *pointer;
    } attr[16];
#endif
#ifndef MUGEN_VITA_GL
    GLint unpack_buffer, unpack_row, unpack_skip_rows, unpack_skip_pixels;
#endif
    GLfloat constant[16][4];
} ModelPass;

void ModelPass_Begin(ModelPass *pass);
void ModelPass_End(const ModelPass *pass);
void ModelPass_Reset(int destroy);
/* Bounded decoded buffers also keep multiplication valid on 32-bit targets. */
#ifndef MODEL_CPU_BUDGET
#ifdef MUGEN_VITA_GL
#define MODEL_CPU_BUDGET (16u * 1024u * 1024u)
#else
#define MODEL_CPU_BUDGET (256u * 1024u * 1024u)
#endif
#endif
int ModelAsset_Validate(const cgltf_data *);
int ModelAsset_ValidateSkins(const cgltf_data *);
int ModelAsset_LoadImages(Model *, const cgltf_data *, const cgltf_options *, const char *);
void ModelGpu_Register(Model *);
int ModelGpu_Ensure(Model *);
void ModelGpu_Release(Model *);
void ModelGpu_Reset(int destroy);
#endif
#endif
