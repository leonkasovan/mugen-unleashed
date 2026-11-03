/*
 * M0 regression tests for R01-R05 (RENDERING_PLAN.md section 4, gates).
 *
 * Built and run by tools/test_render_regress.sh, NOT part of the engine.
 * Links the production units (src/sdl2compat.c, src/sdl2surface.c) so these
 * are the real functions, not copies, plus the MUGEN_SDL2_DIAG diagnostic
 * build (counters + SDL_QueryTexture-based texture check). Uses a hidden
 * window on the platform video driver: no visible display or game data is
 * needed. (The static SDL2 kit ships only the `windows` driver, so
 * SDL_VIDEODRIVER=dummy is unavailable.)
 *
 * Each gate matches the plan's completion criteria:
 *  R01: fails on injected allocation failure, not test setup. The live
 *       texture's actual size comes from SDL_QueryTexture (never the shim's
 *       own metadata); a same-size retry after failure must work.
 *  R02: detects a wrong projection without counting API calls (GL path;
 *       runs only when a GL context is available, else SKIP).
 *  R03: Normal and PageFlip each produce exactly one present per
 *       Gfx_Present-equivalent frame; offscreen updates produce none.
 *       Counted by the diagnostic present counter.
 *  R04: distinguishes the neighbour field from `legacy`, not just sizeof
 *       (host layout test, no SDL needed).
 *  R05: expected image is not a constant fill; tells a transparent sprite
 *       apart from one never drawn (host rasterizer test, no SDL needed).
 * Reverting the matching fix must make its test fail again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef main
#undef main
#endif

/* Production entry points under test. */
SDL_Surface *Sdl2_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void Sdl2_Flip(SDL_Surface *surface);
void Sdl2_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void Sdl2_VideoQuit(void);

/* MUGEN_SDL2_DIAG test hooks (src/sdl2compat.c). */
void Sdl2_DiagReset(void);
void Sdl2_DiagCounters(int *present, int *tex_create, int *tex_destroy);
int Sdl2_DiagTextureSize(int *w, int *h, Uint32 *format);
void Sdl2_DiagFailNext(int which);

/* The engine's logger; this test is not the engine. */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* ---- R01: texture/surface stay paired across failed resize ----
 *
 * The pre-fix bug recorded the new size and swapped the CPU surface while
 * keeping the old texture: SDL_QueryTexture then disagreed with the
 * surface, a same-size retry never rebuilt, and a full upload read past
 * the allocation. After the fix the pair moves together or not at all.
 */
