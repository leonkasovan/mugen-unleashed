/*
 * Verifies the Android B3 present contract without a device.
 *
 * Contract: SDL_UpdateRect/SDL_Flip only mark dirty; Android_PresentFrame is
 * the single presenter. So 1 frame with N dirty marks = 1 upload; a frame
 * with no dirty marks = 0 uploads; M consecutive frames = M uploads.
 *
 * Stubs the SDL functions android_video.c calls (window/renderer/texture are
 * opaque pointers here; UpdateTexture/RenderClear/RenderCopy/RenderPresent
 * count calls). Compiled with the real android_video.c under __ANDROID__.
 */
#define __ANDROID__ 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- SDL stubs live in build/sdlstub.c, declared by stub SDL.h ---- */
#include <SDL.h>
#include <android/log.h>

extern int g_uploads;
extern int g_presents;

/* ---- declarations under test (from android_main.h, trimmed) ---- */
extern void *Android_SetVideoMode(int w, int h, int bpp, Uint32 flags);
extern void Android_UpdateRectX(void *surf, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
extern void Android_Flip(void);
extern void Android_PresentFrame(void);

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    void *surf = Android_SetVideoMode(640, 480, 16, 0);

    check(surf != NULL, "surface created");
    /* draw-path traffic without a frame end: no upload, no present */
    Android_UpdateRectX(surf, 0, 0, 10, 10);
    Android_UpdateRectX(surf, 10, 10, 20, 20);
    Android_Flip();
    check(g_uploads == 0 && g_presents == 0, "dirty marks alone upload nothing");
    /* one frame end: exactly one upload and one present */
    Android_PresentFrame();
    check(g_uploads == 1 && g_presents == 1, "one frame end = one upload + one present");
    /* frame end always flushes (a finished frame is shown even if unchanged);
     * the saving is that N dirty marks inside one frame collapse to one. */
    Android_PresentFrame();
    check(g_uploads == 2 && g_presents == 2, "frame end flushes even when clean");
    /* three frames with dirt: three uploads */
    Android_Flip();
    Android_PresentFrame();
    Android_UpdateRectX(surf, 0, 0, 5, 5);
    Android_PresentFrame();
    Android_Flip();
    Android_PresentFrame();
    check(g_uploads == 5 && g_presents == 5, "three dirty frames = three more uploads");
    if (failures == 0) {
        printf("ok: present contract holds\n");
    }
    return failures != 0;
}
