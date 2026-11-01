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

/* The header aliases SDL_GL_SetAttribute to the shim; inside this file the
 * real SDL2 entry point is the one that has to be called. */
#undef SDL_GL_SetAttribute
#undef SDL_SetVideoMode
#undef SDL_Flip
#undef SDL_UpdateRect

#include <math.h>
/* SDL_syswm.h for Sdl2_GetWindowHandle's SDL_GetWindowWMInfo. The engine's own
 * modules include it separately; it has a proper include guard. */
#include <SDL_syswm.h>

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
int Sdl2_EngineKey(int sdl_keycode)
{
    /* SDL 1.2's numbering for everything below 256; ASCII is identical. */
    if (sdl_keycode >= 0 && sdl_keycode < 256) {
        return sdl_keycode;
    }
    switch (sdl_keycode) {
    /*
     * SDLK_PAUSE is the one non-ASCII key SDL 1.2 numbered below 256 (19, the
     * PC pause scancode), so the range check above does not cover it. The
     * engine treats it specially: KEY_PAUSE_TOGGLE in keyboard.c toggles
     * g_pauseToggled, and gameloop.c tests it. SDL2 moved it above the mask.
     */
    case SDLK_PAUSE: return 19;         /* SDL 1.2 SDLK_PAUSE */
    /*
     * Keypad. SDL 1.2 numbered the digits 256..265 and the operators
     * 266..272; SDL2 spells all of them scancode|SDLK_SCANCODE_MASK. These
     * must match Sdl2_SdlKeyForEngineKey exactly, or a key can be pressed but
     * not released, and focus resync disagrees with event delivery.
     */
    case SDLK_KP_0: return 256;         /* SDL 1.2 SDLK_KP0 */
    case SDLK_KP_1: return 257;
    case SDLK_KP_2: return 258;
    case SDLK_KP_3: return 259;
    case SDLK_KP_4: return 260;
    case SDLK_KP_5: return 261;
    case SDLK_KP_6: return 262;
    case SDLK_KP_7: return 263;
    case SDLK_KP_8: return 264;
    case SDLK_KP_9: return 265;
    case SDLK_KP_PERIOD: return 266;   /* SDL 1.2 SDLK_KP_PERIOD */
    case SDLK_KP_DIVIDE: return 267;   /* SDL 1.2 SDLK_KP_DIVIDE */
    case SDLK_KP_MULTIPLY: return 268;   /* SDL 1.2 SDLK_KP_MULTIPLY */
    case SDLK_KP_MINUS: return 269;   /* SDL 1.2 SDLK_KP_MINUS */
    case SDLK_KP_PLUS: return 270;   /* SDL 1.2 SDLK_KP_PLUS */
    case SDLK_KP_ENTER: return 271;   /* SDL 1.2 SDLK_KP_ENTER */
    case SDLK_KP_EQUALS: return 272;   /* SDL 1.2 SDLK_KP_EQUALS */
    case SDLK_NUMLOCKCLEAR: return 300;   /* SDL 1.2 SDLK_NUMLOCK */
    case SDLK_CAPSLOCK: return 301;       /* SDL 1.2 SDLK_CAPSLOCK */
    case SDLK_SCROLLLOCK: return 302;     /* SDL 1.2 SDLK_SCROLLOCK */
    case SDLK_UP: return 273;   /* SDL 1.2 SDLK_UP */
    case SDLK_DOWN: return 274;   /* SDL 1.2 SDLK_DOWN */
    case SDLK_RIGHT: return 275;   /* SDL 1.2 SDLK_RIGHT */
    case SDLK_LEFT: return 276;   /* SDL 1.2 SDLK_LEFT */
    case SDLK_INSERT: return 277;   /* SDL 1.2 SDLK_INSERT */
    case SDLK_HOME: return 278;   /* SDL 1.2 SDLK_HOME */
    case SDLK_END: return 279;   /* SDL 1.2 SDLK_END */
    case SDLK_PAGEUP: return 280;   /* SDL 1.2 SDLK_PAGEUP */
    case SDLK_PAGEDOWN: return 281;   /* SDL 1.2 SDLK_PAGEDOWN */
    case SDLK_F1: return 282;   /* SDL 1.2 SDLK_F1 */
    case SDLK_F2: return 283;   /* SDL 1.2 SDLK_F2 */
    case SDLK_F3: return 284;   /* SDL 1.2 SDLK_F3 */
    case SDLK_F4: return 285;   /* SDL 1.2 SDLK_F4 */
    case SDLK_F5: return 286;   /* SDL 1.2 SDLK_F5 */
    case SDLK_F6: return 287;   /* SDL 1.2 SDLK_F6 */
    case SDLK_F7: return 288;   /* SDL 1.2 SDLK_F7 */
    case SDLK_F8: return 289;   /* SDL 1.2 SDLK_F8 */
    case SDLK_F9: return 290;   /* SDL 1.2 SDLK_F9 */
    case SDLK_F10: return 291;   /* SDL 1.2 SDLK_F10 */
    case SDLK_F11: return 292;   /* SDL 1.2 SDLK_F11 */
    case SDLK_F12: return 293;   /* SDL 1.2 SDLK_F12 */
    case SDLK_F13: return 294;   /* SDL 1.2 SDLK_F13 */
    case SDLK_F14: return 295;   /* SDL 1.2 SDLK_F14 */
    case SDLK_F15: return 296;   /* SDL 1.2 SDLK_F15 */
    case SDLK_RSHIFT: return 303;   /* SDL 1.2 SDLK_RSHIFT */
    case SDLK_LSHIFT: return 304;   /* SDL 1.2 SDLK_LSHIFT */
    case SDLK_RCTRL: return 305;   /* SDL 1.2 SDLK_RCTRL */
    case SDLK_LCTRL: return 306;   /* SDL 1.2 SDLK_LCTRL */
    case SDLK_RALT: return 307;   /* SDL 1.2 SDLK_RALT */
    case SDLK_LALT: return 308;   /* SDL 1.2 SDLK_LALT */
    case SDLK_SYSREQ: return 317;   /* SDL 1.2 SDLK_SYSREQ */
    default:
        return 0;
    }
}

