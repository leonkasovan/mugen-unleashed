/*
 * GLES draw path: MVP construction and quad vertex generation.
 *
 * RENDERING_PLAN.md sections 11 (V3) and 12: GLES has no fixed-function
 * matrix stack and no immediate mode, so the desktop renderer's
 * glMatrixMode/glOrtho/glBegin/glVertex sequence cannot be compiled for it.
 * This unit provides the two replacements:
 *
 *   1. MVP matrices built on the CPU, reproducing what Gl_SetupView
 *      (src/glrender.c) leaves on the GL matrix stack: the orthographic or
 *      perspective projection, the follow-up glTranslated, and the z scale.
 *   2. Quad vertex + texcoord generation reproducing Glr_QuadVertices,
 *      including the projective Q of stretched quads.
 *
 * Both are pure CPU math with no GL calls, so they are unit-tested on the
 * host (tools/test_gl_mvp.sh) rather than only on a device. The VBO upload
 * and the draw call that consume them stay in the backend.
 *
 * Float rules follow the rest of the engine: intermediates are double,
 * values stored are float, and the operation order matches the original
 * (see AGENTS.md, "Float precision").
 */
#ifndef MUGEN_GL_MVP_H
#define MUGEN_GL_MVP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Column-major 4x4, the layout GL expects from glUniformMatrix4fv(..., 0, m):
 * m[0..3] is column 0. */
typedef struct GlMvp {
    float m[16];
} GlMvp;

/* Quad corners as the renderer passes them: (x0,y0)..(x3,y3) in triangle
 * strip order. */
typedef struct GlQuadVerts {
    float pos[4][2];    /* one x,y pair per corner */
    float tc[4][4];     /* s,t,r,q per corner */
    int count;          /* always 4 */
} GlQuadVerts;

/*
 * Builds the projection the desktop path gets from Gl_SetupView.
 *
 * projection: 0 = orthographic (the sprite path), 1 = perspective.
 * framebuffer: g_glBoundFramebuffer; flips the ortho's bottom/top and the
 *              perspective's top/bottom pair, exactly as the GL path does.
 * near_plane / far_plane / z_scale: g_glNear, g_glFar, g_glZScale.
 *
 * Equivalent to: glMatrixMode(PROJECTION); glLoadIdentity(); glOrtho(...)
 * or glFrustum(...); glTranslated(...); glScaled(1,1,z_scale).
 */
void GlMvp_SetupView(GlMvp *out, int projection, int width, int height, int framebuffer, double near_plane,
                     double far_plane, double z_scale);

/* Modelview used by the sprite path: identity (the desktop path loads an
 * identity modelview in Gl_SetupView and never composes a camera matrix). */
void GlMvp_Identity(GlMvp *out);

/* out = projection * modelview, the matrix a GLES vertex shader multiplies. */
void GlMvp_Multiply(GlMvp *out, const GlMvp *projection, const GlMvp *modelview);

/* Transforms a point by the matrix (w is returned in *w_out). z matters for
 * the perspective projection, which divides by it; the sprite path passes 0. */
void GlMvp_Transform(const GlMvp *mvp, float x, float y, float z, float *x_out, float *y_out, float *z_out,
                     float *w_out);

/*
 * Fills the four corners and texcoords of a sprite quad, mirroring
 * Glr_QuadVertices:
 *
 *  - not special: texcoords (0,0), (1,0), (0,1), (1,1) -- straight sampling.
 *  - special (stretched quad): the bottom edge carries a projective Q so the
 *    perspective-correct sampler keeps the trapezoid. top_offset and
 *    bottom_offset shift the vertical texcoords; width_ratio divides Q.
 *
 * The float order of the desktop code is preserved on purpose: the offsets
 * are doubled in double, `s` is computed as `(offset - doubled) * 0.5`, and
 * the result is stored as float.
 */
void GlQuad_Build(GlQuadVerts *out, const float *v, int special, double c0, double c1, double top_offset,
                  double bottom_offset, float width_ratio);

/*
 * Interleaved vertex layout for the GLES backend: position (x,y) then
 * texcoord (s,t,r,q), all float, one struct per vertex. The q component is
 * the projective stretch; it is part of the layout precisely so it cannot be
 * dropped on the way to the GPU.
 */
#define GL_QUAD_VERTEX_FLOATS 6     /* 2 position + 4 texcoord */
#define GL_QUAD_VERTEX_COUNT  4

/* Byte stride and the byte offset of each attribute inside one vertex. Pure
 * arithmetic over the layout above, so the offsets cannot drift from the
 * packing done by GlQuad_Interleave. */
void GlQuad_Layout(unsigned int *stride, unsigned int *pos_offset, unsigned int *tc_offset);

/* Packs four corners into the interleaved array: out[0..23], six floats per
 * vertex, in the order GlQuad_Layout describes. */
void GlQuad_Interleave(float *out, const GlQuadVerts *q);

/*
 * Modelview for one sprite, mirroring the sequence Gl_DrawSprite leaves on the
 * GL matrix stack:
 *
 *   T(offset) T(flip) T(x,y) S(scale2*sx2,sy2) Rx(angle_y) Ry(angle_z)
 *   Rz(-angle) * SH(shear) * S(scale_x*sx1, scale_y*sy1) T(-anchor)
 *
 * GLES has no matrix stack, so this is built on the CPU and handed to the
 * vertex shader. Every term is kept in the order above: the flips and the shear
 * sit between different scales, and reordering them changes the picture.
 */
typedef struct GlSpriteMv {
    int paletted;           /* selects the palette offset pair below */
    float pal_offset;       /* g_glPalOffset (paletted sprites) */
    float rgba_offset_x;    /* g_glRgbaOffsetX (true-colour sprites) */
    float rgba_offset_y;    /* g_glRgbaOffsetY */
    float x, y;             /* DrawParams.x / .y */
    float angle;            /* DrawParams.angle, rotated by -angle */
    float angle_x;          /* DrawParams.angle_y */
    float angle_z;          /* DrawParams.angle_z */
    float shear_x, shear_y; /* DrawParams.shear_x / .shear_y */
    float scale_x, scale_y;   /* DrawParams.scale_x / .scale_y */
    float scale2_x, scale2_y; /* DrawParams.scale2_x / .scale2_y */
    int sx1, sy1;           /* flip signs of dp->flip  (-1 or 1) */
    int sx2, sy2;           /* flip signs of dp->flip2 (-1 or 1) */
    float anchor_x, anchor_y; /* sprite axis (spr->x / spr->y) */
} GlSpriteMv;

void GlMvp_SpriteModelView(GlMvp *out, const GlSpriteMv *in);

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_GL_MVP_H */
