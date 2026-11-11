/* Production Video_SetMode with a refused platform display. Do not reach the
 * GL loader/probe when there is no context; fallback must remain possible.
 * Engine type declarations use vendored SDL1 headers; no SDL library needed. */
#include "mugen.h"
#ifdef main
#undef main
#endif
#include <assert.h>
#define MUGEN_GLES 1
#include "../src/video.c"

static int refused = 1, mode_calls, gl_calls, target_calls;
static SDL_Surface display;
void Log_Printf(const char *fmt, ...) {}
void MatchLog_Printf(int level,const char *fmt, ...) {}
SDL_Surface *Sdl2_SetVideoMode(int w,int h,int bpp,Uint32 flags)
{ mode_calls++;return refused ? NULL : &display; }
int Sdl2_GL_SetAttribute(SDL_GLattr attr,int value) { return 0; }
int SDL_putenv(const char *value) { return 0; }
int SDL_ShowCursor(int value) { return 0; }
void SDL_FreeSurface(SDL_Surface *s) {}
int Gl_Init(int fbo) { gl_calls++;return 1; }
int Gfx_SetRenderTarget(SpriteSet *s,int i) { target_calls++;return 1; }

int main(void)
{
#ifdef TEST_EXPECT_BITS
    assert(sizeof(void *)*8 == TEST_EXPECT_BITS);
#endif
    printf("abi: %u-bit build\n",(unsigned)(sizeof(void *)*8));
    g_renderMode=RENDER_OPENGL;
    assert(!Video_SetMode(640,480,640,480,32,VIDEO_HWBLIT));
    assert(mode_calls == 1 && gl_calls == 0 && target_calls == 0);
    assert(g_libraryError == LIBERR_VIDEO_MODE);
    assert(g_videoSurface == NULL && g_screenSurface == NULL);
    /* The caller can switch to software after the rejected ES context. */
    g_renderMode=RENDER_SYSTEM;refused=0;
    assert(Video_SetMode(640,480,640,480,16,0));
    assert(mode_calls == 2 && gl_calls == 0 && target_calls == 1);
    g_renderMode=RENDER_OPENGL;
    assert(Video_SetMode(640,480,640,480,32,VIDEO_HWBLIT));
    assert(gl_calls == 1 && target_calls == 2);
    puts("ok: refused GLES mode never enters GL; software retry and successful GLES initialization work");
    return 0;
}

int Sdl2_SetSwapInterval(int value) { (void)value; return 0; }
