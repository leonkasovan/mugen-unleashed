/*
 * SDL 1.2 over SDL2: the removed entry points, reimplemented.
 *
 * Used only by the desktop SDL2 build (Makefile SDL_BACKEND=2). The SDL 1.2
 * build and the Android/Vita ports do not compile this file.
 *
 * The video model is the one the original relies on and SDL2 deleted:
 *
 *   SDL_SetVideoMode once -> write into surface->pixels every frame -> flip.
 *
 * Two presentation backends cover the engine's two renderers:
 *
 *   - OpenGL (RENDER_OPENGL / RENDER_OPENGL_SCREEN): a real window with
 *     SDL_WINDOW_OPENGL and a GL context; the engine's GL renderer draws
 *     straight into it and Sdl2_GL_SwapBuffers presents. The returned surface
 *     is a 1x1 placeholder, because the engine only checks it for NULL.
 *
 *   - System (RENDER_SYSTEM): the engine writes pixels directly into
 *     surface->pixels and never locks (the original never did either), so the
 *     surface is a plain software surface; presenting uploads it into a
 *     streaming texture and draws it with SDL_RenderCopy. The GPU does any
 *     scaling, so the CPU blitter still does exactly the work it did before.
 *
 * Never both at once: SDL_Renderer and a GL context cannot share a window, so
 * a mode change tears the window down and builds the other kind.
 *
 * Buffers and lifetimes are owned here only. Nothing in src/ allocates a
 * window, context, renderer or texture.
 */
#include "mugen.h"
#ifdef MUGEN_SDL2
#include "sdl2keys.h"
#include "sdl2surface.h"

/* The header aliases SDL_GL_SetAttribute to the shim; inside this file the
 * real SDL2 entry point is the one that has to be called. */
#undef SDL_GL_SetAttribute
#undef SDL_SetVideoMode
#undef SDL_Flip
#undef SDL_UpdateRect

#include <math.h>
/* SDL_syswm.h for Sdl2_GetWindowHandle's SDL_GetWindowWMInfo. The engine's own
 * modules include it separately; it has a proper include guard. */
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

typedef struct Sdl2Video {
    SDL_Window *window;
    SDL_GLContext gl;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Surface *surface;       /* what the engine draws into (System) */
    SDL_Surface *placeholder;   /* what the engine sees in GL modes */
    Uint32 texture_format;
    int texture_w;
    int texture_h;
    int logical_w;
    int logical_h;
    int width;
    int height;
    int bpp;
    int is_gl;
    int swap_interval;          /* -1 = SDL default, set by SDL_GL_SWAP_CONTROL */
    int vsync_applied;
} Sdl2Video;

static Sdl2Video g_sdl2Video;


/* 0 = accepted and ignored, because the SDL2 build has no repeat control. */
static int g_sdl2KeyRepeatDelay;
static int g_sdl2KeyRepeatInterval;
static int g_sdl2UnicodeEnabled;

/* ------------------------------------------------------------------ helpers */

static Uint32 Sdl2_WindowFlags(Uint32 flags)
{
    Uint32 out = SDL_WINDOW_SHOWN;

    if (flags & SDL_OPENGL) {
        out |= SDL_WINDOW_OPENGL;
    }
    if (flags & SDL_FULLSCREEN) {
        out |= SDL_WINDOW_FULLSCREEN;
    }
    if (flags & SDL_RESIZABLE) {
        out |= SDL_WINDOW_RESIZABLE;
    }
    if (flags & SDL_NOFRAME) {
        out |= SDL_WINDOW_BORDERLESS;
    }
    return out;
}

/* Pixel masks and the matching SDL2 texture format for a video depth. The
 * System renderer is RGB565 in practice; 8bpp is refused because SDL2 cannot
 * expose a palettised surface as a renderer texture, and silently returning a
 * surface would present black -- the failure mode the Vita port lost time on. */
static int Sdl2_FormatForDepth(int bpp, Uint32 *rmask, Uint32 *gmask, Uint32 *bmask, Uint32 *amask,
                               Uint32 *format)
{
    switch (bpp) {
    case 16:
        *rmask = 0xF800;
        *gmask = 0x07E0;
        *bmask = 0x001F;
        *amask = 0;
        *format = SDL_PIXELFORMAT_RGB565;
        return 1;
    case 32:
        *rmask = 0x00FF0000;
        *gmask = 0x0000FF00;
        *bmask = 0x000000FF;
        *amask = 0xFF000000;
        *format = SDL_PIXELFORMAT_ARGB8888;
        return 1;
    case 24:
        /* Stored as 32 bits; the engine treats 24 as 32 in several places. */
        *rmask = 0x00FF0000;
        *gmask = 0x0000FF00;
        *bmask = 0x000000FF;
        *amask = 0;
        *format = SDL_PIXELFORMAT_RGB888;
        return 1;
    default:
        SDL_SetError("mugen: %d bpp is not supported by the SDL2 video backend", bpp);
        return 0;
    }
}

