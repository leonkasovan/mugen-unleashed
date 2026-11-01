// =============================================================================
// Stubs for the OpenGL renderer, for the Android port.
//
// glrender.c is not compiled here: this port uses the engine's software
// (System) renderer, because the Android build has no desktop GL loader and
// glrender.c is written against desktop GL 1.x/2.1 with glad.
//
// The problem this file solves is that the GL renderer's *symbols* are still
// referenced from modules that ARE compiled: gfx.c picks a renderer through the
// g_gfx* function-pointer tables, sprite.c and palette.c handle GlTex objects,
// and video.c's teardown mentions Gl_Deinit. Excluding glrender.c therefore left
// eighteen undefined symbols at link time.
//
// Two honest choices exist: pull glrender.c in (which cannot compile without
// GL), or provide definitions that are never reached. This is the second. Every
// function here returns a failure or does nothing, and each is only reachable
// from a RENDER_OPENGL branch -- config.c forces RENDER_MODE_SYSTEM on Android,
// so none of them can be called. If one ever is, the failure is visible (a NULL
// texture, a 0 return) rather than silent corruption.
//
// This is a platform back end, not a change to the reconstruction: nothing in
// src/ is altered to make it work.
// =============================================================================
#ifdef ANDROID_PORT

#include <string.h>

#include "mugen.h"

/*
 * The blend-equation extension flag. symbols.h maps g_glewExtBlendLogicOp onto
 * this name for non-glad builds; glrender.c would define it from a GL query.
 */
int MugenVita_GLExtBlendLogicOp = 0;

/* --- GlTex: the GL texture objects -------------------------------------- */

GlTex *GlTex_Create(void)
{
    /* No GL context exists, so no texture can be created. */
    return NULL;
}

int GlTex_Free(GlTex *tex)
{
    (void)tex;
    return 0;
}

int GlTex_Init(GlTex *tex)
{
    if (tex != NULL) {
        memset(tex, 0, sizeof(*tex));
    }
    return 0;
}

int GlTex_Delete(GlTex *tex)
{
    (void)tex;
    return 0;
}

int GlTex_Alloc(GlTex *tex, int w, int h)
{
    (void)tex; (void)w; (void)h;
    return 0;
}

int GlTex_Upload2D(int h, int bpp, GlTex *tex, const void *pixels, int w)
{
    (void)h; (void)bpp; (void)tex; (void)pixels; (void)w;
    return 0;
}

int GlTex_Upload1D(GlTex *tex, int width, const void *pixels)
{
    (void)tex; (void)width; (void)pixels;
    return 0;
}

/* --- vertex buffers ------------------------------------------------------ */

GlTex *GlVbuf_Alloc(int depth, int w, int h)
{
    (void)depth; (void)w; (void)h;
    return NULL;
}

int GlVbuf_Bind(GlTex *vbuf)
{
    (void)vbuf;
    return 0;
}

/* --- view setup and presentation ---------------------------------------- */

int Gl_SetupView(void)
{
    return 0;
}

void Gl_ResetModelview(void)
{
}

void Gl_ApplyClipRect(void)
{
}

void *Gl_Capture(void)
{
    return NULL;
}

void Gl_Present(SpriteSet *set, int sprite, float x, float y, float w, float h)
{
    (void)set; (void)sprite; (void)x; (void)y; (void)w; (void)h;
}

int GlScreen_Upload(int h, int w, const void *pixels)
{
    (void)h; (void)w; (void)pixels;
    return 0;
}

void GlScreen_Draw(int w, int h)
{
    (void)w; (void)h;
}

void Sprite_DrawGL(struct SpriteSet *set, int sprite, const DrawParams *dp)
{
    (void)set; (void)sprite; (void)dp;
}

#endif /* ANDROID_PORT */
