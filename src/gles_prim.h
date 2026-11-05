/*
 * GLES primitive colour and blend state (M8), as pure functions.
 *
 * Why this is a header and not inline code in gles_render.c: the primitives
 * carried two colour/alpha mistakes in a row, and both were invisible to every
 * existing test. Splitting the decision out lets tools/test_prim_math.c check it
 * against the desktop renderer's prim.c without a GPU.
 *
 * The two rules, both taken from prim.c:
 *
 * 1. Colour layout. Video_MapRGB returns 0x00BBGGRR whenever a GL renderer is
 *    active (video.c), so red is the LOW byte and blue the high one.
 *    Prim_GlColor reads exactly that. Decoding it as RGB565 -- which the first
 *    GLES version did -- gives every primitive the wrong hue.
 *
 * 2. Blend model.
 *
 *        rgb * alpha_src                       colour, pre-scaled
 *        glBlendFunc(GL_ONE, GL_SRC_ALPHA)     fragment alpha carries alpha_dst
 *        glBlendEquation(ADD | REVERSE_SUBTRACT)
 *
 *    which yields dst' = rgb*alpha_src + dst*alpha_dst, the engine's equation.
 *    The first GLES version used CONSTANT_ALPHA / ONE_MINUS_CONSTANT_ALPHA,
 *    i.e. dst' = rgb*a + dst*(1-a), and mapped the additive fill entry point to
 *    an opaque one -- so the title screen's active-cursor bar, which is drawn
 *    through g_gfxFillRectColor, painted over the menu instead of adding to it.
 */
#ifndef MUGEN_GLES_PRIM_H
#define MUGEN_GLES_PRIM_H

/* Blend equation, named so this header needs no GL. */
typedef enum GlesPrimEq {
    GLES_PRIM_EQ_NONE = 0,
    GLES_PRIM_EQ_ADD,
    GLES_PRIM_EQ_REVERSE_SUBTRACT
} GlesPrimEq;

typedef struct GlesPrimState {
    int blend;        /* 0: blending disabled (opaque write) */
    GlesPrimEq eq;
    float scale;      /* multiplies the colour; equals alpha_src / 256 */
    float alpha;      /* fragment alpha; equals alpha_dst / 256 */
} GlesPrimState;

/* The engine's primitive entry points, each with prim.c's alpha pair. */
typedef enum GlesPrimKind {
    GLES_PRIM_PUT_PIXEL = 0,   /* no blending */
    GLES_PRIM_ADD_PIXEL,       /* ADD, 256/256 */
    GLES_PRIM_DRAW_LINE,       /* no blending */
    GLES_PRIM_DRAW_LINE_ADD,   /* ADD, 256/256 */
    GLES_PRIM_DRAW_RECT,       /* outline, 256/0   */
    GLES_PRIM_DRAW_RECT_ADD,   /* outline, ADD, 256/256 */
    GLES_PRIM_DRAW_RECT_BLEND, /* outline, explicit alphas */
    GLES_PRIM_DRAW_RECT_SUB,   /* outline, REVERSE_SUBTRACT, 256/256 */
    GLES_PRIM_FILL_RECT,       /* filled, 256/0    */
    GLES_PRIM_FILL_RECT_ADD,   /* filled, ADD, 256/256 -- g_gfxFillRectColor */
    GLES_PRIM_FILL_RECT_SUB,   /* filled, REVERSE_SUBTRACT, 256/256 */
    GLES_PRIM_BLEND_RECT       /* filled, explicit alphas */
} GlesPrimKind;

#define GLES_PRIM_ALPHA_ONE 0x100

/* Decodes a colour as Prim_GlColor does: 0x00BBGGRR, red in the low byte. */
static void GlesPrim_SplitColor(unsigned int color, float *r, float *g, float *b)
{
    *r = (float)(color & 0xFF) / 255.0f;
    *g = (float)((color >> 8) & 0xFF) / 255.0f;
    *b = (float)((color >> 16) & 0xFF) / 255.0f;
}

/* Prim_GlColor: the colour scaled by `scale`, with `alpha` as fragment alpha.
 * The two stay separate so the blend can carry alpha_dst in the source alpha
 * while the colour carries alpha_src. */
static void GlesPrim_Color(unsigned int color, float scale, float alpha, float *r, float *g, float *b, float *a)
{
    GlesPrim_SplitColor(color, r, g, b);
    *r = *r * scale;
    *g = *g * scale;
    *b = *b * scale;
    if (*r > 1.0f) {
        *r = 1.0f;
    }
    if (*g > 1.0f) {
        *g = 1.0f;
    }
    if (*b > 1.0f) {
        *b = 1.0f;
    }
    *a = (alpha > 1.0f) ? 1.0f : ((alpha < 0.0f) ? 0.0f : alpha);
}

/*
 * Resolves the state for one entry point. alpha_src/alpha_dst are in 256ths and
 * ignored by the kinds that have a fixed pair, as in prim.c.
 */
static GlesPrimState GlesPrim_Resolve(GlesPrimKind kind, int alpha_src, int alpha_dst)
{
    GlesPrimState st;

    st.blend = 1;
    st.eq = GLES_PRIM_EQ_ADD;
    st.scale = 1.0f;
    st.alpha = 1.0f;

    switch (kind) {
    case GLES_PRIM_PUT_PIXEL:
    case GLES_PRIM_DRAW_LINE:
        st.blend = 0;
        break;
    case GLES_PRIM_ADD_PIXEL:
    case GLES_PRIM_DRAW_LINE_ADD:
        break;
    case GLES_PRIM_DRAW_RECT:
    case GLES_PRIM_FILL_RECT:
        /* prim.c: Gfx_GlBlendRect(..., 0x100, 0) -- the destination is
         * multiplied by zero, so the colour replaces it. */
        st.scale = 1.0f;
        st.alpha = 0.0f;
        break;
    case GLES_PRIM_DRAW_RECT_ADD:
    case GLES_PRIM_FILL_RECT_ADD:
        /* prim.c: Gfx_GlBlendRect(..., 0x100, 0x100) -- additive. */
        st.scale = 1.0f;
        st.alpha = 1.0f;
        break;
    case GLES_PRIM_DRAW_RECT_SUB:
    case GLES_PRIM_FILL_RECT_SUB:
        st.eq = GLES_PRIM_EQ_REVERSE_SUBTRACT;
        st.scale = 1.0f;
        st.alpha = 1.0f;
        break;
    case GLES_PRIM_DRAW_RECT_BLEND:
    case GLES_PRIM_BLEND_RECT:
        st.scale = (float)alpha_src * (1.0f / 256.0f);
        st.alpha = (float)alpha_dst * (1.0f / 256.0f);
        break;
    default:
        st.blend = 0;
        break;
    }
    return st;
}

#endif /* MUGEN_GLES_PRIM_H */
