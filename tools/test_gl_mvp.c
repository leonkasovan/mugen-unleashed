/*
 * Host gate for the GLES draw path's CPU-side math (RENDERING_PLAN.md V3 /
 * section 12.5): MVP construction and quad vertex/texcoord generation.
 *
 * The expected values are written out as literals with their derivation in
 * comments, so they are independent of src/gl_mvp.c rather than a copy of it.
 * No GL context is needed: this is the math the GLES vertex shader receives.
 *
 * Usage: tools/test_gl_mvp.sh
 */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "gl_mvp.h"
#include "test_abi.h"

/* The engine's defaults (src/globals.c): g_glNear, g_glFar, g_glZScale. */
#define NEAR 1000.0
#define FAR  10000.0
#define ZSC  1.0

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int nearly(double a, double b)
{
    return fabs(a - b) <= (fabs(b) > 1e-9 ? fabs(b) * 1e-5 : 1e-7);
}

static void check_matf(const GlMvp *a, const GlMvp *b, const char *what)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (!nearly((double)a->m[i], (double)b->m[i])) {
            printf("FAIL: %s: m[%d] = %.9g, expected %.9g\n", what, i, (double)a->m[i], (double)b->m[i]);
            failures++;
            return;
        }
    }
}

static void check_mat(const GlMvp *m, const double *want, const char *what)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (!nearly((double)m->m[i], want[i])) {
            printf("FAIL: %s: m[%d] = %.9g, expected %.9g\n", what, i, (double)m->m[i], want[i]);
            failures++;
            return;
        }
    }
}

/*
 * Orthographic projection, screen orientation (framebuffer = 0).
 * glOrtho(0, 640, 480, 0, 1000, 10000) then glTranslated(0, 0, -2000):
 *   m0  = 2/(640-0)          = 1/320
 *   m5  = 2/(0-480)          = -1/240
 *   m10 = -2/(10000-1000)    = -1/4500
 *   m12 = -(640+0)/(640-0)   = -1
 *   m13 = -(0+480)/(0-480)   = 1
 *   m14 = m10 * (-2000) + (-(10000+1000)/9000) = 4/9 - 11/9 = -7/9
 */
static void test_ortho_screen(void)
{
    GlMvp m;
    double want[16] = { 0 };
    float x, y, w;

    GlMvp_SetupView(&m, 0, 640, 480, 0, NEAR, FAR, ZSC);
    want[0] = 1.0 / 320.0;
    want[5] = -1.0 / 240.0;
    want[10] = -1.0 / 4500.0;
    want[12] = -1.0;
    want[13] = 1.0;
    want[14] = -7.0 / 9.0;
    want[15] = 1.0;
    check_mat(&m, want, "ortho 640x480 screen");

    /* y = 0 is the TOP of the screen: NDC y = +1. */
    GlMvp_Transform(&m, 0.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly(x, -1.0) && nearly(y, 1.0) && nearly(w, 1.0), "ortho screen: (0,0) -> (-1,+1)");

    /* y = 480 is the bottom: NDC y = -1. x = 640 is the right edge: +1. */
    GlMvp_Transform(&m, 640.0f, 480.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly(x, 1.0) && nearly(y, -1.0), "ortho screen: (640,480) -> (+1,-1)");
}

/* Same projection with framebuffer = 1: swing the ortho's bottom/top, so
 * y = 0 is the BOTTOM and y = 480 the top. m13 becomes -(480+0)/480 = -1. */
static void test_ortho_framebuffer(void)
{
    GlMvp m;
    double want[16] = { 0 };
    float x, y, w;

    GlMvp_SetupView(&m, 0, 640, 480, 1, NEAR, FAR, ZSC);
    want[0] = 1.0 / 320.0;
    want[5] = 1.0 / 240.0;
    want[10] = -1.0 / 4500.0;
    want[12] = -1.0;
    want[13] = -1.0;
    want[14] = -7.0 / 9.0;
    want[15] = 1.0;
    check_mat(&m, want, "ortho 640x480 framebuffer");

    GlMvp_Transform(&m, 0.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly(x, -1.0) && nearly(y, -1.0), "ortho fb: (0,0) -> (-1,-1)");
    GlMvp_Transform(&m, 640.0f, 480.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly(x, 1.0) && nearly(y, 1.0), "ortho fb: (640,480) -> (+1,+1)");
}