static void Sdl2_DestroyVideo(void)
{
    if (g_sdl2Video.texture != NULL) {
        SDL_DestroyTexture(g_sdl2Video.texture);
        g_sdl2Video.texture = NULL;
    }
    if (g_sdl2Video.renderer != NULL) {
        SDL_DestroyRenderer(g_sdl2Video.renderer);
        g_sdl2Video.renderer = NULL;
    }
    if (g_sdl2Video.gl != NULL) {
        SDL_GL_DeleteContext(g_sdl2Video.gl);
        g_sdl2Video.gl = NULL;
    }
    if (g_sdl2Video.window != NULL) {
        SDL_DestroyWindow(g_sdl2Video.window);
        g_sdl2Video.window = NULL;
    }
    if (g_sdl2Video.surface != NULL) {
        SDL_FreeSurface(g_sdl2Video.surface);
        g_sdl2Video.surface = NULL;
    }
    if (g_sdl2Video.placeholder != NULL) {
        SDL_FreeSurface(g_sdl2Video.placeholder);
        g_sdl2Video.placeholder = NULL;
    }
    g_sdl2Video.texture_format = 0;
    g_sdl2Video.vsync_applied = 0;
}

/*
 * Sets vsync and says so when the driver refuses.
 *
 * SDL_GL_SetSwapInterval returns -1 when the request cannot be honoured (no
 * context, or a driver that does not implement it). Reporting it matters
 * because the engine's own VRetrace setting then silently does not take
 * effect, and "the game still runs at the wrong speed" is hard to attribute
 * without this line.
 */
static void Sdl2_ApplySwapInterval(int interval)
{
    int rc = SDL_GL_SetSwapInterval(interval);

    /*
     * Printed on its own line, after the engine's "...success...OK" line is
     * finished: mugen.log is compared line by line against the SDL 1.2
     * baseline, and a message spliced into the middle of the reconstructed
     * "Initializing graphics..." line breaks that comparison for no reason.
     */
    if (rc != 0) {
        Log_Printf("\nvideo: vsync %d refused by the driver (%s)", interval, SDL_GetError());
        return;
    }
    if (SDL_GL_GetSwapInterval() != interval) {
        Log_Printf("\nvideo: vsync %d requested, driver reports %d", interval, SDL_GL_GetSwapInterval());
    }
}

/* -------------------------------------------------------------------- video */

