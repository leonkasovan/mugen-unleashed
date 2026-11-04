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

/* Shader source with the given defines spliced in at the ES dialect's
 * marker. The ES shaders put the marker after their precision statement, so
 * a plain textual replace is enough (same approach as Gl_InsertDefines). */
static char *gles_insert_at_marker(const char *src, const char *defines)
{
    static const char marker[] = "////*EB_DEFINES*////";
    const char *at;
    size_t head, def_len, tail_len;
    char *out;

    if (src == NULL) {
        return NULL;
    }
    at = strstr(src, marker);
    if (at == NULL || defines == NULL) {
        return strdup(src);
    }
    head = (size_t)(at - src);
    def_len = strlen(defines);
    tail_len = strlen(at + sizeof(marker) - 1);
    out = malloc(head + def_len + tail_len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, src, head);
    memcpy(out + head, defines, def_len);
    memcpy(out + head + def_len, at + sizeof(marker) - 1, tail_len);
    out[head + def_len + tail_len] = '\0';
    return out;
}

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
     * the shaders' defaults: INDEX_SAFE, PERSPECTIVE_EVEN, full PalFX. */
    vs = gles_insert_at_marker(vert_src, defines != NULL ? defines : "");
    fs = gles_insert_at_marker(frag_src, defines != NULL ? defines : "");
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
    if (prog != NULL && prog->program != 0) {
        glDeleteProgram(prog->program);
        prog->program = 0;
    }
}

void GlesDraw_FreeBuffer(GlesBuffer *buf)
{
    if (buf != NULL && buf->vbo != 0) {
        glDeleteBuffers(1, &buf->vbo);
        buf->vbo = 0;
        buf->capacity = 0;
    }
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

    glUseProgram(prog->program);
    /* MVP and the scalar uniforms. Locations of -1 are optimised away and
     * setting them is a no-op by definition. */
    if (prog->u_mvp >= 0) {
        glUniformMatrix4fv(prog->u_mvp, 1, GL_FALSE, mvp16);
    }
    if (prog->texunit0 >= 0) {
        glUniform1i(prog->texunit0, 0);
    }
    if (prog->texunit1 >= 0) {
        glUniform1i(prog->texunit1, 1);
    }
    if (prog->mask >= 0) {
        glUniform1i(prog->mask, mask != 0);
    }
    if (prog->add >= 0) {
        glUniform3f(prog->add, add_r, add_g, add_b);
    }
    if (prog->mul >= 0) {
        glUniform3f(prog->mul, mul_r, mul_g, mul_b);
    }
    if (prog->invertall >= 0) {
        glUniform1i(prog->invertall, invertall != 0);
    }
    if (prog->colorbal >= 0) {
        glUniform1f(prog->colorbal, colorbal);
    }
    if (prog->texsize >= 0) {
        glUniform2f(prog->texsize, tex_w, tex_h);
    }
    if (prog->src_alphamul >= 0) {
        glUniform1f(prog->src_alphamul, src_alphamul);
    }

    /* Texture units: 0 is the sprite, 1 the palette (indexed only; the RGBA
     * program has no texunit1 and its location is -1). */
    glActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE0 + 1);

    glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(GLES_ATTR_POSITION);
    glVertexAttribPointer(GLES_ATTR_POSITION, GLES_VERTEX_POS_COMPONENTS, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (const void *)(size_t)pos_off);
    glEnableVertexAttribArray(GLES_ATTR_TEXCOORD);
    glVertexAttribPointer(GLES_ATTR_TEXCOORD, GLES_VERTEX_TC_COMPONENTS, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (const void *)(size_t)tc_off);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(GLES_ATTR_POSITION);
    glDisableVertexAttribArray(GLES_ATTR_TEXCOORD);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    (void)indexed;
}

#endif /* MUGEN_GLES */