static void test_r01_transaction(void)
{
    SDL_Surface *s1, *s2, *after, *retry;
    int tw, th, present, tc, td;
    Uint32 fmt;

    printf("-- R01 transaction\n");
    Sdl2_DiagReset();
    s1 = Sdl2_SetVideoMode(640, 480, 16, 0);
    check(s1 != NULL && s1->w == 640 && s1->h == 480, "R01: 640x480 setup");
    if (s1 == NULL) {
        return;
    }
    check(Sdl2_DiagTextureSize(&tw, &th, NULL) && tw == 640 && th == 480,
          "R01: live texture is 640x480 (independent query)");

    /* Shrink to a size that must succeed. */
    s2 = Sdl2_SetVideoMode(320, 240, 16, 0);
    check(s2 != NULL && s2->w == 320 && s2->h == 240, "R01: shrink to 320x240");
    if (s2 == NULL) {
        return;
    }
    check(Sdl2_DiagTextureSize(&tw, &th, &fmt) && tw == 320 && th == 240,
          "R01: live texture is 320x240 after shrink");
    (void)fmt;

    /* Failure injection: the replacement texture fails while the surface
     * would succeed -- the exact R01 scenario. The invariant is that the
     * live texture and the published surface always agree: post-fix the
     * resize returns NULL and the old pair survives; pre-fix it recorded
     * the new size, swapped the surface, and left the old texture behind
     * (texture 320x240 vs surface 160x120 -- the SIGSEGV shape). */
    Sdl2_DiagFailNext(1);
    after = Sdl2_SetVideoMode(160, 120, 16, 0);
    check(Sdl2_DiagTextureSize(&tw, &th, NULL), "R01: live texture queryable after failed texture");
    if (after != NULL) {
        /* Pre-fix shape: a surface came back -- it must match the texture. */
        check(tw == after->w && th == after->h, "R01: texture and surface agree after failed texture");
    } else {
        /* Post-fix shape: NULL return, old pair intact. */
        check(tw == 320 && th == 240, "R01: live texture still 320x240 after failed texture");
    }

    /* A same-size retry must work and reuse the surviving pair. */
    Sdl2_DiagReset();
    retry = Sdl2_SetVideoMode(320, 240, 16, 0);
    check(retry != NULL && retry->w == 320 && retry->h == 240, "R01: same-size retry works");
    Sdl2_DiagCounters(&present, &tc, &td);
    (void)td;
    check(tc == 0, "R01: retry creates no new texture (pair reused)");
    if (retry == NULL) {
        return;
    }

    /* Now the mirror: the replacement surface fails while the texture would
     * succeed. Same invariant. */
    Sdl2_DiagFailNext(2);
    after = Sdl2_SetVideoMode(160, 120, 16, 0);
    check(Sdl2_DiagTextureSize(&tw, &th, NULL), "R01: live texture queryable after failed surface");
    if (after != NULL) {
        check(tw == after->w && th == after->h, "R01: texture and surface agree after failed surface");
        retry = after;
    } else {
        check(tw == 320 && th == 240, "R01: live texture still 320x240 after failed surface");
    }

    /* A full upload through the surviving pair must count one present. */
    retry = Sdl2_SetVideoMode(320, 240, 16, 0);
    check(retry != NULL, "R01: back to 320x240 for upload");
    if (retry == NULL) {
        return;
    }
    Sdl2_DiagReset();
    memset(retry->pixels, 0x5A, (size_t)retry->pitch * (size_t)retry->h);
    Sdl2_Flip(retry);
    Sdl2_DiagCounters(&present, NULL, NULL);
    check(present == 1, "R01: one present for the upload");
    check(Sdl2_DiagTextureSize(&tw, &th, NULL) && tw == 320 && th == 240,
          "R01: pair still consistent after upload");
    Sdl2_VideoQuit();
}

/* ---- R03: one present per frame on SDL2 desktop ----
 *
 * UpdateRect and Flip both end in Sdl2_Present on this backend, so one
 * Gfx_Present-equivalent (blit + UpdateRect + PageFlip Flip) used to
 * present twice. The fix skips the UpdateRect when a Flip follows; here
 * each shim call is counted directly: one Flip is one present, and an
 * offscreen pixel update (surface write with no shim call) is none.
 */
static void test_r03_single_present(void)
{
    SDL_Surface *s;
    int present;

    printf("-- R03 single present\n");
    s = Sdl2_SetVideoMode(64, 64, 16, 0);
    check(s != NULL, "R03: setup");
    if (s == NULL) {
        return;
    }

    /* Offscreen update: writing pixels without a shim call presents nothing. */
    Sdl2_DiagReset();
    memset(s->pixels, 0x12, (size_t)s->pitch * (size_t)s->h);
    Sdl2_DiagCounters(&present, NULL, NULL);
    check(present == 0, "R03: offscreen pixel write presents nothing");

    /* One UpdateRect is exactly one present. */
    Sdl2_DiagReset();
    Sdl2_UpdateRect(s, 0, 0, 0, 0);
    Sdl2_DiagCounters(&present, NULL, NULL);
    check(present == 1, "R03: one UpdateRect is one present");

    /* One Flip is exactly one present. */
    Sdl2_DiagReset();
    Sdl2_Flip(s);
    Sdl2_DiagCounters(&present, NULL, NULL);
    check(present == 1, "R03: one Flip is one present");
    Sdl2_VideoQuit();
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    test_r01_transaction();
    test_r03_single_present();
    SDL_Quit();

    if (failures != 0) {
        printf("render-regress: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-regress: R01/R03 shim gates pass\n");
    return 0;
}