SDL_Surface *Sdl2_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    Uint32 rmask, gmask, bmask, amask, format;
    int is_gl = (flags & SDL_OPENGL) ? 1 : 0;
    int want_w = width > 0 ? width : 1;
    int want_h = height > 0 ? height : 1;

    if (!Sdl2_FormatForDepth(bpp, &rmask, &gmask, &bmask, &amask, &format)) {
        Log_Printf("video: %s\n", SDL_GetError());
        return NULL;
    }
    /*
     * Only a backend change destroys anything. A size change must NOT: the GL
     * renderer keeps its textures and display lists in the context, and the
     * engine reaches this path from Video_OnResize() while a match is running.
     * Recreating the context there invalidated every GL object the renderer
     * still held. Depth is not a reason to rebuild either -- the surface is
     * ours to replace, and a GL context does not care about its colour depth.
     */
    if (g_sdl2Video.window != NULL && g_sdl2Video.is_gl != is_gl) {
        Sdl2_DestroyVideo();
    }
    if (g_sdl2Video.window == NULL) {
        g_sdl2Video.window = SDL_CreateWindow("M.U.G.E.N", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                              want_w, want_h, Sdl2_WindowFlags(flags));
        if (g_sdl2Video.window == NULL) {
            Log_Printf("video: SDL_CreateWindow failed: %s\n", SDL_GetError());
            return NULL;
        }
        g_sdl2Video.width = want_w;
        g_sdl2Video.height = want_h;
        g_sdl2Video.bpp = bpp;
        g_sdl2Video.is_gl = is_gl;
    } else if (g_sdl2Video.window != NULL && (width != g_sdl2Video.width || height != g_sdl2Video.height)) {
        SDL_SetWindowSize(g_sdl2Video.window, want_w, want_h);
        g_sdl2Video.width = want_w;
        g_sdl2Video.height = want_h;
    }
    if (flags & SDL_FULLSCREEN) {
        if (SDL_GetWindowFlags(g_sdl2Video.window) & SDL_WINDOW_FULLSCREEN) {
            /* already fullscreen */
        } else {
            SDL_SetWindowFullscreen(g_sdl2Video.window, SDL_WINDOW_FULLSCREEN);
        }
    } else if (SDL_GetWindowFlags(g_sdl2Video.window) & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(g_sdl2Video.window, 0);
    }

    if (is_gl) {
        if (g_sdl2Video.gl == NULL) {
            g_sdl2Video.gl = SDL_GL_CreateContext(g_sdl2Video.window);
            if (g_sdl2Video.gl == NULL) {
                Log_Printf("video: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
                return NULL;
            }
        }
        /* SDL_GL_SWAP_CONTROL arrives before the context exists, so it is
         * recorded by Sdl2_GL_SetAttribute and applied here. */
        if (!g_sdl2Video.vsync_applied && g_sdl2Video.swap_interval >= 0) {
            Sdl2_ApplySwapInterval(g_sdl2Video.swap_interval);
            g_sdl2Video.vsync_applied = 1;
        }
        if (g_sdl2Video.placeholder == NULL) {
            /* The GL renderer never blits into this; video.c only checks the
             * surface it gets back for NULL. 1x1 keeps it honest. */
            g_sdl2Video.placeholder =
                SDL_CreateRGBSurfaceWithFormat(0, 1, 1, bpp, format);
            if (g_sdl2Video.placeholder == NULL) {
                return NULL;
            }
        }
        return g_sdl2Video.placeholder;
    }

    if (g_sdl2Video.renderer == NULL) {
        g_sdl2Video.renderer = SDL_CreateRenderer(g_sdl2Video.window, -1, SDL_RENDERER_ACCELERATED);
        if (g_sdl2Video.renderer == NULL) {
            g_sdl2Video.renderer = SDL_CreateRenderer(g_sdl2Video.window, -1, 0);
        }
        if (g_sdl2Video.renderer == NULL) {
            Log_Printf("video: SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return NULL;
        }
        /* Logical size: the engine's coordinates stay at the game size whatever
         * the window does, exactly as SDL 1.2's surface size did. */
        SDL_RenderSetLogicalSize(g_sdl2Video.renderer, g_sdl2Video.width, g_sdl2Video.height);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest: pixel art */
        SDL_RenderSetIntegerScale(g_sdl2Video.renderer, SDL_FALSE);
    } else if (g_sdl2Video.logical_w != g_sdl2Video.width || g_sdl2Video.logical_h != g_sdl2Video.height) {
        /* The window changed size, so the logical size has to follow or the
         * new image would be stretched by the old ratio -- with no error. */
        SDL_RenderSetLogicalSize(g_sdl2Video.renderer, g_sdl2Video.width, g_sdl2Video.height);
    }
    g_sdl2Video.logical_w = g_sdl2Video.width;
    g_sdl2Video.logical_h = g_sdl2Video.height;

    if (g_sdl2Video.texture == NULL || g_sdl2Video.texture_format != format) {
        if (g_sdl2Video.texture != NULL) {
            SDL_DestroyTexture(g_sdl2Video.texture);
        }
        g_sdl2Video.texture = SDL_CreateTexture(g_sdl2Video.renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                                g_sdl2Video.width, g_sdl2Video.height);
        if (g_sdl2Video.texture == NULL) {
            Log_Printf("video: SDL_CreateTexture failed: %s\n", SDL_GetError());
            return NULL;
        }
        g_sdl2Video.texture_format = format;
    } else if (g_sdl2Video.texture_w != g_sdl2Video.width || g_sdl2Video.texture_h != g_sdl2Video.height) {
        /* A streaming texture's size is fixed at creation. */
        SDL_Texture *resized = SDL_CreateTexture(g_sdl2Video.renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                                 g_sdl2Video.width, g_sdl2Video.height);

        if (resized != NULL) {
            SDL_DestroyTexture(g_sdl2Video.texture);
            g_sdl2Video.texture = resized;
        }
        /* On failure the old texture is kept: a stale frame beats no window. */
    }
    g_sdl2Video.texture_w = g_sdl2Video.width;
    g_sdl2Video.texture_h = g_sdl2Video.height;

    if (g_sdl2Video.surface == NULL || g_sdl2Video.surface->w != g_sdl2Video.width ||
        g_sdl2Video.surface->h != g_sdl2Video.height ||
        g_sdl2Video.surface->format->BitsPerPixel != bpp) {
        if (g_sdl2Video.surface != NULL) {
            SDL_FreeSurface(g_sdl2Video.surface);
            g_sdl2Video.surface = NULL;
        }
        g_sdl2Video.surface = SDL_CreateRGBSurface(0, g_sdl2Video.width, g_sdl2Video.height, bpp, rmask, gmask,
                                                   bmask, amask);
        if (g_sdl2Video.surface == NULL) {
            Log_Printf("video: SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
            return NULL;
        }
        /* Opaque: the engine writes finished pixels and composites with its own
         * blitters, so upload-time blending would darken the image. */
        SDL_SetSurfaceBlendMode(g_sdl2Video.surface, SDL_BLENDMODE_NONE);
    }
    return g_sdl2Video.surface;
}

/* Uploads the engine's framebuffer into the texture and draws it. Both
 * SDL_UpdateRect and SDL_Flip land here: SDL 1.2 used them to say "now show
 * this", and there is nothing cheaper to do with a streaming texture. */
static void Sdl2_Present(void)
{
    if (g_sdl2Video.is_gl) {
        if (g_sdl2Video.window != NULL) {
            SDL_GL_SwapWindow(g_sdl2Video.window);
        }
        return;
    }
    if (g_sdl2Video.renderer == NULL || g_sdl2Video.texture == NULL || g_sdl2Video.surface == NULL) {
        return;
    }
    if (SDL_UpdateTexture(g_sdl2Video.texture, NULL, g_sdl2Video.surface->pixels,
                          g_sdl2Video.surface->pitch) != 0) {
        return;
    }
    SDL_RenderClear(g_sdl2Video.renderer);
    SDL_RenderCopy(g_sdl2Video.renderer, g_sdl2Video.texture, NULL, NULL);
    SDL_RenderPresent(g_sdl2Video.renderer);
}

void Sdl2_Flip(SDL_Surface *surface)
{
    (void)surface;      /* there is exactly one window; the argument is implicit */
    Sdl2_Present();
}

void Sdl2_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    Sdl2_Present();
}

void Sdl2_VideoQuit(void)
{

    Sdl2_DestroyVideo();
}

void Sdl2_SetCaption(const char *title, const char *icon)
{
    (void)icon;
    if (g_sdl2Video.window != NULL && title != NULL) {
        SDL_SetWindowTitle(g_sdl2Video.window, title);
    }
}

int Sdl2_ToggleFullScreen(SDL_Surface *screen)
{
    Uint32 flags;

    (void)screen;
    if (g_sdl2Video.window == NULL) {
        return 0;
    }
    flags = SDL_GetWindowFlags(g_sdl2Video.window);
    if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
        if (SDL_SetWindowFullscreen(g_sdl2Video.window, SDL_WINDOW_FULLSCREEN) != 0) {
            return 0;
        }
    } else if (SDL_SetWindowFullscreen(g_sdl2Video.window, 0) != 0) {
        return 0;
    }
    return 1;
}

