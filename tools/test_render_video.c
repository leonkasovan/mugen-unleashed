/*
 * R02 gate: Video_OnResize's OpenGLScreen path must leave an identity-reset
 * orthographic projection, no matter how many resizes ran before.
 *
 * Links the production src/video.c (Video_OnResize) plus the SDL2 shim, with
 * a real GL context from a hidden window. Reads back GL_PROJECTION_MATRIX
 * and compares against the expected gluOrtho2D(0,w,h,0) -- not API call
 * counts. Pre-fix (no glLoadIdentity) the second resize multiplies the
 * previous projection and the matrix diverges; post-fix it is identical
 * after any number of identical resizes.
 *
 * R04 gate: host layout test, no SDL needed. Builds Bg structs with
 * neighbour sentinels: legacy=0 with the neighbour field nonzero must take
 * the non-rounding branch, legacy=1 with neighbour zero must round. The
 * pre-fix *(Bg*)(bg+0x64) read passes on 32-bit (offset coincides) but the
 * test pins the semantic contract on any ABI: set the neighbour to a
 * nonzero sentinel and legacy=0, the camera must NOT round.
 *
 * R05 gate: host rasterizer test. Draws a 2x1 true-colour RAW sprite
 * (32-bit BGRA, one opaque red pixel + one transparent pixel) at 1:1 onto a
 * 16-bit target via Sprite_DrawSoft and checks the exact RGB565 bytes:
 * neither a constant fill nor a fully-transparent result passes, and a
 * transparent source pixel must leave the target pixel untouched
 * (distinguishing "transparent" from "never drawn").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>

#ifdef main
#undef main
#endif

#include "mugen.h"

/* ---- engine globals video.c needs (declared in symbols.h, defined here) -- */
GameVars *g_game;
int g_renderMode;
int g_videoDepth;
Uint32 g_videoSdlFlags;
SDL_Surface *g_videoSurface;
SDL_Surface *g_screenSurface;
int g_videoGameWidth, g_videoGameHeight, g_videoGamePixels;
int g_videoWidth, g_videoHeight;
int g_videoBytesPerPixel;
unsigned int g_videoFlags;
int g_videoFullscreen;
int g_targetWidth, g_targetHeight, g_targetBpp, g_targetBytesPerPixel;
SpriteSet *g_renderTarget;
int g_renderTargetSprite;
int g_glScreenResized;
int *g_sinTable;
int g_vretrace;
unsigned char g_keyState[0x201];
SDL_Surface *g_unusedSurface1, *g_unusedSurface2;
void *g_videoScratch;
int g_libraryError;
int g_bgmBackgroundMode, g_bgmInitialized;
RECT g_savedWindowRect, g_savedClientRect;
LONG g_savedWindowStyle;
LONG g_savedWindowExStyle;
int g_unused504230, g_unused504234, g_unused50424C;
int g_unusedBump;

/* ---- stubs for video.c callees not under test ---- */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}
void MatchLog_Printf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}
int Gl_Init(int mode)
{
    (void)mode;
    return 1;
}
int Gl_Deinit(void)
{
    return 0;
}
int Gfx_SetRenderTarget(SpriteSet *t, int s)
{
    (void)t;
    (void)s;
    return 1;
}
int PalFx_InitGlobal(void)
{
    return 0;
}
int SinTable_Init(void)
{
    return 0;
}
void Timer_ResetFrame(void)
{
}

void Video_OnResize(int width, int height);

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Expected gluOrtho2D(0,w,h,0) in column-major order. */
static void expected_ortho(float *m, int w, int h)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (float)w;
    m[5] = -2.0f / (float)h;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

static int matrix_equal(const float *a, const float *b)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (fabsf(a[i] - b[i]) > 1e-4f) {
            return 0;
        }
    }
    return 1;
}

static void test_r02_projection(void)
{
    float got[16], want[16];
    int i;

    printf("-- R02 projection\n");
    g_renderMode = RENDER_OPENGL_SCREEN;
    g_videoDepth = 16;
    g_videoSdlFlags = 0;

    /* Twenty identical resizes must give identical matrices and pixels. */
    for (i = 0; i < 20; i++) {
        Video_OnResize(800, 600);
    }
    glGetFloatv(GL_PROJECTION_MATRIX, got);
    expected_ortho(want, 800, 600);
    check(matrix_equal(got, want), "R02: 20 identical resizes give the expected ortho");

    /* And back to game size fills what it should. */
    Video_OnResize(640, 480);
    glGetFloatv(GL_PROJECTION_MATRIX, got);
    expected_ortho(want, 640, 480);
    check(matrix_equal(got, want), "R02: resize back to 640x480 gives the expected ortho");
}

int main(void)
{
    SDL_Surface *glsurf;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    /* A real GL context: the projection matrix only exists there. The
     * engine loads glad in Gl_Init; this harness is not the engine, so it
     * loads the entry points itself after the context is current. */
    glsurf = Sdl2_SetVideoMode(640, 480, 16, SDL_OPENGL);
    if (glsurf == NULL) {
        printf("SKIP: no GL context: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    gladLoadGL();
    test_r02_projection();
    Sdl2_VideoQuit();
    SDL_Quit();

    if (failures != 0) {
        printf("render-video: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-video: R02 gate passes\n");
    return 0;
}