/*
 * Perspective (g_glProjection 1). For a 640x480 target with framebuffer = 1:
 *   a = -480*0.25 = -120, b = 480*0.25 = 120, so glFrustum(-160, 160, -120, 120, 1000, 10000)
 *   then glTranslated(0.5*-640, -480*0.5, -2000) = (-320, -240, -2000).
 *
 * A vertex is projected as frustum * translate * v, so the translate applies
 * first. The frustum's left near edge is x' = -160 at z' = -1000, which is
 * world (x - 320 = -160 -> x = 160, z = 1000). It must land on NDC x = -1:
 * x_clip = (2*1000/320) * -160 = -1000, w_clip = -z' = 1000, so x/w = -1.
 * The right edge is symmetric: world x = 480 -> NDC +1.
 */
static void test_perspective(void)
{
    GlMvp m;
    float x, y, z, w;

    GlMvp_SetupView(&m, 1, 640, 480, 1, NEAR, FAR, ZSC);

    GlMvp_Transform(&m, 160.0f, 240.0f, 1000.0f, &x, &y, &z, &w);
    check(w > 0.0f, "perspective: w is positive in front of the camera");
    check(nearly((double)(x / w), -1.0), "perspective: left near edge -> NDC x = -1");
    check(nearly((double)(y / w), 0.0), "perspective: centre row -> NDC y = 0");

    GlMvp_Transform(&m, 480.0f, 240.0f, 1000.0f, &x, &y, &z, &w);
    check(nearly((double)(x / w), 1.0), "perspective: right near edge -> NDC x = +1");

    /* Behind the camera (z' > 0) flips w negative -- the guard a GLES backend
     * must not draw with. */
    GlMvp_Transform(&m, 320.0f, 240.0f, 3000.0f, &x, &y, &z, &w);
    check(w < 0.0f, "perspective: a point behind the camera has negative w");
}

/* Z scale multiplies column 2 only (m10 and its translated m14). */
static void test_zscale(void)
{
    GlMvp a, b;

    GlMvp_SetupView(&a, 0, 640, 480, 0, NEAR, FAR, 1.0);
    GlMvp_SetupView(&b, 0, 640, 480, 0, NEAR, FAR, 2.0);
    check(nearly((double)b.m[10], (double)a.m[10] * 2.0), "z scale doubles m10");
    check(nearly((double)b.m[0], (double)a.m[0]), "z scale leaves m0 alone");
    check(nearly((double)b.m[14], (double)a.m[14]), "z scale leaves m14 alone (it is column 3)");
}

/* Modelview is identity and multiply(proj, identity) == proj. */
static void test_identity(void)
{
    GlMvp p, i, out;
    int n;

    GlMvp_SetupView(&p, 0, 320, 240, 0, NEAR, FAR, ZSC);
    GlMvp_Identity(&i);
    for (n = 0; n < 16; n++) {
        check(nearly((double)i.m[n], (n % 5 == 0) ? 1.0 : 0.0), "identity matrix");
    }
    GlMvp_Multiply(&out, &p, &i);
    check_matf(&out, &p, "proj * identity == proj");
}

/* ---- quad generation ---- */

static void test_quad_plain(void)
{
    GlQuadVerts q;
    float v[8] = { 10.0f, 20.0f, 30.0f, 20.0f, 10.0f, 40.0f, 30.0f, 40.0f };
    int c;

    GlQuad_Build(&q, v, 0, 0.0, 1.0, 0.0, 0.0, 1.0f);
    check(q.count == 4, "quad has four corners");
    for (c = 0; c < 4; c++) {
        check(nearly((double)q.pos[c][0], (double)v[c * 2]), "quad corner x");
        check(nearly((double)q.pos[c][1], (double)v[c * 2 + 1]), "quad corner y");
        check(nearly((double)q.tc[c][3], 1.0), "plain quad q = 1");
    }
    check(nearly((double)q.tc[0][0], 0.0) && nearly((double)q.tc[0][1], 0.0), "plain tc0 = (0,0)");
    check(nearly((double)q.tc[1][0], 1.0) && nearly((double)q.tc[1][1], 0.0), "plain tc1 = (1,0)");
    check(nearly((double)q.tc[2][0], 0.0) && nearly((double)q.tc[2][1], 1.0), "plain tc2 = (0,1)");
    check(nearly((double)q.tc[3][0], 1.0) && nearly((double)q.tc[3][1], 1.0), "plain tc3 = (1,1)");
}

/*
 * Stretched quad with c0 = 0.5, c1 = 0.75, no offsets, width_ratio 1:
 *   q  = c0/c1                        = 2/3
 *   s0 = ((1-0.5) - 0) * 0.5          = 0.25
 *   s1 = ((0.5+1) - 0) * 0.5          = 0.75
 *   s2 = q * (((1-0.75) - 0) * 0.5)   = (2/3) * 0.125 = 1/12
 *   s3 = q * ((0.75+1) - 0) * 0.5     = (2/3) * 0.875 = 7/12
 * The top edge keeps q = 1; only the bottom edge carries the projection.
 */
