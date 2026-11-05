/*
 * CLIP01 gate: Gfx_SetClipRect must reach the GL clip path.
 *
 * The regression this exists for: the title screen rendered as a pure white
 * frame under RenderMode = OpenGL on Android. Gfx_SetClipRect reached
 * Gl_ApplyClipRect only behind `#ifndef ANDROID_PORT`, a guard written when
 * Android had no GL path at all. M8 made Android a GL platform and the guard
 * was not updated, so no GLES draw ever set its scissor rectangle: the title
 * background's window-clipped additive strips drew full-screen and saturated
 * the frame.
 *
 * The call is compiled from the real src/gfx.c, not a copy, so the guard is
 * what is being tested. Gl_ApplyClipRect and the engine globals the function
 * touches are supplied here; everything else in gfx.c is dropped by
 * --gc-sections, which is what makes linking this without an engine possible.
 *
 * Three configurations are compiled by tools/test_clip_gate.sh:
 *   desktop   RENDER_OPENGL must reach Gl_ApplyClipRect
 *   android   (software only) must NOT -- there is no GL renderer to tell
 *   gles      RENDER_OPENGL must reach it, which is the case that broke
 */
#include <stdio.h>
#include <string.h>

#include "mugen.h"

/* --- the engine globals Gfx_SetClipRect reads or writes ------------------- */
int g_targetWidth = 320;
int g_targetHeight = 240;
int g_targetBpp = 16;
int g_targetPixels = 0;
int g_targetBytesPerPixel = 2;
int g_clipX1, g_clipY1, g_clipX2, g_clipY2;
int g_clipW, g_clipH, g_clipRowSkip;
SpriteSet *g_renderTarget = NULL;
int g_renderTargetSprite = 0;

/* Renderer selection: RENDER_MODE_GL is RENDER_OPENGL (2), see game.h. */
int g_renderMode = RENDER_SYSTEM;

/* --- the GL entry point gfx.c calls --------------------------------------- */
static int g_apply_calls;

void Gl_ApplyClipRect(void)
{
    g_apply_calls++;
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/*
 * Whether this build is supposed to reach the GL clip path. Set by the
 * compiler command line so one source serves all three configurations.
 */
#if defined(CLIP_GATE_EXPECT_GL)
#define EXPECT_GL 1
#else
#define EXPECT_GL 0
#endif

int main(void)
{
    printf("clip-gate: renderer=%s expectation=%s\n",
#if EXPECT_GL
           "compiled with the GL path", "Gl_ApplyClipRect must be reached"
#else
           "compiled without the GL path", "Gl_ApplyClipRect must NOT be reached"
#endif
    );

    /* 1. A clip rect that changes must notify the GL renderer (if present). */
    g_apply_calls = 0;
    g_renderMode = RENDER_OPENGL;
    Gfx_SetClipRect(0, 100, 320, 200);

    check(g_clipX1 == 0 && g_clipY1 == 100 && g_clipX2 == 320 && g_clipY2 == 200,
          "the clip rect is stored");
    check(g_clipW == 320 && g_clipH == 100, "the clip size is derived");

#if EXPECT_GL
    check(g_apply_calls == 1, "an OpenGL clip rect reaches Gl_ApplyClipRect");
#else
    check(g_apply_calls == 0, "a build without the GL path does not call Gl_ApplyClipRect");
#endif

    /*
     * 2. The System renderer must never call it, whatever the build: with no
     *    GL context that would be a call into nothing.
     */
    g_apply_calls = 0;
    g_renderMode = RENDER_SYSTEM;
    Gfx_SetClipRect(0, 0, 320, 240);
    check(g_apply_calls == 0, "the System renderer does not call Gl_ApplyClipRect");

    /*
     * 3. Clamping still happens and still notifies: the title screen relies on
     *    a zero-height window (window = 0,332, 1279,334 collapses to two rows),
     *    and an out-of-range rect must not become a negative scissor.
     */
    g_apply_calls = 0;
    g_renderMode = RENDER_OPENGL;
    Gfx_SetClipRect(-50, -50, 10000, 10000);
    check(g_clipX1 == 0 && g_clipY1 == 0, "negative coordinates are clamped");
    check(g_clipX2 == g_targetWidth && g_clipY2 == g_targetHeight, "oversized coordinates are clamped");
#if EXPECT_GL
    check(g_apply_calls == 1, "a clamped rect still reaches Gl_ApplyClipRect");
#endif

    /* 4. Inverted corners are normalised before use. */
    g_apply_calls = 0;
    Gfx_SetClipRect(300, 200, 10, 20);
    check(g_clipX1 == 10 && g_clipY1 == 20 && g_clipX2 == 300 && g_clipY2 == 200,
          "inverted corners are normalised");

    if (failures != 0) {
        printf("clip-gate: %d FAILURES\n", failures);
        return 1;
    }
    printf("clip-gate: clip rect plumbing is correct for this backend\n");
    return 0;
}