/*
 * The inverse of Sdl2_EngineKey.
 *
 * Each non-printable engine key has exactly one SDL2 keycode that translates to
 * it, so a reverse switch is exact. 1..255 are their own SDL2 keycode. The
 * lookup exists so "is engine key N held?" can be answered from SDL2's
 * scancode-indexed key state without guessing at a numbering scheme.
 */
SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key)
{
    if (engine_key > 0 && engine_key < 256) {
        /* SDLK_PAUSE is the exception: nineteen in SDL 1.2, but SDL2 spells it
         * scancode|SDLK_SCANCODE_MASK like the rest of the non-ASCII keys. */
        if (engine_key == 19) {
            return SDLK_PAUSE;
        }
        return (SDL_Keycode)engine_key;
    }
    switch (engine_key) {
    case 256: return SDLK_KP_0;
    case 257: return SDLK_KP_1;
    case 258: return SDLK_KP_2;
    case 259: return SDLK_KP_3;
    case 260: return SDLK_KP_4;
    case 261: return SDLK_KP_5;
    case 262: return SDLK_KP_6;
    case 263: return SDLK_KP_7;
    case 264: return SDLK_KP_8;
    case 265: return SDLK_KP_9;
    case 266: return SDLK_KP_PERIOD;
    case 267: return SDLK_KP_DIVIDE;
    case 268: return SDLK_KP_MULTIPLY;
    case 269: return SDLK_KP_MINUS;
    case 270: return SDLK_KP_PLUS;
    case 271: return SDLK_KP_ENTER;
    case 272: return SDLK_KP_EQUALS;
    case 273: return SDLK_UP;
    case 274: return SDLK_DOWN;
    case 275: return SDLK_RIGHT;
    case 276: return SDLK_LEFT;
    case 277: return SDLK_INSERT;
    case 278: return SDLK_HOME;
    case 279: return SDLK_END;
    case 280: return SDLK_PAGEUP;
    case 281: return SDLK_PAGEDOWN;
    case 282: return SDLK_F1;
    case 283: return SDLK_F2;
    case 284: return SDLK_F3;
    case 285: return SDLK_F4;
    case 286: return SDLK_F5;
    case 287: return SDLK_F6;
    case 288: return SDLK_F7;
    case 289: return SDLK_F8;
    case 290: return SDLK_F9;
    case 291: return SDLK_F10;
    case 292: return SDLK_F11;
    case 293: return SDLK_F12;
    case 294: return SDLK_F13;
    case 295: return SDLK_F14;
    case 296: return SDLK_F15;
    case 300: return SDLK_NUMLOCKCLEAR;
    case 301: return SDLK_CAPSLOCK;
    case 302: return SDLK_SCROLLLOCK;
    case 303: return SDLK_RSHIFT;
    case 304: return SDLK_LSHIFT;
    case 305: return SDLK_RCTRL;
    case 306: return SDLK_LCTRL;
    case 307: return SDLK_RALT;
    case 308: return SDLK_LALT;
    case 317: return SDLK_SYSREQ;
    default: return SDLK_UNKNOWN;
    }
}

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

