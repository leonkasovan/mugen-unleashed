/*
 * Shader source preprocessing shared by every loader.
 *
 * The engine's shaders carry the EB_DEFINES marker, which is
 * replaced by the "#define ..." block that selects a variant. The desktop
 * loader (Gl_InsertDefines, src/glrender.c 0x0047C460) and the GLES loader
 * (src/gles_draw.c) must agree on that marker byte for byte: they read the
 * same file families, and the shader dialects document it as the contract.
 * The marker and the replacement therefore live here once.
 *
 * Header-only on purpose: the GLES build excludes glrender.c, so a .c file
 * would have to be added to each build's source list for a twenty-line
 * function.
 */
#ifndef MUGEN_SHADER_SRC_H
#define MUGEN_SHADER_SRC_H

#include <stdlib.h>
#include <string.h>

#define SHADER_DEFINES_MARK "////*EB_DEFINES*////"

/*
 * malloc'ed copy of src with the marker replaced by defines. A source with
 * no marker (or a NULL defines, meaning "no extra defines") is copied
 * unchanged. Returns NULL only when src is NULL or allocation fails; the
 * original Gl_InsertDefines dereferenced a NULL src, which no caller did.
 */
static __inline char *ShaderSrc_InsertDefines(const char *src, const char *defines)
{
    const char *mark;
    size_t len_src;
    size_t len_def;
    size_t len_mark;
    size_t head;
    size_t total;
    char *out;

    if (src == NULL) {
        return NULL;
    }
    if (defines == NULL) {
        defines = "";
    }
    len_src = strlen(src);
    len_def = strlen(defines);
    len_mark = strlen(SHADER_DEFINES_MARK);
    mark = strstr(src, SHADER_DEFINES_MARK);
    if (mark == NULL) {
        out = (char *)malloc(len_src + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, src, len_src + 1);
        return out;
    }
    head = (size_t)(mark - src);
    total = len_src - len_mark + len_def;
    out = (char *)malloc(total + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, src, head);
    memcpy(out + head, defines, len_def);
    memcpy(out + head + len_def, mark + len_mark, len_src - head - len_mark);
    out[total] = '\0';
    return out;
}

#endif
