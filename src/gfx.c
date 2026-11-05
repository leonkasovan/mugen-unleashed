/*
 * Graphics core (original module 0x46B7A0-0x46C26F): fades, the screen
 * buffer, render targets and the clip rectangle, presenting the screen
 * buffer, and the selection of the renderer function tables.
 *
 * The render target is a sprite of a sprite set (an SDL surface, or an
 * OpenGL "vbuf" with the OpenGL renderer), or the video surface itself when
 * g_renderTarget is NULL. g_target* and g_clip* describe it to the drawing
 * code.
 */
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#endif
/*
 * Neither <SDL.h> nor <GL/gl.h> here: mugen.h includes glad.h (the whole GL API,
 * fixed-function included) before SDL.h, and glad.h refuses to load if a GL
 * header came first. SDL.h would pull in <GL/gl.h> via SDL_opengl.h.
 */

#include "mugen.h"

/* SDL_gfx, as in src/font.c: the header is not in the SDK. */
#ifndef ANDROID_PORT
extern SDL_Surface *__cdecl zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#endif

#define GL_FRAMEBUFFER_EXT 0x8D40

/* Sprite slot `index` of a sprite set, as the original computes it (no bounds check). */
static Sprite *Gfx_SpriteAt(const SpriteSet *set, int index)
{
    const KeyedArray *a = set->sprites;

    return (Sprite *)(a->data + a->elem_size * index);
}

/* ---- fades ---- */

/*
 * 0x0046B7A0: updates the progress of a fade (and the global PalFX of a
 * palette fade); advance counts the tick. A finished fade is reset.
 */
void Fade_Update(Fade *fade, int advance)
{
    float progress;

    if (fade->type == FADE_NONE) {
        return;
    }
    if (fade->ticks >= fade->time || fade->time == 0) {
        fade->bar_size = 0;
        fade->progress = 0.0f;
        fade->type = FADE_NONE;
        fade->unused_04 = 0x100;
        fade->ticks = 0;
        fade->time = 0;
        fade->out = 0;
        return;
    }
    progress = (float)((double)fade->ticks / (double)fade->time);
    if (!fade->out) {
        progress = (float)(1.0 - progress);
    }
    fade->progress = progress;
    if (fade->type == FADE_PALETTE) {
        int mul = (int)((1.0 - progress) * 256.0);
        PalFx_ScaleGlobalMul(mul, mul, mul);
    }
    if (advance) {
        fade->ticks++;
    }
}

/*
 * 0x0046B830: draws a FADE_BARS fade: black bars from the top and bottom
 * edges, followed by 12 bands of bar_size / 12 pixels that get lighter.
 */
void Fade_DrawBars(const Fade *fade)
{
    int bar = (int)((double)(g_targetHeight / 2 + fade->bar_size) * fade->progress - (double)fade->bar_size);
    int step;
    int alpha;

    g_gfxFillRect(0, 0, g_targetWidth, bar, 0);
    g_gfxFillRect(0, g_targetHeight - bar, g_targetWidth, g_targetHeight, 0);
    step = fade->bar_size / 12;
    for (alpha = 0; alpha < 12 * 0x100; alpha += 0x100) {
        int next = bar + step;
        g_gfxBlendRect(0, bar, g_targetWidth, next - 1, 0, 0x100, alpha / 12);
        g_gfxBlendRect(0, g_targetHeight - next, g_targetWidth, g_targetHeight - bar - 1, 0, 0x100, alpha / 12);
        bar = next;
    }
}

/* ---- screen buffer and render targets ---- */

/*
 * 0x0046B930: a sprite set holding one width x height sprite to render to:
 * an SDL surface in the video surface's format, or an OpenGL vbuf. Quirk:
 * the set leaks when a KeyedArray cannot be created.
 */
