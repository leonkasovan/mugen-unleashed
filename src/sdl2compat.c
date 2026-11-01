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
            SDL_GL_SetSwapInterval(g_sdl2Video.swap_interval);
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
            SDL_GL_SetSwapInterval(value != 0);
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
    case SDLK_KP_PERIOD: return 266;   /* SDL 1.2 SDLK_KP_PERIOD */
    case SDLK_KP_DIVIDE: return 267;   /* SDL 1.2 SDLK_KP_DIVIDE */
    case SDLK_KP_MULTIPLY: return 268;   /* SDL 1.2 SDLK_KP_MULTIPLY */
    case SDLK_KP_MINUS: return 269;   /* SDL 1.2 SDLK_KP_MINUS */
    case SDLK_KP_PLUS: return 270;   /* SDL 1.2 SDLK_KP_PLUS */
    case SDLK_KP_ENTER: return 271;   /* SDL 1.2 SDLK_KP_ENTER */
    case SDLK_KP_EQUALS: return 272;   /* SDL 1.2 SDLK_KP_EQUALS */
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
    case SDLK_CAPSLOCK: return 301;   /* SDL 1.2 SDLK_CAPSLOCK */
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
    return SDL_GetKeyName((SDL_Keycode)Sdl2_EngineKey(engine_key));
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

        if (ncolors > dst->format->palette->ncolors) {
            ncolors = dst->format->palette->ncolors;
        }
        SDL_SetPaletteColors(dst->format->palette, src->format->palette->colors, 0, ncolors);
    }
    if (SDL_GetColorKey(src, &key) == 0) {
        SDL_SetColorKey(dst, SDL_TRUE, key);
    }
    SDL_GetSurfaceColorMod(src, &r, &g, &b);
    SDL_GetSurfaceAlphaMod(src, &a);
    SDL_GetSurfaceBlendMode(src, &blend);
    SDL_SetSurfaceColorMod(dst, r, g, b);
    SDL_SetSurfaceAlphaMod(dst, a);
    SDL_SetSurfaceBlendMode(dst, blend);
}

/* SDL_BlitScaled with the scale-quality hint set for the duration. Both engine
 * call sites pass smooth = 0, which is nearest neighbour -- what SDL_gfx's
 * non-smooth path did and what pixel art needs. */
static int Sdl2_BlitScaledWithHint(SDL_Surface *src, SDL_Surface *dst, int smooth)
{
    const char *quality = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    char saved[8];
    int rc;

    saved[0] = '\0';
    if (quality != NULL) {
        strncpy(saved, quality, sizeof(saved) - 1);
        saved[sizeof(saved) - 1] = '\0';
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, smooth ? "1" : "0");
    rc = SDL_BlitScaled(src, NULL, dst, NULL);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, saved[0] != '\0' ? saved : "0");
    return rc;
}

/*
 * Indexed (8-bit) scaling, pixel by pixel.
 *
 * Not a blitter: SDL2 refuses 8->8 scaling. Nearest neighbour is also exactly
 * what SDL 1.2's SDL_gfx did here, so copying source indices straight through
 * reproduces the old output rather than approximating it. The destination is
 * built with Sdl2_CopySurfaceState, so index 0 stays the colour key and the
 * text colour stays wherever TTF_RenderUTF8_Solid put it.
 */
static SDL_Surface *Sdl2_ZoomIndexed(SDL_Surface *src, int dw, int dh, int smooth)
{
    SDL_Surface *dst;
    int x, y;
    const Uint8 *spix;
    Uint8 *dpix;

    if (smooth != 0) {
        SDL_SetError("zoomSurface: smooth scaling of 8-bit surfaces is not supported");
        return NULL;
    }
    dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 8, src->format->format);
    if (dst == NULL || dst->format->palette == NULL) {
        if (dst != NULL) {
            SDL_FreeSurface(dst);
        }
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
    spix = (const Uint8 *)src->pixels;
    dpix = (Uint8 *)dst->pixels;
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
            dpix[y * dst->pitch + x] = spix[sy * src->pitch + sx];
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    return dst;
}


/*
 * SDL_gfx's zoomSurface over SDL2's blitter.
 *
 * Contract kept from SDL_gfx: the destination is
 * (int)(src->w * zoomx) x (int)(src->h * zoomy) -- truncated, not rounded --
 * with the source's format, colour key, alpha modulation and blend mode, and
 * the result is freed with SDL_FreeSurface. smooth != 0 sets the scale-quality
 * hint, so callers that ask for smoothing get it; both engine call sites pass
 * 0, which the hint maps to nearest neighbour.
 *
 * This is a behavioural substitute, not the original library: SDL_gfx used its
 * own nearest-neighbour zoomsurface with per-pixel alpha handling, and
 * SDL_BlitScaled goes through SDL's blitters. SDL2_PLAN.md requires a
 * differential image test (indexed, 16-bit, 32-bit, colour keys, alpha, odd
 * sizes) before this counts as pixel-equivalent. Until then the risk is
 * confined to scaled TrueType text (src/font.c) and the Vita-only fallback.
 */
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)
{
    int dw, dh;
    SDL_Surface *dst;

    if (src == NULL) {
        return NULL;
    }
    if (!(zoomx > 0.0) || !(zoomy > 0.0)) {
        SDL_SetError("zoomSurface: zoom factors must be positive");
        return NULL;
    }
    dw = (int)((double)src->w * zoomx);
    dh = (int)((double)src->h * zoomy);
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
        return Sdl2_ZoomIndexed(src, dw, dh, smooth);
    }

    dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, src->format->BitsPerPixel, src->format->format);
    if (dst == NULL) {
        return NULL;
    }
    Sdl2_CopySurfaceState(src, dst);
    if (Sdl2_BlitScaledWithHint(src, dst, smooth) != 0) {
        SDL_FreeSurface(dst);
        return NULL;
    }
    return dst;
}

#endif /* MUGEN_SDL2 */
