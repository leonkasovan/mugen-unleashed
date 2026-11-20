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

/* Fixture supplies the exact same CPU bytes to standalone and atlas textures. */
static int sprite_reads;

void *Sprite_GetPixels(SpriteSet *set, int sprite)
{
    Sprite *sprites = (Sprite *)set->sprites->data;

    sprite_reads++;
    return sprites[sprite].pixels;
}

static PFNGLDRAWARRAYSINSTANCEDPROC draw_instanced;
static int instances_drawn, batches_drawn;

static void APIENTRY count_instances(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    instances_drawn += instances;
    batches_drawn++;
    draw_instanced(mode, first, count, instances);
}

static void atlas_fixture(SpriteSet *set, GlTex *palette, GlTex *standalone[2],
                          GlTex *page, float *regions, int atlas)
{
    enum { QUADS = 3 };
    const float uv[] = {0,0,0,1, 1,0,0,1, 0,1,0,1, 1,1,0,1};
    const Sprite *sprites = (const Sprite *)set->sprites->data;
    int i;

    glClearColor(0.1f, 0.2f, 0.3f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < QUADS; i++) {
        int sprite = i % 2;
        float w = (float)sprites[sprite].w, h = (float)sprites[sprite].h;
        float pos[] = {0,0, w,0, 0,h, w,h};
        GlMvp transform = g_glesProjection;
        GlTex *tex = atlas ? page : standalone[sprite];

        /* Fractional scaling and overlap make wrong samples and order visible. */
        transform.m[0] *= 2.13f;
        transform.m[5] *= 2.37f;
        transform.m[12] += (100.4f + (float)i * 7.3f) / 320.0f;
        transform.m[13] += (100.2f + (float)i * 3.1f) / 240.0f;
        gles_batch_record(atlas ? &g_glesProgramAtlas : &g_glesProgramIndexed,
                          1, GL_SRC_ALPHA, GL_CONSTANT_ALPHA, GL_FUNC_ADD, 1, tex, palette,
                          1, transform.m, pos, uv, 0.02f, 0, 0, 0.9f, 1, 1, 0, 0.8f,
                          (float)tex->w, (float)tex->h, 0.6f, i == 2 ? 0.25f : 0.5f,
                          atlas ? regions + sprite * 4 : NULL);
    }
}

static void check_atlas(void)
{
    unsigned char image_a[7 * 5], image_b[9 * 7], colors[256 * 4];
    Sprite sprites[] = {
        { .w = 7, .h = 5, .bpp = 8, .pixels = image_a, .pixels_size = sizeof(image_a) },
        { .w = 9, .h = 7, .bpp = 8, .pixels = image_b, .pixels_size = sizeof(image_b) },
        { .w = 7, .h = 5, .bpp = 8, .pixels = image_a, .pixels_size = sizeof(image_a),
          .x = 11, .pal_index = 3 },
    };
    KeyedArraySlot slots[] = { { .used = 1 }, { .used = 1 }, { .used = 1 } };
    KeyedArray list = { .count = 3, .capacity = 3, .elem_size = sizeof(Sprite),
                        .data = (void *)sprites, .slots = slots };
    SpriteSet set = { .sprites = &list };
    GlTex *page, *standalone[2], *palette;
    float rects[8], linked[4];
    unsigned char *expected, *actual;
    unsigned int i;
    int reads = sprite_reads;

    assert(g_glesProgramAtlas.program != 0);
    for (i = 0; i < sizeof(image_a); i++) image_a[i] = (unsigned char)(i % 5 ? i + 32 : 0);
    for (i = 0; i < sizeof(image_b); i++) image_b[i] = (unsigned char)(i % 7 ? i + 64 : 0);
    for (i = 0; i < 256; i++) {
        colors[4 * i] = (unsigned char)i;
        colors[4 * i + 1] = (unsigned char)(255 - i);
        colors[4 * i + 2] = (unsigned char)(i * 3);
        colors[4 * i + 3] = i == 0 ? 0 : 255;
    }

    Gles_AtlasPlan(&set);
    assert(g_atlasResidentPages == 0 && sprite_reads == reads);
    page = gles_atlas_texture(&set, 0, &sprites[0], rects);
    assert(page != NULL && page->id != 0);
    assert(gles_atlas_texture(&set, 1, &sprites[1], rects + 4) == page);
    assert(gles_atlas_texture(&set, 2, &sprites[2], linked) == page);
    assert(sprite_reads == reads + 2 && g_atlasResidentPages == 1);
    assert(memcmp(rects, linked, sizeof(linked)) == 0);

    for (i = 0; i < 2; i++) {
        standalone[i] = GlTex_Create();
        assert(GlTex_Upload2D(sprites[i].h, 8, standalone[i], sprites[i].pixels, sprites[i].w));
    }
    palette = GlTex_Create();
    assert(GlTex_UploadRGBA(1, 256, palette, colors));

    assert(Gfx_SetRenderTarget(NULL, 0));
    assert(Gl_SetupView());
    atlas_fixture(&set, palette, standalone, page, rects, 0);
    expected = Gl_Capture();
    assert(expected != NULL);

    instances_drawn = batches_drawn = 0;
    draw_instanced = glad_glDrawArraysInstanced;
    glad_glDrawArraysInstanced = count_instances;
    atlas_fixture(&set, palette, standalone, page, rects, 1);
    actual = Gl_Capture();
    glad_glDrawArraysInstanced = draw_instanced;
    assert(actual != NULL && memcmp(actual, expected, 640 * 480 * 4) == 0);
    assert(instances_drawn == 3 && batches_drawn == 2);

    /* A loaded archive survives loss of every GL object. Its page is
     * populated again from the source, once per linked image. */
    assert(Gl_Deinit());
    assert(g_atlasResidentPages == 0 && page->id == 0);
    assert(Gles_OnContextRestore(640, 480));
    assert(Gl_SetupView());
    assert(gles_atlas_texture(&set, 0, &sprites[0], rects) == page);
    assert(gles_atlas_texture(&set, 1, &sprites[1], rects + 4) == page);
    assert(gles_atlas_texture(&set, 2, &sprites[2], linked) == page);
    assert(g_atlasResidentPages == 1 && sprite_reads == reads + 4);
    /* This fixture owns the palette bytes and uploads them again. */
    assert(GlTex_UploadRGBA(1, 256, palette, colors));
    atlas_fixture(&set, palette, standalone, page, rects, 1);
    free(actual);
    actual = Gl_Capture();
    assert(actual != NULL && memcmp(actual, expected, 640 * 480 * 4) == 0);

    free(actual);
    free(expected);
    for (i = 0; i < 2; i++) assert(GlTex_Free(standalone[i]));
    assert(GlTex_Free(palette));
    Gles_OnSpriteSetFreed(&set);
    assert(g_glesAtlases == NULL && g_atlasResidentPages == 0);
    assert(glGetError() == GL_NO_ERROR);
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
        check_atlas();
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
    puts("ok: real GLES shaders, FBO, indexed atlas parity and batch counts, present overlay pixels, capture and context recreation");
    return 0;
}