int Sdl2_GL_SetAttribute(SDL_GLattr attr, int value)
{
    if (attr == SDL_GL_SWAP_CONTROL) {
        g_sdl2Video.swap_interval = value;
        g_sdl2Video.vsync_applied = 0;
        /*
         * SDL 1.2 let this be set before the surface existed. SDL2 has no such
         * attribute -- vsync is a property of a live context -- so the value is
         * recorded here and applied at context creation. When a context already
         * exists it must be applied immediately: the vretrace toggle is the
         * in-match Ctrl+V debug key, and deferring it to the next video mode
         * change meant the setting did nothing until the mode was re-set.
         */
        if (g_sdl2Video.gl != NULL) {
            Sdl2_ApplySwapInterval(value != 0);
            g_sdl2Video.vsync_applied = 1;
        }
        return 0;
    }
    return SDL_GL_SetAttribute(attr, value);
}

void Sdl2_GL_SwapBuffers(void)
{
    if (g_sdl2Video.window != NULL) {
        SDL_GL_SwapWindow(g_sdl2Video.window);
    }
}

void *Sdl2_GetWindowHandle(void)
{
#ifdef _WIN32
    SDL_SysWMinfo info;

    if (g_sdl2Video.window == NULL) {
        return NULL;
    }
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_sdl2Video.window, &info)) {
        return NULL;
    }
    return (void *)info.info.win.window;