SpriteSet *Screen_Create(int depth, int width, int height)
{
    SpriteSet *set;
    Sprite spr;
    int i;

    set = calloc(1, sizeof(SpriteSet));
    if (set == NULL) {
        g_outOfMemory = 1;
        return NULL;
    }
    set->sprites = KeyedArray_Create(1, sizeof(Sprite));
    if (set->sprites == NULL) {
        return NULL;
    }
    set->palettes = KeyedArray_Create(1, sizeof(Palette));
    if (set->palettes == NULL) {
        return NULL;
    }
    for (i = 0; i < 1; i++) {
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
        /*
         * M8: the GLES backend gives Android a real GL renderer, so the screen
         * buffer is a render-target texture in GL mode here too. With a CPU
         * surface instead, Gl_Present had no texture to draw and the window
         * stayed black while sprites rendered invisibly.
         */
        if (g_renderMode == RENDER_OPENGL) {
            void *vbuf = GlVbuf_Alloc(depth, width, height);
            if (!Sprite_Init(SPRITE_INIT_OWN, &spr, SPRITE_FORMAT_TEXTURE, vbuf, 0x1C, width, height, depth, 0, 0)) {
                SpriteSet_Free(set);
                return NULL;
            }
        } else
#endif
        {
            const SDL_PixelFormat *fmt = g_videoSurface->format;
            SDL_Surface *surface = SDL_CreateRGBSurface(0, width, height, depth, fmt->Rmask, fmt->Gmask, fmt->Bmask,
                                                        fmt->Amask);
            if (surface == NULL) {
                SpriteSet_Free(set);
                return NULL;
            }
            SDL_SetClipRect(surface, NULL);
            if (!Sprite_Init(SPRITE_INIT_OWN, &spr, SPRITE_FORMAT_SURFACE, surface, sizeof(SDL_Surface), width, height,
                             depth, 0, 0)) {
                SpriteSet_Free(set);
                return NULL;
            }
        }
        if (!SpriteSet_AddSprite(set, &spr, 0, i)) {
            SpriteSet_Free(set);
            return NULL;
        }
    }
    return set;
}

/*
 * 0x0046BA90: renders to sprite `index` of target, or to the screen when
 * target is NULL, and resets the clip rectangle. Returns 0 (possibly with
 * the target globals already changed) when the sprite cannot be a target.
 */
