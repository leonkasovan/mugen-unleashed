/*
 * Primitive colour/blend gate (M8).
 *
 * Checks GlesPrim_Resolve/GlesPrim_Color (src/gles_prim.h) against the desktop
 * renderer's prim.c. Two bugs motivated it, both of which reached a device:
 *
 *   1. Colours decoded as RGB565 although Video_MapRGB produces 0x00BBGGRR for
 *      a GL renderer, so every primitive had the wrong hue.
 *   2. The additive fill entry point (g_gfxFillRectColor in GL mode) resolved to
 *      an opaque fill with 1-alpha_dst on the destination, so the title
 *      screen's active-cursor bar painted over the menu instead of adding to
 *      it.
 *
 * No GPU involved: the resolved state is applied to a software model of the GL
 * blend pipeline and compared with the equation prim.c implements.
 */
#include <stdio.h>

#include "gles_prim.h"

/* Engine constants that matter here. */
#define ALPHA_ONE 0x100

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Emulates the GL pipeline for a resolved primitive state, in 0..255. */
static double apply(const GlesPrimState *st, double opacity, double src, double dst)
{
    double sf, df, v;

    if (!st->blend) {
        return src;
    }
    /* GlesPrim_Color already multiplied the colour by st->scale (alpha_src). */
    src = src * st->scale;
    /* glBlendFunc(GL_ONE, GL_SRC_ALPHA): source factor ONE, destination factor
     * the fragment alpha, which carries alpha_dst. */
    sf = 1.0;
    df = st->alpha;
    if (st->eq == GLES_PRIM_EQ_REVERSE_SUBTRACT) {
        v = dst * df - src * sf;
        return v < 0.0 ? 0.0 : v;
    }
    v = src * sf + dst * df;
    return v > 255.0 ? 255.0 : v;
}

