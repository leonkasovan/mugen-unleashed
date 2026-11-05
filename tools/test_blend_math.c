/*
 * Blend-factor gate (M8, RENDERING_PLAN section 12).
 *
 * Checks that the GLES blend state GlesBlend_Resolve() asks the GPU for is the
 * same operation the engine performs in software (softblend.c):
 *
 *     dst = src * alpha_src + dst * alpha_dst
 *
 * by emulating the GL blend pipeline from the chosen factors and comparing it
 * with a direct implementation of that equation. No GPU is involved, so this
 * runs on the host and catches factor mistakes that a screenshot average would
 * hide.
 *
 * The failure this exists for: the first GLES backend used 1 - alpha_dst as the
 * destination factor. With alpha_dst = 1 an additive sprite multiplied the
 * background by zero, so ADD red over blue produced red instead of magenta and
 * SUB produced black. That configuration is asserted against below by name.
 */
#include <stdio.h>
#include <string.h>

#include "gles_blend.h"

/* Engine constants, mirroring the engine's own enums (src/gfx.h). */
#define BLEND_NONE 0
#define BLEND_ADD  1
#define BLEND_SUB  2

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Factor value as the GPU would compute it (alpha is 0..1). */
static double factor_value(GlesBlendFactor f, double src_a, double dst_a)
{
    switch (f) {
    case GLES_BLEND_FACTOR_ZERO:
        return 0.0;
    case GLES_BLEND_FACTOR_ONE:
        return 1.0;
    case GLES_BLEND_FACTOR_SRC_ALPHA:
        return src_a;
    case GLES_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        return 1.0 - src_a;
    case GLES_BLEND_FACTOR_CONSTANT_ALPHA:
        return dst_a;
    case GLES_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
        return 1.0 - dst_a;
    default:
        return 0.0;
    }
}

/*
 * Emulates what the GPU does with the chosen state, in the same 0..255 space
 * the software renderer works in.
 */
static double gl_result(const GlesBlendState *st, double src, double dst, double src_a, double dst_a)
{
    double sf = factor_value(st->src, src_a, dst_a);
    double df = factor_value(st->dst, src_a, dst_a);

    if (!st->enabled) {
        return src;
    }
    if (st->eq == GLES_BLEND_EQ_REVERSE_SUBTRACT) {
        /* GL: dst * dstFactor - src * srcFactor. */
        double v = dst * df - src * sf;

        return v < 0.0 ? 0.0 : v;
    }
    {
        double v = src * sf + dst * df;

        return v > 255.0 ? 255.0 : v;
    }
}

/* The engine's equation, straight from softblend.c (256 = 1.0). */
static double engine_result(double src, double dst, double src_a, double dst_a, int subtract)
{
    double a_src = src_a * 256.0;
    double a_dst = dst_a * 256.0;
    double v;

    if (a_dst == 0.0) {
        /* BLEND_OP_COPY: the scaled source replaces the target. */
        v = src * a_src / 256.0;
    } else if (a_src + a_dst <= 256.0) {
        v = (src * a_src + dst * a_dst) / 256.0;
    } else {
        /* Saturating variants clamp at 255. */
        v = (src * a_src + dst * a_dst) / 256.0;
        if (v > 255.0) {
            v = 255.0;
        }
    }
    if (subtract) {
        /* The software subtractive path removes the source from the target. */
        v = dst * (a_dst / 256.0) - src * (a_src / 256.0);
        if (v < 0.0) {
            v = 0.0;
        }
    }
    return v;
}

int main(void)
{
    static const double alphas[] = { 0.0, 0.25, 0.5, 1.0 };
    const int na = (int)(sizeof(alphas) / sizeof(alphas[0]));
    const double src = 200.0; /* red on an 8-bit channel */
    const double dst = 80.0;  /* a darker blue-ish background */
    int i;
    int j;

    /* --- the ADD/SUB agreement that the inverted factor broke ------------- */
    for (i = 0; i < na; i++) {
        for (j = 0; j < na; j++) {
            double a_src = alphas[i];
            double a_dst = alphas[j];
            GlesBlendState add = GlesBlend_ResolveOp(BLEND_ADD, 0, 0);
            GlesBlendState sub = GlesBlend_ResolveOp(BLEND_SUB, 0, 1);
            double got_add = gl_result(&add, src, dst, a_src, a_dst);
            double got_sub = gl_result(&sub, src, dst, a_src, a_dst);
            double want_add = engine_result(src, dst, a_src, a_dst, 0);
            double want_sub = engine_result(src, dst, a_src, a_dst, 1);
            char label[128];

            /* One unit of slack: the software path truncates to integers. */
            snprintf(label, sizeof(label), "ADD matches the engine at src=%.2f dst=%.2f", a_src, a_dst);
            check(got_add - want_add <= 1.0 && want_add - got_add <= 1.0, label);
            snprintf(label, sizeof(label), "SUB matches the engine at src=%.2f dst=%.2f", a_src, a_dst);
            check(got_sub - want_sub <= 1.0 && want_sub - got_sub <= 1.0, label);
        }
    }

    /* --- the specific symptom: alpha_dst = 1 must not erase the background -- */
    {
        GlesBlendState add = GlesBlend_ResolveOp(BLEND_ADD, 0, 0);
        double got = gl_result(&add, 255.0, 255.0, 1.0, 1.0);

        check(factor_value(add.dst, 1.0, 1.0) == 1.0, "destination factor is alpha_dst, not 1 - alpha_dst");
        check(got >= 255.0, "ADD at full source and destination alpha keeps the background");
    }

    /* --- alpha_dst = 0 replaces the target (BLEND_OP_COPY) ---------------- */
    {
        GlesBlendState add = GlesBlend_ResolveOp(BLEND_ADD, 0, 0);
        double got = gl_result(&add, src, dst, 1.0, 0.0);

        check(got - src <= 1.0 && src - got <= 1.0, "alpha_dst 0 replaces the target with the source");
    }

    /* --- masked source-over is the ordinary over operator ------------------ */
    {
        GlesBlendState st = GlesBlend_Resolve(BLEND_NONE, 1);
        double got = gl_result(&st, 255.0, 0.0, 0.5, 1.0);

        check(st.enabled && st.src == GLES_BLEND_FACTOR_SRC_ALPHA &&
                  st.dst == GLES_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              "masked draws use source-over");
        /* 50% red on black is ~128, not 255: the double-count this guards. */
        check(got - 127.5 <= 1.0 && 127.5 - got <= 1.0, "50% alpha over black stays near half");
    }

    /* --- opaque, unmasked draws do not blend at all ------------------------ */
    {
        GlesBlendState st = GlesBlend_Resolve(BLEND_NONE, 0);

        check(!st.enabled, "unmasked draws with no blend mode disable blending");
    }

    if (failures != 0) {
        printf("blend-math: %d FAILURES\n", failures);
        return 1;
    }
    printf("blend-math: GLES blend factors match the engine equation\n");
    return 0;
}
