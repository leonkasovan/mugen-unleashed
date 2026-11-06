/* Optional real-driver smoke test. Native SDL2/GLES declarations; EGL pbuffer
 * instead of an Android window. This is host Mesa evidence, not device parity. */
#include "mugen.h"
#ifdef main
#undef main
#endif
#include <EGL/egl.h>
#include <assert.h>
#include <stdarg.h>
#include "../src/gles_render.c"
#include "../src/gles_draw.c"
#include "../src/gles_probe.c"
#include "../src/gfx.c"

void Log_Printf(const char *fmt, ...)
{ va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap); }
void MatchLog_Printf(int level,const char *fmt, ...)
{ va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap); }
SDL_bool SDL_SetClipRect(SDL_Surface *surface,const SDL_Rect *rect) { return SDL_TRUE; }
void Android_GLDrawableSize(int *w,int *h) { *w=640;*h=480; }
static int overlay_calls;
static void overlay(SpriteSet *set,int sprite)
{
    assert(set == NULL && g_renderTarget == NULL);
    overlay_calls++;
    Gfx_SetClipRect(0,0,640,480);
    Gfx_GlFillRect(10,10,20,20,0x000000FF); /* Engine GL colour: low byte red. */
}

int main(void)
{
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint count, major, minor;
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint size[]={EGL_WIDTH,640,EGL_HEIGHT,480,EGL_NONE};
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    GlTex *tex;
    Sprite spr={0};KeyedArray sprites={0};SpriteSet set={0};
    unsigned char *pixels;
    int pass;

    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display,&major,&minor)) { puts("SKIP: EGL unavailable");return 77; }
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    if (!eglChooseConfig(display,attrs,&config,1,&count) || count == 0) {
        eglTerminate(display);puts("SKIP: no ES3 EGL pbuffer config");return 77;
    }
    surface=eglCreatePbufferSurface(display,config,size);
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT) {
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display,surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display,context);
        eglTerminate(display);puts("SKIP: could not create ES3 pbuffer/context");return 77;
    }
    assert(eglMakeCurrent(display,surface,surface,context));
    assert(gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress));
    printf("driver: %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    printf("abi: %u-bit build\n",(unsigned)(sizeof(void *)*8));
    g_videoWidth=g_targetWidth=640;g_videoHeight=g_targetHeight=480;
    g_videoDepth=g_targetBpp=32;g_renderMode=RENDER_MODE_GL;
    for (pass=0;pass<2;pass++) {
        assert(Gl_Init(1));
        tex=GlVbuf_Alloc(32,640,480);assert(tex);
        spr.w=640;spr.h=480;spr.bpp=32;spr.format=SPRITE_FORMAT_TEXTURE;spr.pixels=tex;
        sprites.data=(void *)&spr;sprites.capacity=1;sprites.elem_size=sizeof(spr);set.sprites=&sprites;
        assert(Gfx_SetRenderTarget(&set,0));
        glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        g_gfxPrePresent=overlay;
        Gl_Present(&set,0,0,0,640,480);
        assert(g_renderTarget == &set && g_glBoundFramebuffer != 0);
        assert(Gfx_SetRenderTarget(NULL,0));
        pixels=Gl_Capture();assert(pixels);
        /* Top-down capture contains both the blue screen quad and red overlay. */
        assert(pixels[(15*640+15)*4] == 255 && pixels[(15*640+15)*4+2] == 0);
        assert(pixels[(30*640+30)*4] == 0 && pixels[(30*640+30)*4+2] == 255);
        free(pixels);
        assert(glGetError() == GL_NO_ERROR);
        assert(GlTex_Free(tex));
        Gl_Deinit();
        assert(eglDestroyContext(display,context));
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);assert(context != EGL_NO_CONTEXT);
        assert(eglMakeCurrent(display,surface,surface,context));
    }
    assert(overlay_calls == 2);
    free(g_glesTextures);free(g_glesBacking);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
    puts("ok: real GLES shaders, FBO, present overlay pixels, capture and context recreation");
    return 0;
}