int Gfx_SetRenderTarget(SpriteSet *target, int index)
{
    int changed = !(target == g_renderTarget && index == g_renderTargetSprite);
    Sprite *spr;

    if (target == NULL) {
        g_targetWidth = g_videoWidth;
        g_targetHeight = g_videoHeight;
        g_renderTarget = NULL;
        g_renderTargetSprite = 0;
        g_targetBpp = g_videoDepth;
        g_targetPixels = 0;
        g_targetBytesPerPixel = g_videoBytesPerPixel;
        g_clipRowSkip = 0;
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
        /* M8: GL mode exists on Android too (see Screen_Create). */
        if (g_renderMode == RENDER_OPENGL && changed) {
#ifdef MUGEN_GLES
            /* GLES has no *EXT framebuffer entry points; GlVbuf_Bind(NULL)
             * binds the window and re-establishes the viewport/projection. */
            GlVbuf_Bind(NULL);
#else
            g_glewBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
            g_glBoundFramebuffer = 0;
            glViewport(0, 0, g_videoWidth, g_videoHeight);
            Gl_SetupView();
#endif
        }
#endif
        Gfx_SetClipRect(0, 0, g_targetWidth, g_targetHeight);
        return 1;
    }
    spr = Gfx_SpriteAt(target, index);
    if (spr == NULL || spr->format < 0 || (spr->format > SPRITE_FORMAT_SURFACE && spr->format != SPRITE_FORMAT_TEXTURE)) {
        return 0;
    }
    g_renderTarget = target;
    g_targetWidth = spr->w;
    g_targetHeight = spr->h;
    g_targetPixels = spr->w * spr->h;
    g_renderTargetSprite = index;
    g_targetBpp = spr->bpp;
    g_targetBytesPerPixel = (spr->bpp + 7) >> 3;
    g_clipRowSkip = 0;
#if defined(MUGEN_GLES)
    /* M8: bind the render target's framebuffer, as the desktop path does. */
    if (spr->format == SPRITE_FORMAT_TEXTURE && (spr->pixels == NULL || !GlVbuf_Bind(spr->pixels))) {
#elif defined(ANDROID_PORT)
    if (spr->format == SPRITE_FORMAT_TEXTURE) {
#else
    if (spr->format == SPRITE_FORMAT_TEXTURE && (spr->pixels == NULL || !GlVbuf_Bind(spr->pixels))) {
#endif
        return 0;
    }
    Gfx_SetClipRect(0, 0, g_targetWidth, g_targetHeight);
    return 1;
}

/* 0x0046BC10: the SDL surface of a sprite, or NULL if it is not one. */
SDL_Surface *SpriteSet_GetSurface(const SpriteSet *set, int sprite)
{
    const Sprite *spr = Gfx_SpriteAt(set, sprite);

    if (spr != NULL && spr->format == SPRITE_FORMAT_SURFACE) {
        return spr->pixels;
    }
    return NULL;
}

/*
 * 0x0046BC30: the screen as an SDL surface (screenshots): a new 32-bit copy
 * of the captured screen with the OpenGL renderers, else the render target's
 * own surface (NULL for none).
 */
SDL_Surface *Gfx_GetScreenSurface(void)
{
    if (g_renderMode == RENDER_MODE_GL || g_renderMode == RENDER_MODE_GL_ALT) {
        void *pixels = g_gfxCapture();
        SDL_Surface *surface;
        if (pixels == NULL) {
            return NULL;
        }
        surface = SDL_CreateRGBSurface(0, g_targetWidth, g_targetHeight, 32, 0x000000FF, 0x0000FF00, 0x00FF0000,
                                       0xFF000000);
        if (surface == NULL) {
            free(pixels);
            return NULL;
        }
        memcpy(surface->pixels, pixels, g_targetWidth * g_targetHeight * 4);
        free(pixels);
        return surface;
    }
    if (g_renderTarget != NULL) {
        const Sprite *spr = Gfx_SpriteAt(g_renderTarget, g_renderTargetSprite);
        if (spr != NULL && spr->format == SPRITE_FORMAT_SURFACE) {
            return spr->pixels;
        }
    }
    return NULL;
}

#ifndef ANDROID_PORT
/* Smallest power of two >= n, computed as the original does (0 for n <= 1). */
static int Gfx_NextPow2(int n)
{
    unsigned int x = (unsigned int)n - 1;

    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x == 0 ? 0 : (int)(x + 1);
}

/* OpenGLScreen: uploads the RGB565 screen buffer to g_glScreenTex and draws it to the window. */
static void Gfx_PresentGlScreen(const SDL_Surface *surface)
{
    unsigned int *rgba = malloc(surface->h * surface->w * 4);
    int tex_w = Gfx_NextPow2(surface->w);
    int tex_h = Gfx_NextPow2(surface->h);
    const unsigned short *src;
    unsigned int *dst;
    int n;

    if (g_glScreenTexReady && g_glScreenTex.w >= tex_w && g_glScreenTex.h >= tex_h && !g_glScreenResized) {
        /* the texture is big enough */
    } else {
        if (g_glScreenTexReady) {
            if (g_glScreenTexCur != NULL) {
                g_glScreenTexCur->refs--;
            }
            GlTex_Delete(&g_glScreenTex);
            g_glScreenTexReady = 0;
            g_glScreenResized = 0;
        }
        GlTex_Init(&g_glScreenTex);
        GlTex_Alloc(&g_glScreenTex, tex_w, tex_h);
        g_glScreenTex.refs++;
        g_glScreenTexCur = &g_glScreenTex;
        g_glScreenTexReady = 1;
    }
    src = surface->pixels;
    dst = rgba;
    for (n = surface->h * surface->w; n != 0; n--) {
        unsigned int px = *src++;
        /* RGB565 -> R, G, B, 0xFF bytes */
        *dst++ = (((px | 0xFFFFFFE0u) << 14 | (px & 0x7E0)) << 5) | ((px >> 8) & 0xF8);
    }
    GlScreen_Upload(surface->h, surface->w, rgba);
    GlScreen_Draw(g_screenSurface->w, g_screenSurface->h);
    free(rgba);
}

#endif
/*
 * 0x0046BCF0: g_gfxPresent of the System and OpenGLScreen renderers: shows
 * the screen buffer (sprite `sprite` of set) at (x, y), then flips the video
 * surface when double buffered. w and h are only used by the OpenGL
 * renderer's version.
 */
#ifdef MUGEN_VITA_GL
/*
 * Scales src into dst for the PS Vita port, writing pixels directly.
 *
 * Three paths: an exact 2x uint16 fast path (the default 480x272 -> 960x544),
 * a general nearest-neighbour path using a cached integer column map, and
 * zoomSurface for anything the first two cannot express (differing pixel
 * formats). The column map is rebuilt only when the sizes change.
 *
 * Nearest neighbour is the right filter here: the default scale is an exact
 * integer, where any smoothing would only cost time and soften the image.
 */
static void Vita_ScaleToVideo(SDL_Surface *src, SDL_Surface *dst)
{
    static int *xmap;
    static int xmap_w;
    static int xmap_sw;

    int sw = src->w;
    int sh = src->h;
    int dw = dst->w;
    int dh = dst->h;
    int bpp = src->format->BytesPerPixel;
    int y;
    Uint8 *dbase = (Uint8 *)dst->pixels;

    /* Formats must agree for a direct copy; otherwise let SDL_gfx convert. */
    if (src->format->BitsPerPixel != dst->format->BitsPerPixel || bpp < 1 || bpp > 4 ||
        dbase == NULL || src->pixels == NULL) {
        SDL_Surface *zoomed = zoomSurface(src, (double)dw / (double)sw, (double)dh / (double)sh, 0);

        if (zoomed != NULL) {
            SDL_Rect full;
            full.x = 0;
            full.y = 0;
            full.w = 0;
            full.h = 0;
            SDL_UpperBlit(zoomed, NULL, dst, &full);
            SDL_FreeSurface(zoomed);
        }
        return;
    }

    /*
     * Exact 2x, 16-bit: duplicate each pixel horizontally, each row vertically.
     *
     * One 32-bit store per source pixel rather than two 16-bit stores -- the
     * duplicated pair is a single word (c | c << 16) -- and the second output
     * row is a row copy. This matters because the video surface's pixels are
     * GPU-mapped memory (gxm_init allocates it with
     * vita_gpu_alloc_mapped_aligned), so store count and coalescing cost more
     * here than they would in ordinary cached RAM.
     */
    if (dw == sw * 2 && dh == sh * 2 && bpp == 2) {
        const Uint8 *sbase = (const Uint8 *)src->pixels;
        Uint8 *row = dbase;
        const size_t dstride = (size_t)dst->pitch;

        for (y = 0; y < sh; y++) {
            const Uint16 *s = (const Uint16 *)(sbase + (size_t)y * src->pitch);
            Uint32 *d0 = (Uint32 *)row;
            int x;

            for (x = 0; x < sw; x++) {
                Uint32 c = s[x];
                d0[x] = c | (c << 16);
            }
            memcpy(row + dstride, row, (size_t)dw * 2);
            row += dstride * 2;
        }
        return;
    }

    /* General nearest neighbour with a cached column map. */
    if (xmap == NULL || xmap_w != dw || xmap_sw != sw) {
        if (xmap != NULL) {
            free(xmap);
        }
        xmap = malloc((size_t)dw * sizeof(int));
        if (xmap == NULL) {
            xmap_w = 0;
            return;
        }
        for (y = 0; y < dw; y++) {
            xmap[y] = y * sw / dw;
        }
        xmap_w = dw;
        xmap_sw = sw;
    }
    for (y = 0; y < dh; y++) {
        const Uint8 *s = (const Uint8 *)src->pixels + (size_t)(y * sh / dh) * src->pitch;
        Uint8 *d = dbase + (size_t)y * dst->pitch;
        int x;

        if (bpp == 2) {
            for (x = 0; x < dw; x++) {
                ((Uint16 *)d)[x] = ((const Uint16 *)s)[xmap[x]];
            }
        } else if (bpp == 4) {
            for (x = 0; x < dw; x++) {
                ((Uint32 *)d)[x] = ((const Uint32 *)s)[xmap[x]];
            }
        } else {
            for (x = 0; x < dw; x++) {
                memcpy(d + (size_t)x * bpp, s + (size_t)xmap[x] * bpp, (size_t)bpp);
            }
        }
    }
}
#endif

void Gfx_Present(SpriteSet *set, int sprite, float x, float y, float w, float h)
{
#ifdef ANDROID_PORT
    SDL_Surface *surface;
    SDL_Rect dst;

    if (g_gfxPrePresent != NULL) {
        SpriteSet *target = g_renderTarget;
        int target_sprite = g_renderTargetSprite;
        g_gfxPrePresent(set, sprite);
        Gfx_SetRenderTarget(target, target_sprite);
    }
    surface = SpriteSet_GetSurface(set, sprite);
    if (surface == NULL || g_screenSurface == NULL) {
        return;
    }
    dst.x = (int)x;
    dst.y = (int)y;
    dst.w = 0;
    dst.h = 0;
    if (SDL_BlitSurface(surface, NULL, g_screenSurface, &dst) == 0) {
        Android_MarkDisplayDirty(g_screenSurface);
    }
#else

    SDL_Surface *surface = NULL;

    if (g_gfxPrePresent != NULL) {
        SpriteSet *target = g_renderTarget;
        int target_sprite = g_renderTargetSprite;
        g_gfxPrePresent(set, sprite);
        Gfx_SetRenderTarget(target, target_sprite);
    }
    if (g_renderMode != RENDER_MODE_GL && g_renderMode != RENDER_MODE_GL_ALT) {
        const Sprite *spr = Gfx_SpriteAt(set, sprite);
        if (spr == NULL || spr->format != SPRITE_FORMAT_SURFACE) {
            return;
        }
        surface = spr->pixels;
    }
    if (g_renderMode == RENDER_OPENGL_SCREEN) {
        Gfx_PresentGlScreen(surface);
    } else if (g_renderMode == RENDER_SYSTEM) {
        SDL_Rect dst;
#ifdef ANDROID_PORT
        /*
         * Android port: no CPU scaling.
         *
         * The Vita branch below scales the game surface up to the panel because
         * its SDL driver flips 1:1. Android's present path is an SDL_RenderCopy
         * of a streaming texture, so the GPU does the scaling for free -- doing
         * it on the CPU first would be pure waste, and would drag
         * Vita_ScaleToVideo's pixel-format assumptions onto a platform it was
         * never tested on.
         */
        dst.x = (Sint16)(int)x;
        dst.y = (Sint16)(int)y;
        dst.w = 0;
        dst.h = 0;
        SDL_UpperBlit(surface, NULL, g_screenSurface, &dst);
        SDL_UpdateRect(g_screenSurface, 0, 0, 0, 0);
#elif defined(MUGEN_VITA_GL)
        /*
         * PS Vita port (port/psvita): stretch the game surface up to the panel.
         *
         * The System renderer opens a surface of the *game* size -- 480x272 by
         * default, so the software blitter does a quarter of the per-pixel work
         * of a full panel size -- while the SDL video surface is the panel size
         * (960x544). The driver's flip draws that surface one-to-one, so
         * something has to scale in between.
         *
         * Not SDL_VITA_SetVideoModeScaling: it writes a scale into the driver's
         * video struct that only its software-blit path reads, and this build
         * never uses that path (VITA_UpdateRects is an empty stub,
         * VITA_FlipHWSurface passes no scale).
         *
         * Not zoomSurface either, at least not as the normal path: it allocates
         * a fresh panel-sized surface and runs generic nearest-neighbour code on
         * every call, which cost 60 fps -> 4.4 fps when it was the per-frame
         * path. This writes straight into the video surface's pixels with a
         * cached column map, and specialises the exact-2x case that the default
         * 480x272 -> 960x544 is. SDL_MUSTLOCK is false for this surface (its
         * flags carry no HWSURFACE/ASYNCBLIT/RLEACCEL), so writing pixels
         * directly is legal.
         *
         * zoomSurface remains the fallback for formats or ratios the fast paths
         * do not cover.
         */
        if (g_videoSurface != NULL && surface != NULL &&
            (surface->w != g_videoSurface->w || surface->h != g_videoSurface->h)) {
            int64_t t0 = 0;

            QueryPerformanceCounter(&t0);
            Vita_ScaleToVideo(surface, g_videoSurface);
            {
                int64_t t1 = 0;
                QueryPerformanceCounter(&t1);
                Vita_PerfAddScale(t1 - t0);
            }
        } else
#endif
        {
            dst.x = (Sint16)(int)x;
            dst.y = (Sint16)(int)y;
            dst.w = 0;
            dst.h = 0;
            SDL_UpperBlit(surface, NULL, g_screenSurface, &dst);
            /* On the SDL2 desktop backend UpdateRect only dirties the display;
             * the frame-boundary flush in Screen_Present (Sdl2_PresentFrame)
             * does the single upload+present, and the PageFlip block below
             * presents via Flip -- either way one present per frame. On
             * SDL 1.2 UpdateRect is the presentation of the single-buffered
             * surface and Flip of a double-buffered one, and each driver
             * ignores the call that does not apply -- the original engine
             * could call both for that reason. */
            SDL_UpdateRect(g_screenSurface, 0, 0, 0, 0);
        }
    }
#ifdef MUGEN_VITA_GL
    /*
     * PS Vita port (port/psvita): present with SDL_Flip.
     *
     * The Vita's SDL video driver hands back a double-buffered surface whether
     * or not SDL_DOUBLEBUF was requested. mugen.log records exactly that:
     *
     *     video: surface 960x544 16bit flags 0xC0000020
     *
     * 0xC0000020 is SDL_FULLSCREEN|SDL_DOUBLEBUF, for a plain SDL_SWSURFACE
     * request. SDL 1.2 only presents a double-buffered surface on SDL_Flip --
     * SDL_UpdateRect merely marks the shadow buffer dirty.
     *
     * The engine flips only when VIDEO_HWBLIT is set (BlitMode = PageFlip, or
     * the OpenGL renderers), and the System renderer does not set it. So every
     * frame was drawn into the back buffer and never shown: the log ran to
     * completion, the loop paced itself at 60 fps, and the panel stayed black.
     *
     * Flip whenever the driver actually gave us double buffering, and stay out
     * of the way when the generic block below is about to flip anyway.
     */
    if ((g_videoFlags & VIDEO_HWBLIT) == 0 && g_videoSurface != NULL &&
        (g_videoSurface->flags & SDL_DOUBLEBUF) != 0) {
        int64_t t0 = 0;

        QueryPerformanceCounter(&t0);
        SDL_Flip(g_videoSurface);
        {
            int64_t t1 = 0;
            QueryPerformanceCounter(&t1);
            Vita_PerfAddFlip(t1 - t0);
        }
    }
#endif
    if (g_videoFlags & VIDEO_HWBLIT) {
        if (g_videoSdlFlags & SDL_DOUBLEBUF) {
#ifdef ANDROID_PORT
            /*
             * Android port: no GL renderer is compiled in (glrender.c is
             * excluded), so the OpenGL branches below cannot be reached and the
             * GL calls are not available. Only the software flip remains.
             */
            SDL_Flip(g_videoSurface);
#else
            if (g_renderMode == RENDER_OPENGL || g_renderMode == RENDER_OPENGL_SCREEN) {
                SDL_GL_SwapBuffers();
            } else {
                SDL_Flip(g_videoSurface);
            }
#endif
        }
#ifndef ANDROID_PORT
        if (g_renderMode == RENDER_OPENGL) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClearDepth(1.0);
            glDisable(GL_SCISSOR_TEST);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
#endif
    }

#endif
}

/*
 * 0x0046BFC0: sets the clip rectangle (x2/y2 exclusive) of the render
 * target, ordered and clamped to it, and passes it to the target's SDL
 * surface or to OpenGL.
 */
void Gfx_SetClipRect(int x1, int y1, int x2, int y2)
{
    const Sprite *spr = NULL;
    int t;

    if (g_renderTarget != NULL) {
        spr = Gfx_SpriteAt(g_renderTarget, g_renderTargetSprite);
    }
    if (x1 > x2) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x1 > g_targetWidth - 1) {
        x1 = g_targetWidth - 1;
    }
    if (y1 > g_targetHeight - 1) {
        y1 = g_targetHeight - 1;
    }
    if (x2 < x1) {
        x2 = x1;
    }
    if (y2 < y1) {
        y2 = y1;
    }
    if (x2 > g_targetWidth) {
        x2 = g_targetWidth;
    }
    if (y2 > g_targetHeight) {
        y2 = g_targetHeight;
    }
    g_clipX2 = x2;
    g_clipY2 = y2;
    g_clipX1 = x1;
    g_clipY1 = y1;
    g_clipW = x2 - x1;
    g_clipH = y2 - y1;
    g_clipRowSkip = g_targetWidth - (x2 - x1);
    if (spr != NULL && spr->format == SPRITE_FORMAT_SURFACE) {
        SDL_Rect rect;
        rect.x = (Sint16)x1;
        rect.y = (Sint16)y1;
        rect.w = (Uint16)(x2 - x1);
        rect.h = (Uint16)(y2 - y1);
        SDL_SetClipRect(spr->pixels, &rect);
    }
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
    /*
     * M8 fix: the GLES backend made Android a GL platform, but this call was
     * still behind "not Android". Nothing then set the scissor rectangle, so
     * every GLES draw ignored the clip rect and covered the whole target.
     *
     * The visible symptom was the title screen: its background is split into
     * `window`-clipped strips (several only two pixels tall) and two of them are
     * `trans = add`, so drawing them unclipped tiled an additive copy of the
     * whole backdrop over the frame and saturated it to pure white. The same
     * omission dropped the menu's own item window, so text never scrolled or
     * clipped either.
     */
    if (g_renderMode == RENDER_OPENGL) {
        Gl_ApplyClipRect();
    }
#endif

}

/* v * scale rounded to the nearest integer (halves up), with the original's float steps. */
static int Gfx_ScaleRound(float v, float scale)
    {
    float scaled = v * scale;
    float rounded = (float)floor(scaled + 0.5);

    return (int)rounded;
}

/* 0x0046C0C0: Gfx_SetClipRect with coordinates scaled by (sx, sy) and rounded. */
void Gfx_SetClipRectScaled(float x1, float y1, float x2, float y2, float sx, float sy)
{
    int iy2 = Gfx_ScaleRound(y2, sy);
    int ix2 = Gfx_ScaleRound(x2, sx);
    int iy1 = Gfx_ScaleRound(y1, sy);
    int ix1 = Gfx_ScaleRound(x1, sx);

    Gfx_SetClipRect(ix1, iy1, ix2, iy2);
}

/* 0x0046C190: points the renderer entry points at those of renderer `mode` (g_renderMode). */
void Gfx_SelectRenderer(int mode)
{
    g_gfxClear = g_gfxClearFns[mode];
    g_gfxPutPixel = g_gfxPutPixelFns[mode];
    g_gfxAddPixel = g_gfxAddPixelFns[mode];
    g_gfxInvertPixel = g_gfxInvertPixelFns[mode];
    g_gfxDrawLine = g_gfxDrawLineFns[mode];
    g_gfxDrawLineAdd = g_gfxDrawLineAddFns[mode];
    g_gfxDrawRect = g_gfxDrawRectFns[mode];
    g_gfxDrawRectAdd = g_gfxDrawRectAddFns[mode];
    g_gfxDrawRectBlend = g_gfxDrawRectBlendFns[mode];
    g_gfxDrawRectSub = g_gfxDrawRectSubFns[mode];
    g_gfxFillRect = g_gfxFillRectFns[mode];
    g_gfxFillRectColor = g_gfxFillRectColorFns[mode];
    g_gfxBlendRect = g_gfxBlendRectFns[mode];
    g_gfxFillRectSub = g_gfxFillRectSubFns[mode];
    g_gfxCapture = g_gfxCaptureFns[mode];
    g_gfxPresent = g_gfxPresentFns[mode];
}

/*
 * M6 capability query (plan section 10.6): what the selected renderer can
 * actually do, from compiled-in tables and live state -- not from macros
 * or link success. A NULL/stub table entry means the mode is not
 * functional on this profile (section 10.7): the caller must not select it
 * outside a fallback it reports.
 *
 * Bits (GfxCap_* in gfx.h): SYSTEM always; TRUECOLOR_SOFT since the
 * true-colour software path; GPU_SPRITE only when the GL drawers are linked
 * (g_gfxPresentFns entry non-NULL and not the Stub_Nop); GLScreen whenever
 * System drawing exists (it presents the same buffer through GL).
 */
unsigned int Gfx_Capability(int mode)
{
    unsigned int caps = 0;

    if (mode < 0 || mode >= GFX_RENDERER_COUNT) {
        return 0;
    }
    /* System drawing is the reference path on every profile. */
    caps |= GFX_CAP_SYSTEM;
    /* True-colour software rasterizer (softrender.c true-colour path). */
    caps |= GFX_CAP_TRUECOLOR_SOFT;
    if (g_gfxPresentFns[mode] != NULL && g_gfxPresentFns[mode] != (GfxPresentFn)Stub_Nop) {
        caps |= GFX_CAP_PRESENT;
        if (mode == RENDER_OPENGL) {
            caps |= GFX_CAP_GPU_SPRITE | GFX_CAP_FBO;
        }
        if (mode == RENDER_OPENGL_SCREEN) {
            caps |= GFX_CAP_GLSCREEN;
        }
    }
    return caps;
}

/*
 * M6 startup log (plan section 10.1 + 10.10): requested mode (from the
 * config) vs effective renderer (actually selected), once. The names stay
 * the config values; DIRECTX is reported as the System alias it is.
 */
static const char *Gfx_ModeName(int mode)
{
    switch (mode) {
    case RENDER_SYSTEM:
        return "system";
    case RENDER_OPENGL_SCREEN:
        return "openglscreen";
    case RENDER_OPENGL:
        return "opengl";
    default:
        return "unknown";
    }
}

void Gfx_LogRenderer(int requested, int effective, int directx)
{
    Log_Printf("renderer: requested=%s%s effective=%s caps=0x%X\n", Gfx_ModeName(requested),
               directx ? "+directx(alias)" : "", Gfx_ModeName(effective), Gfx_Capability(effective));
}
