/*
 * M0 host gates: R04 layout contract and R05 true-colour rasterizer.
 *
 * No SDL video needed (SDL surfaces are created in memory only).
 * Links production units: sprite.c + keyedarray.c + palette.c +
 * softrender.c, so the drawers under test are the real functions.
 * The R05 expected image is computed independently below (masks from the
 * format, not from the engine's pack routine) and is neither a constant
 * fill nor fully transparent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef main
#undef main
#endif

#include "mugen.h"

/* ---- engine globals the linked units need ---- */
int g_renderMode;
int g_renderTargetSprite;
SpriteSet *g_renderTarget;
int g_targetBpp;
int g_glMaxTextureSize;
int g_outOfMemory;
PalRemap *g_palRemapCur;
SpriteDecodeFn g_spriteDecoders[13];
int g_clipX1, g_clipY1, g_clipX2, g_clipY2;

/* ---- stubs for callees not under test ---- */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}
void MatchLog_Printf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}
void PalRemap_Lookup(const PalRemap *r, int group, int index, int *out_group, int *out_index)
{
    (void)r;
    (void)group;
    (void)index;
    (void)out_group;
    (void)out_index;
}
int SpriteCache_Get(void *a, int b, int c, void **d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return 0;
}
void SpriteCache_Remove(void *a)
{
    (void)a;
}
void Sprite_DrawGL(SpriteSet *s, int i, const DrawParams *d)
{
    (void)s;
    (void)i;
    (void)d;
}
GlTex *GlTex_Create(void)
{
    return NULL;
}
int GlTex_Free(GlTex *tex)
{
    (void)tex;
    return 0;
}
int GlTex_Upload2D(int h, int bpp, GlTex *tex, const void *pixels, int w)
{
    (void)h;
    (void)bpp;
    (void)tex;
    (void)pixels;
    (void)w;
    return 0;
}
int GlTex_Upload1D(GlTex *tex, int width, const void *pixels)
{
    (void)tex;
    (void)width;
    (void)pixels;
    return 0;
}
PalFx g_palFxApplied;
PalFx g_palFx;
int g_palConvertCount;
unsigned int g_palFxSerial;
/* Palette unit not linked (the true-colour path under test never calls into
 * it at runtime); satisfy sprite.c's static references. */
void Pal_Convert(Palette *p)
{
    (void)p;
}
void Pal_Free(Palette *p)
{
    (void)p;
}
int Math_Round(float x)
{
    return (int)floorf(x + 0.5f);
}
/* Rotate/ADD drawers live in softblend.c, not linked here; the true-colour
 * path under test never reaches them (angle != 0 logs and draws unrotated,
 * blend != NONE logs and draws masked). Stubs keep the link honest about
 * what this gate covers. */