/* --------------------------------------------------------------- zoomSurface */

/* Palette, colour key and modulation of src onto the fresh surface dst, which
 * zoomSurface must reproduce: SDL 1.2's SDL_gfx did, and the engine's callers
 * free the result without further fix-up. */
static void Sdl2_CopySurfaceState(SDL_Surface *src, SDL_Surface *dst)
{
    Uint32 key = 0;
    Uint8 r = 0, g = 0, b = 0, a = 255;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;

    if (src->format->palette != NULL && dst->format->palette != NULL) {
        int ncolors = src->format->palette->ncolors;
        SDL_Color black[256];
        int pad;
        int i;

        if (ncolors > dst->format->palette->ncolors) {
            ncolors = dst->format->palette->ncolors;
        }
        SDL_SetPaletteColors(dst->format->palette, src->format->palette->colors, 0, ncolors);
        /*
         * SDL2 gives a new surface a white-to-black ramp in the entries the
         * source did not set. SDL_gfx zeroed them instead, and the differential
         * test sees the difference. The engine's palette handling does not
         * guarantee it never reads an unset index, so matching the old library
         * is the safe choice.
         *
         * The tail starts at the last entry the *source* actually set, not at
         * ncolors: SDL2 allocates every 8-bit surface with a full 256-entry
         * palette, so ncolors is always 256 and never marks the real end.
         */
        memset(black, 0, sizeof(black));
        /*
         * SDL2 allocates every 8-bit surface with a full 256-entry palette and
         * leaves the unset entries at its default (255,255,255,255), so there is
         * no "ncolors" that marks the real end of the source's palette. The
         * first all-white entry is where SDL stopped writing; everything from
         * there on is the default and SDL_gfx had it zeroed.
         */
        pad = ncolors;
        for (i = 0; i < ncolors; i++) {
            const SDL_Color *c = &src->format->palette->colors[i];

            if (c->r == 255 && c->g == 255 && c->b == 255 && c->a == 255) {
                pad = i;
                break;
            }
        }
        if (pad < dst->format->palette->ncolors) {
            SDL_SetPaletteColors(dst->format->palette, black, pad, dst->format->palette->ncolors - pad);
        }
    }
    if (SDL_GetColorKey(src, &key) == 0) {
        SDL_SetColorKey(dst, SDL_TRUE, key);
    }
    SDL_GetSurfaceColorMod(src, &r, &g, &b);
    SDL_GetSurfaceAlphaMod(src, &a);
    SDL_GetSurfaceBlendMode(src, &blend);
    SDL_SetSurfaceColorMod(dst, r, g, b);
    /*
     * The destination's own modulation is part of the result SDL_gfx produced,
     * but it must not be *applied* during the scale: the intermediate surface
     * needs the source's final pixels. Applying it here is what the blit would
     * do on the way in, and it darkened every colour channel by one in the
     * differential test -- the intermediate surface then carried
     * (colour * alpha / 255) and the engine's own blit darkened it again.
     *
     * So the modulation is recorded as opaque/neutral for the blit and the
     * destination is left with the source's state once the pixels are in.
     */
    SDL_SetSurfaceAlphaMod(dst, 255);
    SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_NONE);
    (void)a;
    (void)blend;
}

/*
 * Nearest-neighbour scaling, pixel by pixel.
 *
 * SDL_BlitScaled is not usable here, for two separate reasons measured by
 * tools/test_zoomdiff.sh:
 *   - it refuses 8-bit to 8-bit outright ("Blit combination not supported"),
 *     which is exactly TTF_RenderUTF8_Solid's output;
 *   - for the formats it does accept it applies the source's alpha modulation
 *     while copying, so a source pixel of 0xFE020304 arrives as 0xFE010203.
 *     SDL_gfx copies the pixels through untouched, and the engine's own blit
 *     applies the modulation afterwards -- applying it twice darkened every
 *     scaled glyph.
 *
 * Copying the source pixels directly reproduces SDL_gfx's non-smooth path,
 * which is what both engine call sites ask for (smooth = 0).
 */
