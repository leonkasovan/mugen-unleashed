/*
 * GLES draw backend: program loading, uniform setup and VBO quad submission.
 *
 * RENDERING_PLAN.md sections 11 (V3) and 12: the desktop renderer draws with
 * immediate mode (glBegin/glVertex/glTexCoord) and a fixed-function matrix
 * stack. Neither exists in GLES, so this backend supplies the replacement
 * for the programmable path:
 *
 *   - one interleaved vertex buffer (position + texcoord with the projective
 *     Q), uploaded per quad;
 *   - explicit attribute locations bound before linking, so the shader does
 *     not depend on the driver's attribute assignment;
 *   - one mat4 MVP uniform, computed on the CPU by src/gl_mvp.c;
 *   - the palette bound as a 256x1 2D texture (GLES has no 1D textures).
 *
 * The geometry math is in src/gl_mvp.c and is host-tested; this file is the
 * thin GL layer over it. It is compile-checked against the vendored GLES
 * loader, and becomes runnable once a GLES context and the mode-selection
 * wiring land (tracked as the remainder of the M8 todo).
 */
#ifndef MUGEN_GLES_DRAW_H
#define MUGEN_GLES_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

/* The interleaved vertex layout (position, then texcoord with its projective
 * q) is owned by src/gl_mvp.c so its offsets can be host-tested; see
 * GL_QUAD_VERTEX_FLOATS and GlQuad_Layout there. Dropping q flattens the
 * trapezoid -- the GLES-Q mutation in tools/test_mutate.sh is that failure. */
#define GLES_VERTEX_POS_COMPONENTS 2
#define GLES_VERTEX_TC_COMPONENTS  4

/* Attribute locations, bound explicitly before linking. */
#define GLES_ATTR_POSITION  0
#define GLES_ATTR_TEXCOORD  1

/* A linked program and its uniform locations. A location of -1 means the
 * compiler optimised the uniform out, which is normal and tolerated. */
typedef struct GlesProgram {
    unsigned int program;
    int u_mvp;
    int texunit0;
    int texunit1;
    int mask;
    int add;
    int mul;
    int invertall;
    int colorbal;
    int texsize;
    int src_alphamul;
} GlesProgram;

/* Vertex-buffer state owned by the backend (one quad at a time). */
typedef struct GlesBuffer {
    unsigned int vbo;
    unsigned int capacity;  /* vertices the buffer can hold */
} GlesBuffer;

/* Creates the program from GLSL ES source text. Returns 1 on success and
 * fills *out (its uniform locations included). Attribute locations are bound
 * before linking. The source may carry the engine's defines marker; `defines`
 * is spliced there (NULL or "" for none). */
int GlesDraw_LoadProgram(GlesProgram *out, const char *vert_src, const char *frag_src, const char *label,
                         const char *defines);

/* Deletes the program and clears the struct. */
void GlesDraw_FreeProgram(GlesProgram *prog);

/* Uploads one quad (four vertices in triangle-strip order) and draws it.
 * `indexed` selects the fragment path's palette texture unit. */
void GlesDraw_Quad(GlesBuffer *buf, const GlesProgram *prog, const float *mvp16, const float *positions8,
                   const float *texcoords16, int indexed, int mask, float add_r, float add_g, float add_b,
                   float mul_r, float mul_g, float mul_b, int invertall, float colorbal, float tex_w, float tex_h,
                   float src_alphamul);

/* Releases the vertex buffer. */
void GlesDraw_FreeBuffer(GlesBuffer *buf);

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_GLES_DRAW_H */
