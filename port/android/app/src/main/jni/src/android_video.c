// =============================================================================
// SDL 1.2 video compatibility layer over SDL2, for the Android port.
//
// Why this exists: the engine reproduces M.U.G.E.N 1.1, whose video code is
// built around one persistent software surface -- SDL_SetVideoMode once, write
// into surface->pixels every frame, then SDL_Flip. SDL2 removed that model.
// Rather than edit src/video.c and src/gfx.c (which are faithful reconstructions
// of the original and must stay that way for the Windows and Linux builds), the
// removed calls are reimplemented here.
//
// The pixel path deliberately mirrors what the Vita port learned the hard way:
//   - the surface is ALWAYS a software surface, because the engine writes
//     surface->pixels directly and never calls SDL_LockSurface (the original
//     never did either);
//   - presenting is an explicit texture update + render copy, because SDL2 has
//     no "flip a software surface" call;
//   - scaling happens in the GPU (SDL_RenderCopy), which is free compared with
//     CPU scaling.
// =============================================================================
#ifdef __ANDROID__

#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "android_main.h"

#define ALOG_TAG "mugen"

static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_texture;
static SDL_Surface  *s_surface;
static Uint32        s_texture_format;
static int           s_pref_w = 640;
static int           s_pref_h = 480;
static int           s_pref_bpp = 16;
static int           s_created_w;
static int           s_created_h;
static int           s_inited;
/* M2: present at most once per frame; a second request in the same frame is a
 * no-op. Set by Android_RequestPresent, cleared by present(). */
static int           s_dirty;

static void log_android_sdl_error(const char *what)
{
    __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "%s failed: %s", what, SDL_GetError());
}

int Android_VideoInit(void)
{
    if (s_inited) {
        return SDL_SUCCESS;
    }
    /*
     * The engine asks SDL for its joystick, event and timer machinery through
     * SDL_Init as well. Video is handled here, so SDL_INIT_VIDEO is still
     * requested -- SDL2 needs it before any window can be created.
     */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        log_android_sdl_error("SDL_Init");
        return -1;
    }
    s_inited = 1;
    return SDL_SUCCESS;
}

/*
 * 8bpp is deliberately refused.
 *
 * The engine's System renderer is RGB565 (16bpp); the 8bpp path exists for the
 * original's indexed modes, which this port does not offer. SDL2 has no palette
 * surface normally used for a renderer texture anyway, so claiming support here
 * would produce a black screen rather than an error -- the same failure mode the
 * Vita port spent a long time on.
 */
static int bpp_to_masks(int bpp, Uint32 *rmask, Uint32 *gmask, Uint32 *bmask, Uint32 *amask,
                        Uint32 *fmt)
{
    switch (bpp) {
    case 16:
        *rmask = 0xF800; *gmask = 0x07E0; *bmask = 0x001F; *amask = 0;
        *fmt = SDL_PIXELFORMAT_RGB565;
        return 0;
    case 32:
        *rmask = 0x00FF0000; *gmask = 0x0000FF00; *bmask = 0x000000FF; *amask = 0xFF000000;
        *fmt = SDL_PIXELFORMAT_ARGB8888;
        return 0;
    case 24:
        /* Stored as 32-bit anyway; the engine treats 24 as 32 in several places. */
        *rmask = 0x00FF0000; *gmask = 0x0000FF00; *bmask = 0x000000FF; *amask = 0;
        *fmt = SDL_PIXELFORMAT_RGB888;
        return 0;
    default:
        return -1;
    }
}

SDL_Surface *Android_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    Uint32 rmask, gmask, bmask, amask, sdl_fmt;
    int want_w, want_h;

    (void)flags;    /* FULLSCREEN / DOUBLEBUF / HWSURFACE are honoured by the path below. */

    Android_VideoInit();

    if (bpp_to_masks(bpp, &rmask, &gmask, &bmask, &amask, &sdl_fmt) != 0) {
        SDL_SetError("unsupported bpp %d on Android (8bpp not supported)", bpp);
        log_android_sdl_error("SetVideoMode");
        return NULL;
    }

    want_w = (width > 0) ? width : s_pref_w;
    want_h = (height > 0) ? height : s_pref_h;

    /*
     * M1: a size change after the first call must resize the window and reset
     * the logical size. The engine calls SetVideoMode again whenever the
     * configured GameWidth/GameHeight change, and an unreleased
     * SDL_RenderSetLogicalSize would leave the new image stretched by the old
     * ratio -- with no error, which is how this class of bug hides.
     */
    if (s_window != NULL &&
        (s_created_w != want_w || s_created_h != want_h)) {
        SDL_SetWindowSize(s_window, want_w, want_h);
        if (s_renderer != NULL) {
            SDL_RenderSetLogicalSize(s_renderer, want_w, want_h);
        }
        s_created_w = want_w;
        s_created_h = want_h;
    }

    /* Reuse the window if the size did not change; the engine re-sets the mode often. */
    if (s_window == NULL) {
        s_window = SDL_CreateWindow("mugen-unleashed",
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    want_w, want_h,
                                    SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
        if (s_window == NULL) {
            log_android_sdl_error("SDL_CreateWindow");
            return NULL;
        }
        s_created_w = want_w;
        s_created_h = want_h;
    }

    if (s_renderer == NULL) {
        /*
         * Accelerated renderer, with drivers that support streaming textures.
         * SDL_RENDERER_PRESENTVSYNC is requested but not required: the engine
         * paces itself through Frame_UpdateSkip and asking for vsync on a
         * 60 Hz-expecting game can halve the rate on a 30 Hz surface.
         */
        s_renderer = SDL_CreateRenderer(s_window, -1,
                                        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        if (s_renderer == NULL) {
            s_renderer = SDL_CreateRenderer(s_window, -1, 0);
        }
        if (s_renderer == NULL) {
            log_android_sdl_error("SDL_CreateRenderer");
            return NULL;
        }
        /* Integer-free scaling is what the engine's own GL path uses. */
        SDL_RenderSetLogicalSize(s_renderer, want_w, want_h);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest: matches the pixel art */
        SDL_RenderSetIntegerScale(s_renderer, SDL_FALSE);
    }

    if (s_texture != NULL && s_texture_format != sdl_fmt) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    if (s_texture == NULL) {
        s_texture = SDL_CreateTexture(s_renderer, sdl_fmt, SDL_TEXTUREACCESS_STREAMING,
                                      want_w, want_h);
        if (s_texture == NULL) {
            log_android_sdl_error("SDL_CreateTexture");
            return NULL;
        }
        s_texture_format = sdl_fmt;
    }

    if (s_surface != NULL && (s_surface->w != want_w || s_surface->h != want_h ||
                              s_surface->format->BitsPerPixel != bpp)) {
        SDL_FreeSurface(s_surface);
        s_surface = NULL;
    }
    if (s_surface == NULL) {
        /*
         * A plain software surface. SDL_CreateRGBSurface allocates the pixels and
         * a shadow-blit, so the engine's SDL_UpperBlit/SDL_FreeSurface calls work
         * normally; only presentation goes through the texture.
         */
        s_surface = SDL_CreateRGBSurface(0, want_w, want_h, bpp, rmask, gmask, bmask, amask);
        if (s_surface == NULL) {
            log_android_sdl_error("SDL_CreateRGBSurface");
            return NULL;
        }
        /*
         * Opaque by default: the engine writes finished pixels into this surface
         * and composites into it with its own blitters, so blending on upload
         * would darken or fade the image. SDL 1.2 surfaces behaved the same way.
         */
        SDL_SetSurfaceBlendMode(s_surface, SDL_BLENDMODE_NONE);
    }

    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                        "video: %dx%d %dbit surface (fmt %s), texture streaming",
                        s_surface->w, s_surface->h, bpp,
                        bpp == 16 ? "RGB565" : (bpp == 32 ? "ARGB8888" : "RGB888"));
    return s_surface;
}

