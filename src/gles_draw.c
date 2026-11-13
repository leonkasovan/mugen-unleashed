/*
 * GLES draw backend -- see gles_draw.h for what it replaces and why.
 *
 * Compiled only when MUGEN_GLES is set (src/mugen.h then includes the
 * vendored GLES loader instead of the desktop glad). The geometry itself is
 * produced by src/gl_mvp.c, which is host-tested; this file only moves that
 * data to the GPU.
 */
#include "mugen.h"

#ifdef MUGEN_GLES

#include <string.h>

#include "gl_mvp.h"
#include "gles_draw.h"
#include "shader_src.h"
#include "batchprof.h"

static unsigned int gles_compile(GLenum type, const char *src, const char *label)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    char log[512];

    if (shader == 0) {
        Log_Printf("GLES: glCreateShader failed for %s\n", label);
        return 0;
    }
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        Log_Printf("GLES: %s shader compile failed: %s\n", label, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int GlesDraw_LoadProgram(GlesProgram *out, const char *vert_src, const char *frag_src, const char *label,
                         const char *defines)
{
    char *vs;
    char *fs;
    GLuint v;
    GLuint f;
    GLuint p;
    GLint ok = 0;
    char log[512];

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->u_mvp = -1;
    out->u_color = -1;
    out->texunit0 = -1;
    out->texunit1 = -1;
    out->mask = -1;
    out->add = -1;
    out->mul = -1;
    out->invertall = -1;
    out->colorbal = -1;
    out->texsize = -1;
    out->src_alphamul = -1;

    /* Variant defines ("#define X\n" each), spliced at the marker. NULL means
     * the shaders' defaults: INDEX_SAFE, PERSPECTIVE_EVEN, full PalFX. The
     * marker and the replacement are shared with Gl_InsertDefines; see
     * src/shader_src.h. */
    vs = ShaderSrc_InsertDefines(vert_src, defines);
    fs = ShaderSrc_InsertDefines(frag_src, defines);
    if (vs == NULL || fs == NULL) {
        free(vs);
        free(fs);
        return 0;
    }
    v = gles_compile(GL_VERTEX_SHADER, vs, label);
    f = v != 0 ? gles_compile(GL_FRAGMENT_SHADER, fs, label) : 0;
    free(vs);
    free(fs);
    if (v == 0 || f == 0) {
        if (v != 0) {
            glDeleteShader(v);
        }
        return 0;
    }

    p = glCreateProgram();
    if (p == 0) {
        glDeleteShader(v);
        glDeleteShader(f);
        Log_Printf("GLES: glCreateProgram failed for %s\n", label);
        return 0;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    /* Explicit attribute binding: the shader must not depend on whatever
     * locations the driver picks, and GLES has no glGetAttribLocation
     * guarantee before link. */
    glBindAttribLocation(p, GLES_ATTR_POSITION, "a_position");
    glBindAttribLocation(p, GLES_ATTR_TEXCOORD, "a_texcoord");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLsizei len = 0;
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &len, log);
        Log_Printf("GLES: %s program link failed: %s\n", label, log);
        glDeleteProgram(p);
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    /* The shaders stay attached to the program; deleting them now only flags
     * them for deletion, which is the usual pattern. */
    glDeleteShader(v);
    glDeleteShader(f);

    out->program = p;
    out->u_mvp = glGetUniformLocation(p, "u_mvp");
    out->u_color = glGetUniformLocation(p, "u_color");
    out->texunit0 = glGetUniformLocation(p, "texunit0");
    out->texunit1 = glGetUniformLocation(p, "texunit1");
    out->mask = glGetUniformLocation(p, "mask");
    out->add = glGetUniformLocation(p, "add");
    out->mul = glGetUniformLocation(p, "mul");
    out->invertall = glGetUniformLocation(p, "invertall");
    out->colorbal = glGetUniformLocation(p, "colorbal");
    out->texsize = glGetUniformLocation(p, "texsize");
    out->src_alphamul = glGetUniformLocation(p, "src_alphamul");
    if (out->u_mvp < 0) {
        /* Without the MVP the quad cannot be placed; treat it as a failure
         * rather than drawing at an undefined transform. */
        Log_Printf("GLES: %s has no u_mvp uniform\n", label);
        glDeleteProgram(p);
        out->program = 0;
        return 0;
    }
    return 1;
}

void GlesDraw_FreeProgram(GlesProgram *prog)
{
    if (prog != NULL) {
        if (prog->program != 0) glDeleteProgram(prog->program);
        memset(prog, 0, sizeof(*prog));
        prog->u_mvp = prog->u_color = -1;
    }
    /* The name is gone; the cached bind refers to a program that no longer
     * exists (and a relink would reuse the same numeric name). */
    GlesDraw_InvalidateState();
}

void GlesDraw_FreeBuffer(GlesBuffer *buf)
{
    if (buf != NULL && buf->vbo != 0) {
        glDeleteBuffers(1, &buf->vbo);
        buf->vbo = 0;
        buf->capacity = 0;
    }
    GlesDraw_InvalidateState();
}

/*
 * Redundant-state cache.
 *
 * Both backends draw one quad per call, and the engine keeps the same program
 * bound for long runs of sprites, so glUseProgram and most of the uniforms were
 * being re-sent with identical values on every quad. Measured on an RG351MP the
 * state setup fell from ~0.9 ms to ~0.25 ms of a ~13 ms draw.
 *
 * Cached: the bound program, the uniforms that rarely change, and the vertex
 * buffer's attribute setup. NOT cached: the buffer contents (every quad
 * uploads), the MVP and the texture size (they change per quad), the sampler
 * uniforms (set once per program), blend state (a different path owns it) and
 * texture bindings (src/gles_render.c binds them per draw).
 *
 * The model path (src/model.c) binds its own VAO and restores the previous one
 * through ModelPass, so it cannot leave this cache wrong; anything else that
 * changes the program, a uniform or the attribute state must call
 * GlesDraw_InvalidateState -- as the solid-colour path and a context loss do.
 */
static unsigned int g_stateProgram;
static int g_stateProgramValid;
static int g_stateUniformsValid;    /* the uniforms below belong to g_stateProgram */
static int g_stateMask;
static float g_stateAdd[3];
static float g_stateMul[3];
static int g_stateInvert;
static float g_stateColorbal;
static float g_stateAlphamul;
/* The vertex buffer and attribute setup, valid for one buffer object. */
static unsigned int g_stateVbo;
static int g_stateAttribsValid;

void GlesDraw_InvalidateState(void)
{
    g_stateProgramValid = 0;
    g_stateUniformsValid = 0;
    g_stateAttribsValid = 0;
}

void GlesDraw_Quad(GlesBuffer *buf, const GlesProgram *prog, const float *mvp16, const float *positions8,
                   const float *texcoords16, int indexed, int mask, float add_r, float add_g, float add_b,
                   float mul_r, float mul_g, float mul_b, int invertall, float colorbal, float tex_w, float tex_h,
                   float src_alphamul)
{
    float verts[GL_QUAD_VERTEX_COUNT * GL_QUAD_VERTEX_FLOATS];
    GlQuadVerts quad;
    unsigned int stride;
    unsigned int pos_off;
    unsigned int tc_off;
    unsigned long long bp_start;
    unsigned long long bp_quad_start;
    int i;

    if (buf == NULL || prog == NULL || prog->program == 0 || mvp16 == NULL || positions8 == NULL ||
        texcoords16 == NULL) {
        return;
    }
    if (buf->vbo == 0) {
        glGenBuffers(1, &buf->vbo);
        if (buf->vbo == 0) {
            Log_Printf("GLES: glGenBuffers failed\n");
            return;
        }
    }

    /* The caller hands over the corners and texcoords the CPU geometry step
     * produced (GlQuad_Build); this only interleaves them for the GPU. */
    for (i = 0; i < GL_QUAD_VERTEX_COUNT; i++) {
        quad.pos[i][0] = positions8[i * 2];
        quad.pos[i][1] = positions8[i * 2 + 1];
        quad.tc[i][0] = texcoords16[i * 4];
        quad.tc[i][1] = texcoords16[i * 4 + 1];
        quad.tc[i][2] = texcoords16[i * 4 + 2];
        quad.tc[i][3] = texcoords16[i * 4 + 3];
    }
    quad.count = GL_QUAD_VERTEX_COUNT;
    GlQuad_Interleave(verts, &quad);
    GlQuad_Layout(&stride, &pos_off, &tc_off);

    /* MUGEN_DEBUG: times the whole submission. The counter reads are part of
     * what is measured, so the result is an upper bound (batchprof.h). */
    bp_quad_start = BATCHPROF_NOW();
    bp_start = bp_quad_start;

    /* Program: skip the bind when it is already the current one. Locations of
     * -1 are optimised away and setting them is a no-op by definition. */
    if (!g_stateProgramValid || g_stateProgram != prog->program) {
        glUseProgram(prog->program);
        BatchProf_Program(prog->program);
        g_stateProgram = prog->program;
        g_stateProgramValid = 1;
        g_stateUniformsValid = 0;   /* a program change resets its uniforms */
    }

    /* MVP and the texture size change per quad, so they are always sent. */
    if (prog->u_mvp >= 0) {
        glUniformMatrix4fv(prog->u_mvp, 1, GL_FALSE, mvp16);
    }
    if (prog->texsize >= 0) {
        glUniform2f(prog->texsize, tex_w, tex_h);
    }

    /* The rest change rarely, so they are compared first. Comparing floats
     * bit-for-bit is right here: these are copied from fixed-point integers,
     * so an equal value is an identical bit pattern. */
    if (!g_stateUniformsValid || g_stateMask != (mask != 0)) {
        if (prog->mask >= 0) {
            glUniform1i(prog->mask, mask != 0);
        }
        g_stateMask = (mask != 0);
    }
    if (!g_stateUniformsValid || g_stateAdd[0] != add_r || g_stateAdd[1] != add_g || g_stateAdd[2] != add_b) {
        if (prog->add >= 0) {
            glUniform3f(prog->add, add_r, add_g, add_b);
        }
        g_stateAdd[0] = add_r; g_stateAdd[1] = add_g; g_stateAdd[2] = add_b;
    }
    if (!g_stateUniformsValid || g_stateMul[0] != mul_r || g_stateMul[1] != mul_g || g_stateMul[2] != mul_b) {
        if (prog->mul >= 0) {
            glUniform3f(prog->mul, mul_r, mul_g, mul_b);
        }
        g_stateMul[0] = mul_r; g_stateMul[1] = mul_g; g_stateMul[2] = mul_b;
    }
    if (!g_stateUniformsValid || g_stateInvert != (invertall != 0)) {
        if (prog->invertall >= 0) {
            glUniform1i(prog->invertall, invertall != 0);
        }
        g_stateInvert = (invertall != 0);
    }
    if (!g_stateUniformsValid || g_stateColorbal != colorbal) {
        if (prog->colorbal >= 0) {
            glUniform1f(prog->colorbal, colorbal);
        }
        g_stateColorbal = colorbal;
    }
    if (!g_stateUniformsValid || g_stateAlphamul != src_alphamul) {
        if (prog->src_alphamul >= 0) {
            glUniform1f(prog->src_alphamul, src_alphamul);
        }
        g_stateAlphamul = src_alphamul;
    }
    g_stateUniformsValid = 1;

    /* The sampler uniforms are set once per program, not per quad. */
    if (prog->texunit0 >= 0) {
        glUniform1i(prog->texunit0, 0);
    }
    if (prog->texunit1 >= 0) {
        glUniform1i(prog->texunit1, 1);
    }

    /* Texture units: 0 is the sprite, 1 the palette (indexed only; the RGBA
     * program has no texunit1 and its location is -1). Left on unit 0 so the
     * next caller binds where it expects to. */
    glActiveTexture(GL_TEXTURE0 + 1);
    glActiveTexture(GL_TEXTURE0);

    /* Everything above is state setup; from here it is the vertex upload, the
     * draw, and the teardown. Split so a measurement can tell them apart. */
    {
        unsigned long long bp_state = BATCHPROF_NOW();

        BatchProf_Split(BATCHPROF_SPLIT_STATE, bp_state - bp_start);
        bp_start = bp_state;
    }

    /*
     * The buffer binding and the attribute pointers are set once and left in
     * place: the same buffer object is used for every quad, so its attributes
     * stay valid, and the old per-quad enable/pointer/disable cycle was five
     * calls of pure overhead. GLES keeps attribute state in the bound VAO, and
     * the model path binds its own VAO and restores the previous one
     * (ModelPass_Begin/End), so nothing here is clobbered by a model draw.
     */
    if (!g_stateAttribsValid || g_stateVbo != buf->vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
        glEnableVertexAttribArray(GLES_ATTR_POSITION);
        glVertexAttribPointer(GLES_ATTR_POSITION, GLES_VERTEX_POS_COMPONENTS, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (const void *)(size_t)pos_off);
        glEnableVertexAttribArray(GLES_ATTR_TEXCOORD);
        glVertexAttribPointer(GLES_ATTR_TEXCOORD, GLES_VERTEX_TC_COMPONENTS, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (const void *)(size_t)tc_off);
        g_stateVbo = buf->vbo;
        g_stateAttribsValid = 1;
    }

    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
    {
        unsigned long long bp_buf = BATCHPROF_NOW();

        BatchProf_Split(BATCHPROF_SPLIT_BUFFER, bp_buf - bp_start);
        bp_start = bp_buf;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    Perf_CountDraw(2);      /* a quad is two triangles */

    /* One call, one quad, and the four vertices were just re-streamed: this is
     * the batching ratio of 1.00 the profiler reports. */
    {
        unsigned long long bp_end = BATCHPROF_NOW();

        BatchProf_Submit(1, GL_QUAD_VERTEX_COUNT, (unsigned long)sizeof(verts), 2, bp_end - bp_quad_start);
        BatchProf_Split(BATCHPROF_SPLIT_DRAW, bp_end - bp_start);
    }
    (void)indexed;
}

#endif /* MUGEN_GLES */