static SDL_Surface *Sdl2_ZoomNearest(SDL_Surface *src, int dw, int dh)
{
    SDL_Surface *dst;
    int x, y;
    int spitch, dpitch, bpp;

    dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, src->format->BitsPerPixel, src->format->format);
    if (dst == NULL) {
        return NULL;
    }
    Sdl2_CopySurfaceState(src, dst);
    if (SDL_LockSurface(src) != 0) {
        SDL_FreeSurface(dst);
        return NULL;
    }
    if (SDL_LockSurface(dst) != 0) {
        SDL_UnlockSurface(src);
        SDL_FreeSurface(dst);
        return NULL;
    }
    bpp = src->format->BytesPerPixel;
    spitch = src->pitch;
    dpitch = dst->pitch;
    for (y = 0; y < dh; y++) {
        int sy = (int)((long long)y * src->h / dh);

        if (sy >= src->h) {
            sy = src->h - 1;
        }
        for (x = 0; x < dw; x++) {
            int sx = (int)((long long)x * src->w / dw);

            if (sx >= src->w) {
                sx = src->w - 1;
            }
            memcpy((Uint8 *)dst->pixels + y * dpitch + x * bpp,
                   (const Uint8 *)src->pixels + sy * spitch + sx * bpp, (size_t)bpp);
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    return dst;
}

SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)
{
    int dw, dh;
    SDL_Surface *dst;
    Uint32 format;

    if (src == NULL) {
        return NULL;
    }
    if (!(zoomx > 0.0) || !(zoomy > 0.0)) {
        SDL_SetError("zoomSurface: zoom factors must be positive");
        return NULL;
    }
    /*
     * SDL_gfx rounds the destination size to nearest, it does not truncate.
     * Measured against the kit's libSDL_gfx.a:
     *   zoomSurfaceSize(13, 7, 1.5, 2.5) = 20x18   (19.5, 17.5)
     *   zoomSurfaceSize(17, 1, 1.0, 0.5) = 17x1
     *   zoomSurfaceSize(16, 16, 0.5, 0.5) = 8x8
     * Truncating cost a row and a column on every fractional scale, which is
     * exactly the case the engine hits when a font is drawn at a non-integer
     * scale (src/font.c's zoomSurface call).
     */
    dw = (int)((double)src->w * zoomx + 0.5);
    dh = (int)((double)src->h * zoomy + 0.5);
    if (dw <= 0 || dh <= 0) {
        SDL_SetError("zoomSurface: resulting size is empty");
        return NULL;
    }

    /*
     * Indexed sources get their own path, and not for the palette alone:
     * SDL2's blitter cannot scale 8-bit to 8-bit at all -- SDL_BlitScaled
     * returns -1 "Blit combination not supported" -- so the generic path below
     * does not merely lose the colours, it fails outright. That matters because
     * TTF_RenderUTF8_Solid returns an 8-bit palettised surface, and font.c
     * scales it.
     */
    if (src->format->BitsPerPixel == 8) {
        if (smooth != 0) {
            SDL_SetError("zoomSurface: smooth scaling of 8-bit surfaces is not supported");
            return NULL;
        }
        return Sdl2_ZoomNearest(src, dw, dh);
    }

    /*
     * SDL_gfx promotes 16-bit sources to 32-bit output (measured: an RGB565
     * 8x8 comes back as 32bpp). Matching that keeps the engine's blit path
     * identical to the SDL 1.2 build instead of introducing a new surface
     * format halfway through text drawing.
     *
     * Everything goes through Sdl2_ZoomNearest rather than SDL_BlitScaled: see
     * the comment there. For the 16-bit case the source is converted first, so
     * the copy loop still works on like formats.
     */
    /* BitsPerPixel is 15 or 16 for the two 16-bit formats (RGB555 and RGB565),
     * so test the byte width instead of guessing at the bit count. */
    if (src->format->BytesPerPixel == 2) {
        SDL_Surface *promoted = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);

        if (promoted == NULL) {
            return NULL;
        }
        dst = Sdl2_ZoomNearest(promoted, dw, dh);
        SDL_FreeSurface(promoted);
        return dst;
    }
    return Sdl2_ZoomNearest(src, dw, dh);
}

#endif /* MUGEN_SDL2 */
