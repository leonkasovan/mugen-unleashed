/*
 * GLES draw path: MVP construction and quad vertex generation.
 * See gl_mvp.h for why this exists and what it replaces.
 */
#include <math.h>
#include <string.h>

#include "gl_mvp.h"

/* Column-major helpers: index = column * 4 + row. */
#define M(c, r) ((c) * 4 + (r))

static void mat_identity(double *m)
{
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = 0.0;
    }
    m[0] = 1.0;
    m[5] = 1.0;
    m[10] = 1.0;
    m[15] = 1.0;
}

/* out = a * b (b then a applied to a column vector). */
static void mat_mul(double *out, const double *a, const double *b)
{
    double r[16];
    int c, row, k;

    for (c = 0; c < 4; c++) {
        for (row = 0; row < 4; row++) {
            double sum = 0.0;
            for (k = 0; k < 4; k++) {
                sum += a[M(k, row)] * b[M(c, k)];
            }
            r[M(c, row)] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

/* glOrtho(left, right, bottom, top, near, far), column-major. */
static void mat_ortho(double *out, double left, double right, double bottom, double top, double near_plane,
                      double far_plane)
{
    double rl = right - left;
    double tb = top - bottom;
    double fn = far_plane - near_plane;

    mat_identity(out);
    out[M(0, 0)] = 2.0 / rl;
    out[M(1, 1)] = 2.0 / tb;
    out[M(2, 2)] = -2.0 / fn;
    out[M(3, 0)] = -(right + left) / rl;
    out[M(3, 1)] = -(top + bottom) / tb;
    out[M(3, 2)] = -(far_plane + near_plane) / fn;
}

/* glFrustum(left, right, bottom, top, near, far), column-major. */
static void mat_frustum(double *out, double left, double right, double bottom, double top, double near_plane,
                        double far_plane)
{
    double rl = right - left;
    double tb = top - bottom;
    double fn = far_plane - near_plane;

    memset(out, 0, 16 * sizeof(double));
    out[M(0, 0)] = 2.0 * near_plane / rl;
    out[M(1, 1)] = 2.0 * near_plane / tb;
    out[M(2, 0)] = (right + left) / rl;
    out[M(2, 1)] = (top + bottom) / tb;
    out[M(2, 2)] = -(far_plane + near_plane) / fn;
    out[M(2, 3)] = -1.0;
    out[M(3, 2)] = -2.0 * far_plane * near_plane / fn;
}

/* glTranslated(tx, ty, tz). */
static void mat_translate(double *out, double tx, double ty, double tz)
{
    mat_identity(out);
    out[M(3, 0)] = tx;
    out[M(3, 1)] = ty;
    out[M(3, 2)] = tz;
}

/* glScaled(sx, sy, sz). */
static void mat_scale(double *out, double sx, double sy, double sz)
{
    mat_identity(out);
    out[M(0, 0)] = sx;
    out[M(1, 1)] = sy;
    out[M(2, 2)] = sz;
}

/* Stores a double matrix as the float matrix GL takes. */
static void mat_store(GlMvp *out, const double *m)
{
    int i;

    for (i = 0; i < 16; i++) {
        out->m[i] = (float)m[i];
    }
}

void GlMvp_Identity(GlMvp *out)
{
    double m[16];

    mat_identity(m);
    mat_store(out, m);
}

void GlMvp_SetupView(GlMvp *out, int projection, int width, int height, int framebuffer, double near_plane,
                     double far_plane, double z_scale)
{
    double proj[16];
    double tmp[16];
    double xlate[16];
    double scale[16];
    double w = (double)width;
    double h = (double)height;

    if (projection == 0) {
        /* Orthographic: the sprite path. Same branch structure as
         * Gl_SetupView, including the framebuffer y flip. */
        int bottom = 0;
        int top = height;

        if (framebuffer == 0) {
            bottom = height;
            top = 0;
        }
        mat_ortho(proj, 0.0, w, (double)bottom, (double)top, near_plane, far_plane);
        mat_translate(xlate, 0.0, 0.0, near_plane * -2.0);
        /* proj = proj * translate */
        mat_mul(tmp, proj, xlate);
        memcpy(proj, tmp, sizeof(proj));
    } else if (projection == 1) {
        double nh = -h;
        double nw = -w;
        double a = nh * 0.25;
        double b = h * 0.25;

        if (framebuffer == 0) {
            double t = a;
            a = b;
            b = t;
        }
        mat_frustum(proj, nw * 0.25, w * 0.25, a, b, near_plane, far_plane);
        mat_translate(xlate, 0.5 * nw, nh * 0.5, near_plane * -2.0);
        mat_mul(tmp, proj, xlate);
        memcpy(proj, tmp, sizeof(proj));
    } else {
        mat_identity(proj);
    }
    /* glScaled(1, 1, z_scale) is applied last, so it is the right-most
     * factor: proj = proj * scale. */
    mat_scale(scale, 1.0, 1.0, z_scale);
    mat_mul(tmp, proj, scale);
    mat_store(out, tmp);
}

void GlMvp_Multiply(GlMvp *out, const GlMvp *projection, const GlMvp *modelview)
{
    double a[16];
    double b[16];
    double r[16];
    int i;

    for (i = 0; i < 16; i++) {
        a[i] = (double)projection->m[i];
        b[i] = (double)modelview->m[i];
    }
    mat_mul(r, a, b);
    mat_store(out, r);
}

void GlMvp_Transform(const GlMvp *mvp, float x, float y, float z, float *x_out, float *y_out, float *z_out,
                     float *w_out)
{
    double in[4];
    double out[4];
    int row, k;

    in[0] = (double)x;
    in[1] = (double)y;
    in[2] = (double)z;
    in[3] = 1.0;
    for (row = 0; row < 4; row++) {
        double sum = 0.0;
        for (k = 0; k < 4; k++) {
            sum += (double)mvp->m[M(k, row)] * in[k];
        }
        out[row] = sum;
    }
    if (x_out != NULL) {
        *x_out = (float)out[0];
    }
    if (y_out != NULL) {
        *y_out = (float)out[1];
    }
    if (z_out != NULL) {
        *z_out = (float)out[2];
    }
    if (w_out != NULL) {
        *w_out = (float)out[3];
    }
}

void GlQuad_Layout(unsigned int *stride, unsigned int *pos_offset, unsigned int *tc_offset)
{
    /* One vertex is GL_QUAD_VERTEX_FLOATS floats, position first. */
    if (stride != NULL) {
        *stride = GL_QUAD_VERTEX_FLOATS * (unsigned int)sizeof(float);
    }
    if (pos_offset != NULL) {
        *pos_offset = 0u;
    }
    if (tc_offset != NULL) {
        *tc_offset = 2u * (unsigned int)sizeof(float);
    }
}

void GlQuad_Interleave(float *out, const GlQuadVerts *q)
{
    int i;

    if (out == NULL || q == NULL) {
        return;
    }
    for (i = 0; i < GL_QUAD_VERTEX_COUNT; i++) {
        out[i * GL_QUAD_VERTEX_FLOATS + 0] = q->pos[i][0];
        out[i * GL_QUAD_VERTEX_FLOATS + 1] = q->pos[i][1];
        out[i * GL_QUAD_VERTEX_FLOATS + 2] = q->tc[i][0];
        out[i * GL_QUAD_VERTEX_FLOATS + 3] = q->tc[i][1];
        out[i * GL_QUAD_VERTEX_FLOATS + 4] = q->tc[i][2];
        out[i * GL_QUAD_VERTEX_FLOATS + 5] = q->tc[i][3];
    }
}

void GlQuad_Build(GlQuadVerts *out, const float *v, int special, double c0, double c1, double top_offset,
                  double bottom_offset, float width_ratio)
{
    int i;

    for (i = 0; i < 4; i++) {
        out->pos[i][0] = v[i * 2];
        out->pos[i][1] = v[i * 2 + 1];
    }
    out->count = 4;

    if (!special) {
        /* Straight sampling: (0,0) (1,0) (0,1) (1,1), q = 1. */
        out->tc[0][0] = 0.0f;
        out->tc[0][1] = 0.0f;
        out->tc[1][0] = 1.0f;
        out->tc[1][1] = 0.0f;
        out->tc[2][0] = 0.0f;
        out->tc[2][1] = 1.0f;
        out->tc[3][0] = 1.0f;
        out->tc[3][1] = 1.0f;
        for (i = 0; i < 4; i++) {
            out->tc[i][2] = 0.0f;
            out->tc[i][3] = 1.0f;
        }
        return;
    }

    {
        /* Stretched quad (Glr_QuadVertices' special branch): the bottom edge
         * gets a projective q so the trapezoid samples correctly. */
        float q = (float)(c0 / c1);
        double c2x2 = top_offset + top_offset;
        double c3x2;
        float s;

        if (0.0f != width_ratio) {
            q = (float)((double)q / (double)width_ratio);
        }
        /* Top edge: q stays 1 (the glMultiTexCoord2f form sets s,t only). */
        s = (float)(((1.0 - c0) - c2x2) * 0.5);
        out->tc[0][0] = s;
        out->tc[0][1] = 0.0f;
        out->tc[0][2] = 0.0f;
        out->tc[0][3] = 1.0f;

        s = (float)((c0 + 1.0 - c2x2) * 0.5);
        out->tc[1][0] = s;
        out->tc[1][1] = 0.0f;
        out->tc[1][2] = 0.0f;
        out->tc[1][3] = 1.0f;

        /* Bottom edge: q is carried on both corners. */
        c3x2 = bottom_offset + bottom_offset;
        s = (float)(q * (((1.0 - c1) - c3x2) * 0.5));
        out->tc[2][0] = s;
        out->tc[2][1] = 1.0f;
        out->tc[2][2] = 0.0f;
        out->tc[2][3] = q;

        s = (float)(q * ((c1 + 1.0 - c3x2) * 0.5));
        out->tc[3][0] = s;
        out->tc[3][1] = 1.0f;
        out->tc[3][2] = 0.0f;
        out->tc[3][3] = q;
    }
}

/* Rotation about X by `deg`, matching glRotatef(deg, 1, 0, 0). */
static void mat_rotate_x(double *out, double deg)
{
    double r = deg * (3.14159265358979323846 / 180.0);
    double c = cos(r);
    double s = sin(r);

    mat_identity(out);
    out[M(1, 1)] = c;
    out[M(2, 1)] = -s;
    out[M(1, 2)] = s;
    out[M(2, 2)] = c;
}

/* Rotation about Y by `deg`, matching glRotatef(deg, 0, 1, 0). */
static void mat_rotate_y(double *out, double deg)
{
    double r = deg * (3.14159265358979323846 / 180.0);
    double c = cos(r);
    double s = sin(r);

    mat_identity(out);
    out[M(0, 0)] = c;
    out[M(2, 0)] = s;
    out[M(0, 2)] = -s;
    out[M(2, 2)] = c;
}

/* Rotation about Z by `deg`, matching glRotatef(deg, 0, 0, 1). */
static void mat_rotate_z(double *out, double deg)
{
    double r = deg * (3.14159265358979323846 / 180.0);
    double c = cos(r);
    double s = sin(r);

    mat_identity(out);
    out[M(0, 0)] = c;
    out[M(1, 0)] = -s;
    out[M(0, 1)] = s;
    out[M(1, 1)] = c;
}

/*
 * The shear matrix Gl_DrawSprite passes to glMultMatrixf: column-major with
 * m[4] = shear_x and m[1] = shear_y, i.e.
 *   x' = x + shear_x * y
 *   y' = shear_y * x + y
 */
static void mat_shear(double *out, double shear_x, double shear_y)
{
    mat_identity(out);
    out[M(1, 0)] = shear_x;
    out[M(0, 1)] = shear_y;
}

/* tmp = m * x, then m = tmp, so a caller can post-multiply in GL's order. */
static void mat_post(double *m, const double *x)
{
    double tmp[16];

    mat_mul(tmp, m, x);
    memcpy(m, tmp, sizeof(tmp));
}

void GlMvp_SpriteModelView(GlMvp *out, const GlSpriteMv *in)
{
    double m[16];
    double t[16];

    if (out == NULL || in == NULL) {
        return;
    }
    mat_identity(m);

    /* Offset: the half-texel shift that puts a sprite's texels on pixel
     * centres, a different pair for paletted and true-colour textures. */
    if (in->paletted) {
        mat_translate(t, (double)in->pal_offset, (double)in->pal_offset, 0.0);
    } else {
        mat_translate(t, (double)in->rgba_offset_x, (double)in->rgba_offset_y, 0.0);
    }
    mat_post(m, t);

    /* Flip nudge: a negative flip2*flip scale mirrors in place, and GL's path
     * adds a hair of translation so the mirrored edge lands on the texel. */
    if ((double)(in->sx2 * in->sx1) * (double)in->scale2_x * (double)in->scale_x < 0.0) {
        mat_translate(t, 0.99999899, 0.0, 0.0);
        mat_post(m, t);
    }
    if ((double)(in->sy2 * in->sy1) * (double)in->scale2_y * (double)in->scale_y < 0.0) {
        mat_translate(t, 0.0, 0.99999899, 0.0);
        mat_post(m, t);
    }

    mat_translate(t, (double)in->x, (double)in->y, 0.0);
    mat_post(m, t);

    mat_scale(t, (double)(in->sx2 * in->scale2_x), (double)(in->sy2 * in->scale2_y), 1.0);
    mat_post(m, t);

    mat_rotate_x(t, (double)in->angle_x);
    mat_post(m, t);
    mat_rotate_y(t, (double)in->angle_z);
    mat_post(m, t);
    mat_rotate_z(t, -(double)in->angle);
    mat_post(m, t);

    mat_shear(t, (double)in->shear_x, (double)in->shear_y);
    mat_post(m, t);

    mat_scale(t, (double)(in->sx1 * in->scale_x), (double)(in->sy1 * in->scale_y), 1.0);
    mat_post(m, t);

    mat_translate(t, -(double)in->anchor_x, -(double)in->anchor_y, 0.0);
    mat_post(m, t);

    mat_store(out, m);
}
