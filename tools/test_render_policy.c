/*
 * Host gate for the renderer selection policy (RENDERING_PLAN.md 10.1/12.2).
 *
 * Checks the full requested-mode x capability table, including that a mode is
 * never granted a capability the caller did not report -- the failure mode
 * section 12.2 warns about (removing the System force alone selects a GL path
 * whose tables are NULL).
 *
 * Usage: tools/test_render_policy.sh
 */
#include <stdio.h>

#include "render_policy.h"
#include "test_abi.h"

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Resolves and asserts one row of the table. */
static void row(int requested, int gl_screen, int gl_sprite, int want_effective, int want_fell_back,
                const char *label)
{
    RenderCaps caps;
    RenderDecision d;

    caps.system = 1;
    caps.gl_screen = gl_screen;
    caps.gl_sprite = gl_sprite;
    RenderPolicy_Resolve(requested, &caps, &d);
    if (d.effective != want_effective || d.fell_back != want_fell_back) {
        printf("FAIL: %s: effective=%d fell_back=%d (wanted %d/%d)\n", label, d.effective, d.fell_back,
               want_effective, want_fell_back);
        failures++;
        return;
    }
    /* A fallback must always carry a reason; a kept mode must not. */
    if (want_fell_back) {
        check(d.reason != NULL, label);
    } else {
        check(d.reason == NULL, label);
    }
}

int main(void)
{
    TEST_REPORT_ABI();
    RenderCaps caps;
    RenderDecision d;

    /* Desktop: everything compiled. Every mode is kept as requested. */
    row(RENDER_POLICY_SYSTEM, 1, 1, RENDER_POLICY_SYSTEM, 0, "desktop: system kept");
    row(RENDER_POLICY_OPENGL_SCREEN, 1, 1, RENDER_POLICY_OPENGL_SCREEN, 0, "desktop: openglscreen kept");
    row(RENDER_POLICY_OPENGL, 1, 1, RENDER_POLICY_OPENGL, 0, "desktop: opengl kept");

    /* Android today: no GL tables compiled at all. */
    row(RENDER_POLICY_SYSTEM, 0, 0, RENDER_POLICY_SYSTEM, 0, "android: system kept");
    row(RENDER_POLICY_OPENGL_SCREEN, 0, 0, RENDER_POLICY_SYSTEM, 1, "android: openglscreen falls back");
    row(RENDER_POLICY_OPENGL, 0, 0, RENDER_POLICY_SYSTEM, 1, "android: opengl falls back");

    /* GLES backend present but the sprite programs not yet wired: the GPU
     * mode must still fall back -- this is the state the M8 todo is in, and
     * the plan's explicit warning. */
    row(RENDER_POLICY_OPENGL, 1, 0, RENDER_POLICY_SYSTEM, 1, "partial GLES: opengl falls back");
    /* A presenter without sprite programs is enough for OpenGLScreen. */
    row(RENDER_POLICY_OPENGL_SCREEN, 1, 0, RENDER_POLICY_OPENGL_SCREEN, 0, "partial GLES: openglscreen kept");

    /* Sprite programs but no presenter: neither GL mode may be selected. */
    row(RENDER_POLICY_OPENGL, 0, 1, RENDER_POLICY_SYSTEM, 1, "no presenter: opengl falls back");
    row(RENDER_POLICY_OPENGL_SCREEN, 0, 1, RENDER_POLICY_SYSTEM, 1, "no presenter: openglscreen falls back");

    /* Defensive: unknown mode and NULL caps. */
    row(99, 1, 1, RENDER_POLICY_SYSTEM, 1, "unknown mode falls back");
    row(-1, 1, 1, RENDER_POLICY_SYSTEM, 1, "negative mode falls back");

    RenderPolicy_Resolve(RENDER_POLICY_OPENGL, NULL, &d);
    check(d.effective == RENDER_POLICY_SYSTEM && d.fell_back == 1 && d.reason != NULL,
          "NULL caps resolve to System with a reason");

    /* System is never reported as a fallback, even with nothing available. */
    caps.system = 0;
    caps.gl_screen = 0;
    caps.gl_sprite = 0;
    RenderPolicy_Resolve(RENDER_POLICY_SYSTEM, &caps, &d);
    check(d.effective == RENDER_POLICY_SYSTEM && d.fell_back == 0, "system is never a fallback");

    if (failures != 0) {
        printf("render-policy: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-policy: selection table gates pass\n");
    return 0;
}