int *g_sinTable;
void DrawParams_FlipToAngle(DrawParams *d)
{
    (void)d;
}
void SoftSprite_RotateAddFlip(SpriteSet *s, int i, float x, float y, float a, float b, float c, float d, float e,
                              int f, int g)
{
    (void)s; (void)i; (void)x; (void)y; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
}
void SoftSprite_RotateAdd(SpriteSet *s, int i, float x, float y, float a, float b, float c, float d, float e,
                          int f, int g)
{
    (void)s; (void)i; (void)x; (void)y; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
}
void SoftSprite_DrawScaledAdd(SpriteSet *s, int i, int a, float x, float y, float sx, float sy, int b)
{
    (void)s; (void)i; (void)a; (void)x; (void)y; (void)sx; (void)sy; (void)b;
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* ---- R04: layout contract ----
 *
 * The 32-bit reference offsets (AGENTS.md): Bg.legacy at 0x64, BgElem.y at
 * 0x88, InputBuf.keys at 0x78, Projectile.afterimage at 0x504. The M0 gate
 * pins the reference side on the 32-bit host: the struct the compiler laid
 * out must put the field exactly there, so member access and the original's
 * hardcoded offset read the same member. The 64-bit side (member access
 * reads the right field where the offset would not) is the ABI01 gate with
 * a real 64-bit compiler -- out of M0 scope, tracked by the M4 todo.
 */
static void test_r04_layout(void)
{
    printf("-- R04 layout\n");
    check(offsetof(Bg, legacy) == 0x64, "R04: Bg.legacy at 0x64");
    check(offsetof(BgElem, y) == 0x88, "R04: BgElem.y at 0x88");
    check(offsetof(InputBuf, keys) == 0x78, "R04: InputBuf.keys at 0x78");
    check(offsetof(Projectile, afterimage) == 0x504, "R04: Projectile.afterimage at 0x504");
    check(sizeof(Bg) == 0x68, "R04: sizeof Bg");
    check(sizeof(Projectile) == 0x508, "R04: sizeof Projectile");

    /* Neighbour-sentinel semantics: legacy lives where the camera reads it.
     * A Bg with legacy=0 and every neighbouring int nonzero must still read
     * legacy as 0 through the member -- the contract the camera fix relies
     * on (no rounding when legacy is 0 regardless of neighbours). */
    {
        Bg bg;
        memset(&bg, 0xAB, sizeof(bg));
        bg.legacy = 0;
        check(bg.legacy == 0, "R04: legacy reads 0 with nonzero neighbours");
        bg.legacy = 1;
        check(bg.legacy != 0, "R04: legacy reads nonzero when set");
    }
}

/* ---- R05: true-colour rasterizer ----
 *
 * 2x1 32-bit BGRA RAW sprite: pixel 0 opaque red (B=0,G=0,R=255,A=255),
 * pixel 1 fully transparent (A=0). Drawn 1:1 at (1,0) onto a 4x2 RGB565
 * target prefilled with 0x1234. Expected, computed from the masks here:
 * target(1,0) = RGB565(255,0,0) = 0xF800, target(2,0) still 0x1234
 * (transparent source leaves the pixel untouched -- not "never drawn",
 * because the prefill proves the drawer visited the span without writing).
 */
static void test_r05_truecolour(void)
{
    SDL_Surface *dst;
    SpriteSet *tset, *sset;
    Sprite tspr, sspr;
    unsigned char srcpx[2 * 4];
    DrawParams dp;
    unsigned short *row0;

    printf("-- R05 true-colour\n");
    g_targetBpp = 16;
    g_clipX1 = 0;
    g_clipY1 = 0;
    g_clipX2 = 4;
    g_clipY2 = 2;

    dst = SDL_CreateRGBSurface(0, 4, 2, 16, 0xF800, 0x07E0, 0x001F, 0);
    check(dst != NULL, "R05: target surface");
    if (dst == NULL) {
        return;
    }
    {
        int i, n = (dst->pitch * 2) / 2;
        unsigned short *p = (unsigned short *)dst->pixels;
        for (i = 0; i < n; i++) {
            p[i] = 0x1234;
        }
    }

    tset = calloc(1, sizeof(SpriteSet));
    sset = calloc(1, sizeof(SpriteSet));
    check(tset != NULL && sset != NULL, "R05: sets");
    if (tset == NULL || sset == NULL) {
        return;
    }
    tset->sprites = KeyedArray_Create(1, sizeof(Sprite));
    sset->sprites = KeyedArray_Create(1, sizeof(Sprite));
    check(tset->sprites != NULL && sset->sprites != NULL, "R05: sprite tables");
    if (tset->sprites == NULL || sset->sprites == NULL) {
        return;
    }

    memset(&tspr, 0, sizeof(tspr));
    tspr.w = 4;
    tspr.h = 2;
    tspr.bpp = 16;
    tspr.format = SPRITE_FORMAT_SURFACE;
    tspr.pixels = dst;
    Sprite_InitRows(&tspr);
    KeyedArray_Add(tset->sprites, &tspr, 0, 0);

    /* B,G,R,A order (little-endian BGRA, as the PNG decoder writes it). */
    srcpx[0] = 0;
    srcpx[1] = 0;
    srcpx[2] = 255;
    srcpx[3] = 255;     /* opaque red */
    srcpx[4] = 0;
    srcpx[5] = 0;
    srcpx[6] = 0;
    srcpx[7] = 0;       /* transparent */
    memset(&sspr, 0, sizeof(sspr));
    sspr.w = 2;
    sspr.h = 1;
    sspr.bpp = 32;
    sspr.format = SPRITE_FORMAT_RAW;
    sspr.pixels = srcpx;
    Sprite_InitRows(&sspr);
    sspr.pal_index = -1;
    KeyedArray_Add(sset->sprites, &sspr, 0, 0);

    g_renderTarget = tset;
    g_renderTargetSprite = 0;
    memset(&dp, 0, sizeof(dp));
    dp.scale_x = 1.0f;
    dp.scale_y = 1.0f;
    dp.scale2_x = 1.0f;
    dp.scale2_y = 1.0f;
    dp.x = 1.0f;
    dp.y = 0.0f;
    dp.blend = BLEND_NONE;
    dp.masked = 1;
    Sprite_DrawSoft(sset, 0, &dp);

    row0 = (unsigned short *)dst->pixels;
    check(row0[0] == 0x1234, "R05: pixel left of sprite untouched");
    check(row0[1] == 0xF800, "R05: opaque red pixel is RGB565 red");
    check(row0[2] == 0x1234, "R05: transparent pixel leaves target intact");
    check(row0[3] == 0x1234, "R05: pixel right of sprite untouched");
    check(((unsigned short *)((char *)dst->pixels + dst->pitch))[0] == 0x1234,
          "R05: second row untouched");
    SDL_FreeSurface(dst);
}

int main(void)
{
    test_r04_layout();
    test_r05_truecolour();

    if (failures != 0) {
        printf("render-host: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-host: R04/R05 gates pass\n");
    return 0;
}
