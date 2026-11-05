/*
 * GLES blend state selection (M8), as a pure function.
 *
 * Why this is a separate header: the blend factors have to agree with the
 * engine's own blending equation, which lives in softblend.c:
 *
 *     dst = src * alpha_src + dst * alpha_dst          (BLEND_OP_SCALE)
 *     dst = src * alpha_src                            (alpha_dst == 0)
 *
 * Getting that wrong is invisible in a screenshot average and very visible on
 * screen: the first GLES version used 1 - alpha_dst as the destination factor,
 * so an additive sprite erased the background instead of adding to it. Making
 * the decision a pure function lets tools/test_blend_math.c check it against a
 * software reference of the engine equation without a GPU.
 *
 * The shader folds alpha_src into its output alpha (`color.a *= src_alphamul`),
 * so the source factor is GL_SRC_ALPHA and the constant carries alpha_dst.
 */
#ifndef MUGEN_GLES_BLEND_H
#define MUGEN_GLES_BLEND_H

/* Which equation glBlendEquation gets. */
typedef enum GlesBlendEq {
    GLES_BLEND_EQ_NONE = 0,
    GLES_BLEND_EQ_ADD,
    GLES_BLEND_EQ_REVERSE_SUBTRACT
} GlesBlendEq;

/* The factors, named rather than given as GL enums so this header needs no GL. */
typedef enum GlesBlendFactor {
    GLES_BLEND_FACTOR_ZERO = 0,
    GLES_BLEND_FACTOR_ONE,
    GLES_BLEND_FACTOR_SRC_ALPHA,
    GLES_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    GLES_BLEND_FACTOR_CONSTANT_ALPHA,
    GLES_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
} GlesBlendFactor;

typedef struct GlesBlendState {
    int enabled;
    GlesBlendFactor src;
    GlesBlendFactor dst;
    GlesBlendEq eq;
} GlesBlendState;

/*
 * Resolves the blend state for a draw.
 *
 * blend is the engine's BLEND_NONE / BLEND_ADD / BLEND_SUB; masked is
 * DrawParams.masked. The masked path is ordinary source-over; the additive and
 * subtractive paths are the engine equation above.
 */
static GlesBlendState GlesBlend_Resolve(int blend, int masked)
{
    GlesBlendState st;

    st.enabled = 1;
    st.src = GLES_BLEND_FACTOR_ONE;
    st.dst = GLES_BLEND_FACTOR_ZERO;
    st.eq = GLES_BLEND_EQ_ADD;

    if (blend != 0 /* BLEND_NONE */) {
        st.src = GLES_BLEND_FACTOR_SRC_ALPHA;
        st.dst = GLES_BLEND_FACTOR_CONSTANT_ALPHA;
        st.eq = GLES_BLEND_EQ_ADD;
        return st;
    }
    if (masked != 0) {
        st.src = GLES_BLEND_FACTOR_SRC_ALPHA;
        st.dst = GLES_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        st.eq = GLES_BLEND_EQ_ADD;
        return st;
    }
    st.enabled = 0;
    return st;
}

/*
 * The same decision, but knowing whether the caller wants subtractive blending.
 * BLEND_SUB is a separate engine value from BLEND_ADD, so the equation differs;
 * kept as a second entry point so GlesBlend_Resolve can stay a two-argument
 * call at the draw site.
 */
static GlesBlendState GlesBlend_ResolveOp(int blend, int masked, int subtract)
{
    GlesBlendState st = GlesBlend_Resolve(blend, masked);

    if (st.enabled && subtract) {
        st.eq = GLES_BLEND_EQ_REVERSE_SUBTRACT;
    }
    return st;
}

#endif /* MUGEN_GLES_BLEND_H */
