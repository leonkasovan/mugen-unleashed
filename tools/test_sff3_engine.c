/* Linked against the production engine. Run via tools/test_sff3.py. */
#include "mugen.h"
#include "sffatlas.h"
#include <stdlib.h>
#ifdef main
#undef main
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static SpriteSet *load_exact(const char *path)
{
    Stream *s = Stream_OpenFile(path, "rb");
    CHECK(s);
    SpriteSet *set = Sff_LoadStream(s);
    CHECK(set && Stream_Tell(s) == 0);
    Stream_Close(s);
    return set;
}

static void equal_sprite(SpriteSet *a, int i, SpriteSet *b, int j)
{
    const Sprite *x = (const Sprite *)(a->sprites->data + i*sizeof(Sprite));
    const Sprite *y = (const Sprite *)(b->sprites->data + j*sizeof(Sprite));
    CHECK(x->w == y->w && x->h == y->h && x->bpp == y->bpp &&
          x->x == y->x && x->y == y->y && x->pal_index == y->pal_index);
    unsigned char *p = Sprite_GetPixels(a,i), *q = Sprite_GetPixels(b,j);
    CHECK(p && q && !memcmp(p,q,(size_t)x->w*x->h*((x->bpp+7)/8)));
}

static void equal_sets(SpriteSet *a, SpriteSet *b)
{
    CHECK(b && Sff_SourceVersion(a) == Sff_SourceVersion(b));
    CHECK(a->sprites->count == b->sprites->count && a->palettes->count == b->palettes->count);
    for (int i = 0; i < a->sprites->count; i++) {
        CHECK(a->sprites->slots[i].key1 == b->sprites->slots[i].key1 &&
              a->sprites->slots[i].key2 == b->sprites->slots[i].key2);
        equal_sprite(a,i,b,i);
    }
    for (int i = 0; i < a->palettes->count; i++) {
        const Palette *x = (const Palette *)(a->palettes->data + i*sizeof(Palette));
        const Palette *y = (const Palette *)(b->palettes->data + i*sizeof(Palette));
        CHECK(a->palettes->slots[i].key1 == b->palettes->slots[i].key1 &&
              a->palettes->slots[i].key2 == b->palettes->slots[i].key2 &&
              x->count == y->count && !memcmp(x->data,y->data,x->count*4));
    }
}

static void draw_fixture(SpriteSet *set)
{
    DrawParams dp = {0};
    dp.scale_x = 3.75f; dp.scale_y = 2.25f;
    dp.scale2_x = dp.scale2_y = dp.width_ratio = dp.top_stretch = dp.bottom_stretch = 1;
    dp.alpha_src = dp.alpha_dst = 1;
    dp.masked = 1;
    for (int i = 0; i < set->sprites->count; ++i) {
        dp.x = 18.25f + i; dp.y = 20.75f + i;
        dp.flip = (i & 1) ? DRAW_FLIP_H : 0;
        dp.blend = (i == 2) ? BLEND_ADD : BLEND_NONE;
        Sprite_Draw(set,i,&dp);
    }
}

static void check_soft_frame(SpriteSet *src, SpriteSet *atlas)
{
    g_videoDepth = 16;
    g_videoWidth = g_videoHeight = 64;
    g_videoSurface = SDL_CreateRGBSurfaceWithFormat(0,64,64,16,SDL_PIXELFORMAT_RGB565);
    CHECK(g_videoSurface);
    SpriteSet *target = Screen_Create(16,64,64);
    CHECK(target && Gfx_SetRenderTarget(target,0));
    SDL_Surface *surface = SpriteSet_GetSurface(target,0);
    CHECK(surface);
    g_palFx.mul[0] = g_palFx.mul[1] = g_palFx.mul[2] = g_palFx.color = 256;
    size_t bytes = 64 * surface->pitch;
    unsigned char *ref = malloc(bytes);
    CHECK(ref);
    memset(surface->pixels, 0, bytes);
    draw_fixture(src);
    memcpy(ref, surface->pixels, bytes);
    memset(surface->pixels, 0, bytes);
    CHECK(memcmp(ref, surface->pixels, bytes));
    draw_fixture(atlas);
    CHECK(!memcmp(ref, surface->pixels, bytes));
    free(ref);
    Gfx_SetRenderTarget(NULL,0);
    SpriteSet_Free(target);
    SDL_FreeSurface(g_videoSurface);
    g_videoSurface = NULL;
}

#if !defined(MUGEN_GLES) && !defined(MUGEN_VITA_GL) && !defined(ANDROID_PORT)
static PFNGLBEGINPROC real_begin;
static int draws;
static void APIENTRY count_draw(GLenum mode)
{
    draws++;
    real_begin(mode);
}