int main(void)
{
    float r, g, b, a;
    GlesPrimState st;

    /* --- 1. Colour layout: 0x00BBGGRR, red in the low byte ------------- */
    GlesPrim_SplitColor(0x000000FFu, &r, &g, &b);
    check(r > 0.99f && g < 0.01f && b < 0.01f, "0x000000FF decodes as pure red (low byte is R)");

    GlesPrim_SplitColor(0x0000FF00u, &r, &g, &b);
    check(g > 0.99f && r < 0.01f && b < 0.01f, "0x0000FF00 decodes as pure green");

    GlesPrim_SplitColor(0x00FF0000u, &r, &g, &b);
    check(b > 0.99f && r < 0.01f && g < 0.01f, "0x00FF0000 decodes as pure blue");

    /*
     * The exact colour the cursor pulse produces: Video_MapRGB(c/2, c, c) with
     * c around 32 gives 0x00202010-ish -- a pale blue-grey. Under an RGB565
     * decode this same value reads as almost pure blue, which is what the dark
     * navy bar was.
     */
    {
        unsigned int cursor = (0x20u << 16) | (0x20u << 8) | 0x10u; /* B=0x20 G=0x20 R=0x10 */

        GlesPrim_SplitColor(cursor, &r, &g, &b);
        check(b >= g && g > r, "the cursor colour is blue-grey, not pure blue");
        check(b < 0.20f, "the cursor colour is pale, not saturated");
    }

    /* --- 2. Scale and alpha are applied separately ---------------------- */
    GlesPrim_Color(0x00808080u, 0.5f, 0.25f, &r, &g, &b, &a);
    check(a > 0.24f && a < 0.26f, "the fragment alpha carries the alpha argument");
    check(r > 0.24f && r < 0.27f, "the colour carries the scale");
    GlesPrim_Color(0x00FFFFFFFFu & 0x00FFFFFFu, 2.0f, 4.0f, &r, &g, &b, &a);
    check(r <= 1.0f && g <= 1.0f && b <= 1.0f, "scaled components clamp to 1");
    check(a <= 1.0f, "the fragment alpha clamps to 1");

    /* --- 3. The additive fill must really add --------------------------- */
    st = GlesPrim_Resolve(GLES_PRIM_FILL_RECT_ADD, ALPHA_ONE, ALPHA_ONE);
    check(st.blend == 1, "the additive fill enables blending");
    check(st.eq == GLES_PRIM_EQ_ADD, "the additive fill uses ADD");
    check(st.scale == 1.0f && st.alpha == 1.0f, "the additive fill uses 256/256");
    /* red added over blue is magenta, not red: the bug produced plain red. */
    {
        double out = apply(&st, 1.0, 255.0, 255.0);

        check(out >= 255.0, "additive over a bright background saturates to white");
    }
    {
        double out = apply(&st, 1.0, 128.0, 64.0);

        check(out > 190.0 && out < 194.0, "additive sums source and destination");
    }

    /* --- 4. The plain fill replaces (prim.c uses 0x100, 0) -------------- */
    st = GlesPrim_Resolve(GLES_PRIM_FILL_RECT, ALPHA_ONE, 0);
    check(st.blend == 1, "the plain fill still goes through the blender");
    check(st.alpha == 0.0f, "the plain fill zeroes the destination factor");
    {
        double out = apply(&st, 1.0, 200.0, 17.0);

        check(out > 199.0 && out < 201.0, "the plain fill replaces the destination");
    }

    /* --- 5. Outline variants share the same pairs as the fills ---------- */
    st = GlesPrim_Resolve(GLES_PRIM_DRAW_RECT_ADD, ALPHA_ONE, ALPHA_ONE);
    check(st.blend == 1 && st.eq == GLES_PRIM_EQ_ADD && st.alpha == 1.0f, "the additive outline is additive");
    st = GlesPrim_Resolve(GLES_PRIM_DRAW_RECT, ALPHA_ONE, 0);
    check(st.alpha == 0.0f, "the plain outline zeroes the destination factor");

    /* --- 6. Explicit alphas pass through -------------------------------- */
    st = GlesPrim_Resolve(GLES_PRIM_BLEND_RECT, 0x80, 0x40);
    check(st.scale > 0.49f && st.scale < 0.51f, "blend rect: alpha_src scales the colour");
    check(st.alpha > 0.24f && st.alpha < 0.26f, "blend rect: alpha_dst is the destination factor");
    {
        double out = apply(&st, 1.0, 200.0, 100.0);

        /* 200*0.5 + 100*0.25 = 125 */
        check(out > 124.0 && out < 126.0, "blend rect matches src*a_src + dst*a_dst");
    }

    /* --- 7. Subtract reverses the operands ------------------------------ */
    st = GlesPrim_Resolve(GLES_PRIM_FILL_RECT_SUB, ALPHA_ONE, ALPHA_ONE);
    check(st.eq == GLES_PRIM_EQ_REVERSE_SUBTRACT, "the subtract fill reverses the equation");
    {
        double out = apply(&st, 1.0, 60.0, 200.0);

        check(out > 139.0 && out < 141.0, "subtract removes the source from the destination");
    }
    {
        double out = apply(&st, 1.0, 200.0, 60.0);

        check(out == 0.0, "subtract clamps at zero");
    }

    /* --- 8. Pixels and plain lines do not blend ------------------------- */
    st = GlesPrim_Resolve(GLES_PRIM_PUT_PIXEL, ALPHA_ONE, ALPHA_ONE);
    check(st.blend == 0, "put-pixel writes opaquely");
    st = GlesPrim_Resolve(GLES_PRIM_DRAW_LINE, ALPHA_ONE, ALPHA_ONE);
    check(st.blend == 0, "a plain line writes opaquely");
    st = GlesPrim_Resolve(GLES_PRIM_DRAW_LINE_ADD, ALPHA_ONE, ALPHA_ONE);
    check(st.blend == 1 && st.eq == GLES_PRIM_EQ_ADD, "an additive line blends additively");

    if (failures != 0) {
        printf("prim-math: %d FAILURES\n", failures);
        return 1;
    }
    printf("prim-math: primitive colour and blend state match prim.c\n");
    return 0;
}