#else
    return (void *)g_sdl2Video.window;
#endif
}

/* -------------------------------------------------------------------- misc */

char *Sdl2_AudioDriverName(char *namebuf, int maxlen)
{
    const char *name = SDL_GetCurrentAudioDriver();

    if (namebuf == NULL || maxlen <= 0) {
        return NULL;
    }
    if (name == NULL) {
        name = "";
    }
    SDL_strlcpy(namebuf, name, (size_t)maxlen);
    return namebuf;
}

int Sdl2_EnableKeyRepeat(int delay, int interval)
{
    /* SDL2 removed key repeat control: repeat is a platform setting and
     * SDL_KEYDOWN arrives already repeated. The reconstructed calls (console
     * on, console off, keyboard init) are recorded rather than dropped so the
     * intent stays visible. */
    g_sdl2KeyRepeatDelay = delay;
    g_sdl2KeyRepeatInterval = interval;
    return 0;
}

int Sdl2_EnableUNICODE(int enable)
{
    int previous = g_sdl2UnicodeEnabled;

    g_sdl2UnicodeEnabled = enable;
    /* SDL2 always delivers text through SDL_TEXTINPUT; events.c decodes it
     * into the engine's unicode field. */
    return previous;
}

/*
 * SDL2 keycode -> the engine's key identifier (SDL 1.2 numbering).
 *
 * Why a table: SDL 1.2 and SDL2 do not share a number for any non-printable
 * key, and the difference is not a constant offset. SDL 1.2 called the up
 * arrow SDLK_UP == 273; SDL2 calls it scancode|SDLK_SCANCODE_MASK, and its
 * scancode for the same key is 82. The engine indexes g_keyState, g_keyMap and
 * g_inputState with the SDL 1.2 numbers -- they are also what mugen.cfg
 * contains -- so the conversion has to be name-exact.
 *
 * Printable ASCII is the same in both (SDLK_a == 'a', SDLK_RETURN == '\r',
 * SDLK_SPACE == ' ', SDLK_ESCAPE == 0x1B), so those are passed through and are
 * not listed. Everything else is below. A key SDL2 has and SDL 1.2 did not
 * (LGUI, PRINTSCREEN, KP_CLEAR, ...) returns 0, which every consumer already
 * treats as "ignore".
 *
 * Like SDL 1.2's own SDL_EnableUNICODE translation, the result is not
 * remembered: a key pressed under one layout and released under another can
 * leave its state set. The engine already tolerates that (Keyboard_Init clears
 * the array, and Android's port has the same property).
 */
/*
 * Sdl2_EngineKey and Sdl2_SdlKeyForEngineKey live in src/sdl2keys.c so SDL2
 * consumers without this file's window/present implementation (Android, Vita
 * SDL2) can share the exact same tables. This file includes that module's
 * header; the functions below (Sdl2_KeyName) call through to it.
 */
/* The engine key's name, as the console prints bindings (title.c formats the
 * list with SDL_GetKeyName). SDL2's own names are for SDL2 keycodes, which the
 * engine no longer carries, so translate first and name the SDL 1.2 value. */
const char *Sdl2_KeyName(int engine_key)
{
    /*
     * engine_key is already in the engine's numbering, so it has to go through
     * the *inverse* to become an SDL2 keycode. Running the forward translation
     * here would be backwards: Sdl2_EngineKey expects an SDL2 keycode, so 273
     * (SDL 1.2's Up) would be looked up as if it were an SDL2 value, miss the
     * table, and return 0 -- SDL_GetKeyName(0) is "" -- so every arrow, F-key
     * and modifier would read as an empty name in the binding menu.
     */
    SDL_Keycode sdl_key = Sdl2_SdlKeyForEngineKey(engine_key);

    if (sdl_key == SDLK_UNKNOWN) {
        return "";
    }
    return SDL_GetKeyName(sdl_key);
}

/*
 * Nearest-neighbour scaling lives in src/sdl2surface.c so SDL2 consumers
 * without this file's window/present implementation (Android, Vita SDL2)
 * scale identically (PLAN Android B4). This file keeps the video/GL/audio
 * shim; the sampler is shared.
 */
#endif /* MUGEN_SDL2 */
