/* Port addition: explicit 2D/3D state boundary for desktop GL, GLES and VitaGL. */
#include "mugen.h"
#ifdef MUGEN_MODEL3D
#include "model_backend.h"

#ifdef MUGEN_GLES
static GLuint model_vao;
#endif

static int ModelPass_HasFbo(void)
{
#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL)
    return 1;
#else
    return g_glewExtFramebufferObject != 0;
#endif
}

void ModelPass_Begin(ModelPass *p)
{
    int i;
    memset(p, 0, sizeof(*p));
    glGetIntegerv(GL_CURRENT_PROGRAM, &p->program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &p->array_buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &p->element_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &p->active_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &p->unpack);
#ifndef MUGEN_VITA_GL
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &p->unpack_buffer);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &p->unpack_row);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &p->unpack_skip_rows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &p->unpack_skip_pixels);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
    glGetIntegerv(GL_VIEWPORT, p->viewport);
    glGetIntegerv(GL_SCISSOR_BOX, p->scissor);
    glGetIntegerv(GL_DEPTH_FUNC, &p->depth_func);
    glGetIntegerv(GL_CULL_FACE_MODE, &p->cull_mode);
    glGetIntegerv(GL_FRONT_FACE, &p->front_face);
    glGetIntegerv(GL_BLEND_SRC_RGB, &p->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &p->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &p->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &p->blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &p->blend_eq_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &p->blend_eq_alpha);
#ifndef MUGEN_GLES
    p->alpha_test = glIsEnabled(GL_ALPHA_TEST);
#endif
    p->blend = glIsEnabled(GL_BLEND); p->depth = glIsEnabled(GL_DEPTH_TEST);
    p->cull = glIsEnabled(GL_CULL_FACE); p->scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &p->depth_mask);
    glGetBooleanv(GL_COLOR_WRITEMASK, p->color_mask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, p->clear_color);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &p->clear_depth);
    if (ModelPass_HasFbo()) {
        glGetIntegerv(0x8CA6 /* FRAMEBUFFER_BINDING */, &p->framebuffer);
        glGetIntegerv(0x8CA7 /* RENDERBUFFER_BINDING */, &p->renderbuffer);
    }
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &p->units);
#ifdef MUGEN_MODEL_BASIC
    if (p->units > 1) p->units = 1;
#elif defined(MUGEN_GLES)
    if (p->units > 5) p->units = 5;
#else
    if (p->units > 16) p->units = 16;
#endif
    for (i = 0; i < p->units; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &p->texture_2d[i]);
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &p->texture_cube[i]);
    }
    glActiveTexture(p->active_texture);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &p->attrs);
    if (p->attrs > 16) p->attrs = 16;
    for (i = 0; i < p->attrs; ++i) {
        /* Attribute zero's current value is not queryable on legacy GL/Vita. */
        if (i != 0) glGetVertexAttribfv(i, GL_CURRENT_VERTEX_ATTRIB, p->constant[i]);
#ifndef MUGEN_GLES
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &p->attr[i].enabled);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &p->attr[i].buffer);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &p->attr[i].size);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &p->attr[i].type);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &p->attr[i].normalized);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &p->attr[i].stride);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &p->attr[i].pointer);
#endif
    }
#ifdef MUGEN_GLES
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &p->read_framebuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &p->vao);
    if (model_vao == 0) glGenVertexArrays(1, &model_vao);
    glBindVertexArray(model_vao);
#endif
}

static void ModelPass_Enable(GLenum cap, int enabled)
{
    if (enabled) glEnable(cap); else glDisable(cap);
}

void ModelPass_End(const ModelPass *p)
{
    int i;
#ifdef MUGEN_GLES
    glBindVertexArray(p->vao);
#endif
    for (i = 0; i < p->attrs; ++i) {
#ifndef MUGEN_GLES
        glBindBuffer(GL_ARRAY_BUFFER, p->attr[i].buffer);
        glVertexAttribPointer(i, p->attr[i].size, p->attr[i].type, p->attr[i].normalized,
                              p->attr[i].stride, p->attr[i].pointer);
        if (p->attr[i].enabled) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
#endif
        if (i != 0) glVertexAttrib4fv(i, p->constant[i]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, p->array_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->element_buffer);
    for (i = 0; i < p->units; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, p->texture_2d[i]);
        glBindTexture(GL_TEXTURE_CUBE_MAP, p->texture_cube[i]);
    }
    glActiveTexture(p->active_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, p->unpack);
#ifndef MUGEN_VITA_GL
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, p->unpack_buffer);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, p->unpack_row);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, p->unpack_skip_rows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, p->unpack_skip_pixels);
#endif
    if (ModelPass_HasFbo()) {
#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL)
#ifdef MUGEN_GLES
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, p->read_framebuffer);
#else
        glBindFramebuffer(GL_FRAMEBUFFER, p->framebuffer);
#endif
        glBindRenderbuffer(GL_RENDERBUFFER, p->renderbuffer);
#else
        g_glewBindFramebufferEXT(0x8D40 /* FRAMEBUFFER */, p->framebuffer);
        g_glewBindRenderbufferEXT(0x8D41 /* RENDERBUFFER */, p->renderbuffer);
#endif
    }
    g_glBoundFramebuffer = p->framebuffer;
    glViewport(p->viewport[0], p->viewport[1], p->viewport[2], p->viewport[3]);
    glScissor(p->scissor[0], p->scissor[1], p->scissor[2], p->scissor[3]);
    glDepthFunc(p->depth_func); glDepthMask(p->depth_mask);
    glCullFace(p->cull_mode); glFrontFace(p->front_face);
    glColorMask(p->color_mask[0], p->color_mask[1], p->color_mask[2], p->color_mask[3]);
    glBlendFuncSeparate(p->blend_src_rgb, p->blend_dst_rgb, p->blend_src_alpha, p->blend_dst_alpha);
    glBlendEquationSeparate(p->blend_eq_rgb, p->blend_eq_alpha);
    glClearColor(p->clear_color[0], p->clear_color[1], p->clear_color[2], p->clear_color[3]);
#if defined(MUGEN_GLES) || defined(MUGEN_VITA_GL)
    glClearDepthf(p->clear_depth);
#else
    glClearDepth(p->clear_depth);
#endif
#ifndef MUGEN_GLES
    ModelPass_Enable(GL_ALPHA_TEST, p->alpha_test);
#endif
    ModelPass_Enable(GL_BLEND, p->blend); ModelPass_Enable(GL_DEPTH_TEST, p->depth);
    ModelPass_Enable(GL_CULL_FACE, p->cull); ModelPass_Enable(GL_SCISSOR_TEST, p->scissor_test);
    glUseProgram(p->program);
    g_glCurrentProgram = p->program;
}

void ModelPass_Reset(int destroy)
{
#ifdef MUGEN_GLES
    if (destroy && model_vao != 0) glDeleteVertexArrays(1, &model_vao);
    model_vao = 0;
#else
    (void)destroy;
#endif
}
#endif