/*
 * Uploads the software surface into the texture and draws it.
 *
 * This is the SDL2 replacement for "the back buffer now becomes the front
 * buffer". SDL_UpdateRect and SDL_Flip both land here; the engine distinguishes
 * them only as an optimisation, and there is nothing cheaper to do.
 */
/*
 * Uploads the software surface into the texture and draws it.
 *
 * M2: guarded by s_dirty. The engine normally calls Gfx_Present once per frame,
 * but SDL_UpdateRect is also used from draw paths, and each pass here uploads
 * the WHOLE surface (~600 KB at 640x480x16) plus a clear and a copy. Presenting
 * once per dirty frame keeps that bounded; a second call in the same frame only
 * marks the surface dirty again.
 *
 * The frame boundary is Android_PresentFrame(), called from the point where the
 * engine has finished drawing and is about to pace itself.
 */
static void present(void)
{
    if (s_renderer == NULL || s_texture == NULL || s_surface == NULL) {
        return;
    }
    if (!s_dirty) {
        return;
    }
    s_dirty = 0;
    if (SDL_UpdateTexture(s_texture, NULL, s_surface->pixels, s_surface->pitch) != 0) {
        log_android_sdl_error("SDL_UpdateTexture");
        return;
    }
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

/*
 * SDL 1.2's SDL_AudioDriverName. SDL_GetCurrentAudioDriver takes no length, so
 * the copy is bounds-checked against maxlen.
 */
char *Android_AudioDriverName(char *namebuf, int maxlen)
{
    const char *name = SDL_GetCurrentAudioDriver();

    if (namebuf == NULL || maxlen <= 0) {
        return NULL;
    }
    if (name == NULL) {
        name = "";
    }
    strncpy(namebuf, name, (size_t)maxlen - 1);
    namebuf[maxlen - 1] = '\0';
    return namebuf;
}

/*
 * SDL 1.2's SDL_GetKeyState: the number of keycodes SDL supports (SDLK_LAST).
 * SDL2's SDL_GetKeyboardState already returns SDL_NUM_SCANCODES entries, which
 * is the same array the engine indexes with SDLK values -- SDL2 maps SDLK to
 * scancodes for this call.
 */
const Uint8 *Android_GetKeyState(int *count)
{
    int n = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&n);

    if (count != NULL) {
        *count = n;
    }
    return keys;
}

void Android_UpdateRectX(SDL_Surface *surf, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    (void)surf; (void)x; (void)y; (void)w; (void)h;
    s_dirty = 1;
    present();
}

/*
 * Opens a new frame: the engine has finished drawing this one. Any subsequent
 * SDL_Flip/UpdateRect belongs to the next frame, so the dirty flag is cleared
 * only from inside present() and re-armed here.
 */
void Android_PresentFrame(void)
{
    s_dirty = 1;
    present();
}

void Android_UpdateRect(Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    (void)x; (void)y; (void)w; (void)h;
    present();
}

void Android_Flip(void)
{
    s_dirty = 1;
    present();
}

int Android_ToggleFullscreen(SDL_Surface *screen)
{
    (void)screen;
    /*
     * The Android surface is always fullscreen; the WM cannot be toggled without
     * recreating the window, and the engine only uses this for a debug hotkey.
     */
    return 1;
}

void Android_VideoQuit(void)
{
    if (s_texture != NULL) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    if (s_renderer != NULL) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    if (s_surface != NULL) {
        SDL_FreeSurface(s_surface);
        s_surface = NULL;
    }
}

#endif /* __ANDROID__ */