static void capture_run(SpriteSet *set, unsigned char *rgba, int first_pair)
{
    glClearColor(0.2f,0.4f,0.6f,1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (first_pair) {
        DrawParams dp = {0};
        dp.scale_x = 3.75f; dp.scale_y = 2.25f;
        dp.scale2_x = dp.scale2_y = dp.width_ratio = dp.top_stretch = dp.bottom_stretch = 1;
        dp.masked = 1; dp.alpha_src = dp.alpha_dst = 1;
        dp.x = 17.75f; dp.y = 16.25f;
        Gl_DrawSprite(0,&dp,set);
        dp.x += 4.25f;
        Gl_DrawSprite(5,&dp,set);
        dp.masked = 0;
        Gl_DrawSprite(5,&dp,set);
    } else draw_fixture(set);
    GlBatch_Flush();
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    CHECK(glGetError() == GL_NO_ERROR);
}

/* Requires the stock vertex and fragment shaders in the data directory. */
static int check_desktop(const char *source, const char *atlas)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_Window *window = SDL_CreateWindow("SFF3 test",0,0,64,64,SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = window ? SDL_GL_CreateContext(window) : NULL;
    if (!ctx) { fprintf(stderr, "OpenGL unavailable: %s\n", SDL_GetError()); return 77; }
    g_videoWidth = g_videoHeight = g_targetWidth = g_targetHeight = 64;
    g_renderMode = RENDER_MODE_GL;
    g_videoDepth = g_targetBpp = 32;
    if (!Gl_Init(1)) { fputs("OpenGL 2.1 framebuffer support unavailable\n",stderr); return 77; }
    CHECK(Gfx_SetRenderTarget(NULL,0));
    g_clipX2 = g_clipY2 = 64;
    Gl_ApplyClipRect();
    g_glSpriteBufferFlags = 0;
    g_palFx.mul[0] = g_palFx.mul[1] = g_palFx.mul[2] = g_palFx.color = 256;
    SpriteSet *original = load_exact(source), *converted = load_exact(atlas);
    unsigned char expected[64*64*4], actual[64*64*4];
    real_begin = glad_glBegin;
    glad_glBegin = count_draw;
    for (int pair = 1; pair >= 0; --pair) {
        draws = 0;
        capture_run(original,expected,pair);
        if (pair) CHECK(draws == 3);
        draws = 0;
        capture_run(converted,actual,pair);
        CHECK(!memcmp(actual,expected,sizeof(actual)));
        if (pair) {
            const char *env = getenv("MUGEN_SPRITEBATCH");
            CHECK(draws == (env && !strcmp(env,"0") ? 3 : 2));
        }
    }
    glad_glBegin = real_begin;
    SpriteSet_Free(converted); SpriteSet_Free(original);
    Gl_Deinit(); SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(window); SDL_Quit();
    puts("desktop GL frames and draw counts match");
    return 0;
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
#if !defined(MUGEN_GLES) && !defined(MUGEN_VITA_GL) && !defined(ANDROID_PORT)
    if (argc == 4 && !strcmp(argv[1],"--gl")) return check_desktop(argv[2],argv[3]);
#endif
    CHECK(argc == 3 || argc == 5);
    g_renderMode = 0;
    g_spriteDecompBuffer = DecompBuffer_Create(16 * 1024 * 1024);
    CHECK(g_spriteDecompBuffer);
    SpriteSet *source = load_exact(argv[1]);
    SpriteSet *set = Sff_Load(argv[2]);
    equal_sets(source,set);
    int legacy = argc == 5 && !strcmp(argv[4], "fallback");
    CHECK(set->version == (legacy ? source->version : SFF_VERSION_300));
    if (argc == 5 && !legacy) check_soft_frame(source,set);
    SpriteSet_Free(set);
    if (argc == 5) {
        int selection[] = {9000,1,SFF_SELECT_END};
        Stream *zip = Stream_OpenZip(argv[3],"char.def");
        CHECK(zip);
        Stream_ZipUnload(zip);
        set = Sff_LoadZipMember(zip,"assets/fixture.sff",NULL);
        CHECK(set && set->version == (legacy ? source->version : SFF_VERSION_300));
        equal_sets(source,set);
        SpriteSet_Free(set);
        Stream_ZipUnload(zip);
        if (!legacy) {
            set = Sff_LoadZipMember(zip,"assets/fixture.sff",selection);
            CHECK(set && set->sprites->count == 1);
            equal_sprite(source,1,set,0);
            SpriteSet_Free(set);
            set = Sff_LoadSelectedFile(argv[2],selection);
            CHECK(set && set->sprites->count == 1);
            equal_sprite(source,1,set,0);
            SpriteSet_Free(set);
        }
        Stream_Close(zip);
    }
    SpriteSet_Free(source);
    free(g_spriteDecompBuffer->data);
    free(g_spriteDecompBuffer);
    return 0;
}