static void test_quad_special(void)
{
    GlQuadVerts q;
    float v[8] = { 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 50.0f, 100.0f, 50.0f };

    GlQuad_Build(&q, v, 1, 0.5, 0.75, 0.0, 0.0, 1.0f);
    check(nearly((double)q.tc[0][3], 1.0), "special: top-left q = 1");
    check(nearly((double)q.tc[1][3], 1.0), "special: top-right q = 1");
    check(nearly((double)q.tc[2][3], 2.0 / 3.0), "special: bottom-left q = c0/c1");
    check(nearly((double)q.tc[3][3], 2.0 / 3.0), "special: bottom-right q = c0/c1");
    check(nearly((double)q.tc[0][0], 0.25), "special: s0 = 0.25");
    check(nearly((double)q.tc[1][0], 0.75), "special: s1 = 0.75");
    check(nearly((double)q.tc[2][0], 1.0 / 12.0), "special: s2 = 1/12");
    check(nearly((double)q.tc[3][0], 7.0 / 12.0), "special: s3 = 7/12");
    check(nearly((double)q.tc[2][1], 1.0) && nearly((double)q.tc[3][1], 1.0), "special: bottom t = 1");
}

/* width_ratio divides q (never multiplies), and 0 leaves q untouched. */
static void test_quad_width_ratio(void)
{
    GlQuadVerts a, b, c;
    float v[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

    GlQuad_Build(&a, v, 1, 0.5, 0.75, 0.0, 0.0, 1.0f);
    GlQuad_Build(&b, v, 1, 0.5, 0.75, 0.0, 0.0, 2.0f);
    check(nearly((double)b.tc[2][3], (double)a.tc[2][3] / 2.0), "width_ratio divides q");

    GlQuad_Build(&c, v, 1, 0.5, 0.75, 0.0, 0.0, 0.0f);
    check(nearly((double)c.tc[2][3], 2.0 / 3.0), "width_ratio 0 leaves q alone");
}

/* top_offset / bottom_offset shift the vertical texcoords of their edge. */
static void test_quad_offsets(void)
{
    GlQuadVerts q;
    float v[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

    /* top_offset 0.1 -> c2x2 = 0.2, so s0 = ((1-0.5) - 0.2) * 0.5 = 0.15. */
    GlQuad_Build(&q, v, 1, 0.5, 0.75, 0.1, 0.0, 1.0f);
    check(nearly((double)q.tc[0][0], 0.15), "top_offset shifts the top edge");
}

/*
 * Interleaved layout. One vertex is 6 floats: position at offset 0, texcoord
 * at offset 2 floats = 8 bytes, stride 24 bytes. The packing must place each
 * component exactly there, q included -- this is the contract between
 * GlQuad_Interleave and the attribute pointers in src/gles_draw.c.
 */
static void test_interleave(void)
{
    GlQuadVerts q;
    float v[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    float out[GL_QUAD_VERTEX_COUNT * GL_QUAD_VERTEX_FLOATS];
    unsigned int stride = 0, pos_off = 1, tc_off = 0;
    int i;

    GlQuad_Build(&q, v, 1, 0.5, 0.75, 0.0, 0.0, 1.0f);
    memset(out, 0, sizeof(out));
    GlQuad_Interleave(out, &q);

    GlQuad_Layout(&stride, &pos_off, &tc_off);
    check(stride == 24u, "layout: stride is 6 floats (24 bytes)");
    check(pos_off == 0u, "layout: position at offset 0");
    check(tc_off == 8u, "layout: texcoord at offset 8 (2 floats)");

    for (i = 0; i < GL_QUAD_VERTEX_COUNT; i++) {
        const float *vt = &out[i * GL_QUAD_VERTEX_FLOATS];

        check(nearly((double)vt[0], (double)v[i * 2]), "interleave: x at +0");
        check(nearly((double)vt[1], (double)v[i * 2 + 1]), "interleave: y at +1");
        check(nearly((double)vt[2], (double)q.tc[i][0]), "interleave: s at +2");
        check(nearly((double)vt[3], (double)q.tc[i][1]), "interleave: t at +3");
        check(nearly((double)vt[4], 0.0), "interleave: r at +4");
        check(nearly((double)vt[5], (double)q.tc[i][3]), "interleave: q at +5");
    }
    /* The bottom edge must still carry the projective value, not 1. */
    check(nearly((double)out[2 * GL_QUAD_VERTEX_FLOATS + 5], 2.0 / 3.0),
          "interleave: bottom-left keeps q = c0/c1");
}

/*
 * Sprite modelview: the terms of Gl_DrawSprite's matrix sequence.
 *
 *  - a plain sprite at (x, y) with unit scales is a pure translation by
 *    (x - anchor_x, y - anchor_y);
 *  - scale_x multiplies the x column only;
 *  - flip2 negative on x mirrors and adds the 0.99999899 nudge along x;
 *  - a 180-degree angle negates both axes (with the translation applied
 *    before the rotation, as GL's post-multiply order does).
 */
static void test_sprite_modelview(void)
{
    GlSpriteMv in;
    GlMvp m;
    float x, y, w;

    memset(&in, 0, sizeof(in));
    in.paletted = 1;
    in.pal_offset = 0.0f;
    in.scale_x = in.scale_y = 1.0f;
    in.scale2_x = in.scale2_y = 1.0f;
    in.sx1 = in.sy1 = 1;
    in.sx2 = in.sy2 = 1;
    in.anchor_x = 10.0f;
    in.anchor_y = 20.0f;

    /* Plain: point (0,0) lands on (x - anchor). */
    in.x = 100.0f;
    in.y = 200.0f;
    GlMvp_SpriteModelView(&m, &in);
    GlMvp_Transform(&m, 0.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 90.0) && nearly((double)y, 180.0), "mv: plain sprite translates to (x-ax, y-ay)");
    check(nearly((double)w, 1.0), "mv: w stays 1 for an affine modelview");

    /*
     * scale_x = 2 scales the offset measured from the anchor: the point at
     * anchor+10 lands 20 past x. (M = T(x,y) * S * T(-anchor), so the anchor
     * itself always maps to (x, y).)
     */
    in.scale_x = 2.0f;
    GlMvp_SpriteModelView(&m, &in);
    GlMvp_Transform(&m, 20.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 100.0 + 20.0), "mv: scale_x scales the offset from the anchor");
    GlMvp_Transform(&m, 10.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 100.0), "mv: scale_x keeps the anchor at x");
    GlMvp_Transform(&m, 0.0f, 10.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)y, 190.0), "mv: scale_x leaves y alone");
    in.scale_x = 1.0f;

    /*
     * Horizontal flip through flip2: the nudge is applied first in the chain
     * (T(offset) T(flip) ...), so it adds to the final position after the
     * mirror. Point at the anchor maps to x + 0.99999899; a point 6 left of the
     * anchor mirrors to 6 right of it.
     */
    in.sx2 = -1;
    GlMvp_SpriteModelView(&m, &in);
    GlMvp_Transform(&m, 10.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 100.0 + 0.99999899), "mv: flip2 x nudges by 0.99999899");
    GlMvp_Transform(&m, 4.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 100.0 + 6.0 + 0.99999899), "mv: flip2 x mirrors about the anchor");
    in.sx2 = 1;

    /* 180 degrees: both axes negate about the anchor. */
    in.angle = 180.0f;
    GlMvp_SpriteModelView(&m, &in);
    GlMvp_Transform(&m, 10.0f, 20.0f, 0.0f, &x, &y, NULL, &w);
    /* The anchor maps to the translation point (100, 200). */
    check(nearly((double)x, 100.0) && nearly((double)y, 200.0), "mv: 180deg keeps the anchor at (x,y)");
    GlMvp_Transform(&m, 0.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 110.0) && nearly((double)y, 220.0), "mv: 180deg mirrors about the anchor");
    in.angle = 0.0f;

    /* Paletted and true-colour sprites use different half-texel offsets. */
    in.paletted = 0;
    in.rgba_offset_x = 0.5f;
    in.rgba_offset_y = 0.25f;
    GlMvp_SpriteModelView(&m, &in);
    GlMvp_Transform(&m, 0.0f, 0.0f, 0.0f, &x, &y, NULL, &w);
    check(nearly((double)x, 90.5) && nearly((double)y, 180.25), "mv: true-colour offset is applied");
}

int main(void)
{
    TEST_REPORT_ABI();
    test_ortho_screen();
    test_ortho_framebuffer();
    test_perspective();
    test_zscale();
    test_identity();
    test_quad_plain();
    test_quad_special();
    test_quad_width_ratio();
    test_quad_offsets();
    test_interleave();
    test_sprite_modelview();

    if (failures != 0) {
        printf("gl-mvp: %d FAILURES\n", failures);
        return 1;
    }
    printf("gl-mvp: MVP and quad gates pass\n");
    return 0;
}
