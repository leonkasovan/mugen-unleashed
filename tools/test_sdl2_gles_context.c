/* Actual SDL2 window/context boundary: reject the first loader call, then retry
 * on the same window. A failed loader must not leave a reusable GL context. */
#include "mugen.h"
#ifdef main
#undef main
#endif
#include <assert.h>
#include <stdarg.h>
static int Test_LoadGles(GLADloadproc load);
#define gladLoadGLES2Loader Test_LoadGles
#include "../src/sdl2video.c"
#undef gladLoadGLES2Loader

static int loads;
void Log_Printf(const char *fmt, ...) { va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap); }
static int Test_LoadGles(GLADloadproc load)
{
    loads++;
    return loads == 1 ? 0 : gladLoadGLES2Loader(load);
}

int main(void)
{
    SDL_Surface *surface;
    if (SDL_Init(0) != 0) { puts("SKIP: SDL video unavailable");return 77; }
    assert(Sdl2_GL_SetAttribute(SDL_GL_RED_SIZE,8) == 0);
    assert(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);
    SDL_QuitSubSystem(SDL_INIT_VIDEO); /* Exercise lazy mode setup separately. */
    surface=Sdl2_SetVideoMode(640,480,32,DISPLAY_OPENGL);
    if (loads == 0) { Sdl2_VideoQuit();SDL_Quit();puts("SKIP: ES3 context unavailable");return 77; }
    assert(surface == NULL && loads == 1);
    assert(g_sdl2Video.gl == NULL && g_sdl2Video.window != NULL);
    surface=Sdl2_SetVideoMode(640,480,32,DISPLAY_OPENGL);
    assert(surface != NULL && loads == 2 && g_sdl2Video.gl != NULL);
    assert(GLVersion.major >= 3 && glGetString(GL_VERSION) != NULL);
    assert(strncmp((const char *)glGetString(GL_VERSION), "OpenGL ES ", 10) == 0);
    Sdl2_VideoQuit();SDL_Quit();
    puts("ok: rejected SDL2 GLES loader drops the context; retry reloads and succeeds");
    return 0;
}
