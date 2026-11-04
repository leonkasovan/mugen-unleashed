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
 *     is a placeholder the engine never draws into, but it must carry the real
 *     window size: the OpenGLScreen presenter reads its dimensions to size the
 *     screen quad.
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
    /*
     * Size of the storage pair the texture must match -- the surface the
     * engine actually draws into. Not the window size: after a refused resize
     * the window has already moved while the old pair survives.
     */
    int storage_w;
    int storage_h;
    int logical_w;
    int logical_h;
    int width;
    int height;
    int bpp;
    int is_gl;
    int swap_interval;          /* -1 = SDL default, set by SDL_GL_SWAP_CONTROL */
    int vsync_applied;
    int display_dirty;          /* frame-end flush pending (M3 frame boundary) */
} Sdl2Video;

static Sdl2Video g_sdl2Video;

#ifdef MUGEN_SDL2_DIAG
/*
 * Diagnostic counters for the M0/M3 regression harness (RENDERING_PLAN.md
 * section 4.5): linked only into test builds via -DMUGEN_SDL2_DIAG. They
 * count operations without changing renderer semantics or logging per
 * pixel on normal builds. Sdl2_DiagPresent counts Sdl2_Present calls that
 * reached an upload (not early returns); Sdl2_DiagTextureCreate/Destroy
 * track texture lifetime to catch unbalanced create/destroy.
 */
static int g_sdl2DiagPresent;
static int g_sdl2DiagTextureCreate;
static int g_sdl2DiagTextureDestroy;

void Sdl2_DiagReset(void)
{
    g_sdl2DiagPresent = 0;
    g_sdl2DiagTextureCreate = 0;
    g_sdl2DiagTextureDestroy = 0;
}

void Sdl2_DiagCounters(int *present, int *tex_create, int *tex_destroy)
{
    if (present != NULL) {
        *present = g_sdl2DiagPresent;
    }
    if (tex_create != NULL) {
        *tex_create = g_sdl2DiagTextureCreate;
    }
    if (tex_destroy != NULL) {
        *tex_destroy = g_sdl2DiagTextureDestroy;
    }
}

/* Independent check for tests (plan section 5): the live texture's actual
 * format and size from SDL_QueryTexture, never the shim's own metadata. */
int Sdl2_DiagTextureSize(int *w, int *h, Uint32 *format)
{
    Uint32 fmt = 0;
    int access = 0, tw = 0, th = 0;

    if (g_sdl2Video.texture == NULL || SDL_QueryTexture(g_sdl2Video.texture, &fmt, &access, &tw, &th) != 0) {
        return 0;
    }
    if (w != NULL) {
        *w = tw;
    }
    if (h != NULL) {
        *h = th;
    }
    if (format != NULL) {
        *format = fmt;
    }
    return 1;
}

/* Fault injection for tests (plan section 4.4): make the next texture or
 * surface creation fail, without GNU --wrap, fake headers or body copies.
 * The wrappers below are the allocation ops the production code uses in
 * DIAG builds; the test arms them. which: 1 = texture, 2 = surface, 3 =
 * format surface (the OpenGLScreen placeholder). */
static int g_sdl2DiagFailTexture;
static int g_sdl2DiagFailSurface;
static int g_sdl2DiagFailFormatSurface;

void Sdl2_DiagFailNext(int which)
{
    if (which == 1) {
        g_sdl2DiagFailTexture = 1;
    } else if (which == 2) {
        g_sdl2DiagFailSurface = 1;
    } else if (which == 3) {
        g_sdl2DiagFailFormatSurface = 1;
    }
}

static SDL_Texture *Sdl2_DiagCreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h)
{
    if (g_sdl2DiagFailTexture) {
        g_sdl2DiagFailTexture = 0;
        SDL_SetError("injected texture failure");
        return NULL;
    }
    return SDL_CreateTexture(r, format, access, w, h);
}

static SDL_Surface *Sdl2_DiagCreateSurface(Uint32 flags, int w, int h, int bpp, Uint32 rmask, Uint32 gmask,
                                             Uint32 bmask, Uint32 amask)
{
    if (g_sdl2DiagFailSurface) {
        g_sdl2DiagFailSurface = 0;
        SDL_SetError("injected surface failure");
        return NULL;
    }
    return SDL_CreateRGBSurface(flags, w, h, bpp, rmask, gmask, bmask, amask);
}
static SDL_Surface *Sdl2_DiagCreateSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format)
{
    if (g_sdl2DiagFailFormatSurface) {
        g_sdl2DiagFailFormatSurface = 0;
        SDL_SetError("injected format-surface failure");
        return NULL;
    }
    return SDL_CreateRGBSurfaceWithFormat(flags, w, h, depth, format);
}
#define SDL_CreateTexture Sdl2_DiagCreateTexture
#define SDL_CreateRGBSurface Sdl2_DiagCreateSurface
#define SDL_CreateRGBSurfaceWithFormat Sdl2_DiagCreateSurfaceWithFormat
#endif

/* SDL_CreateTexture/SDL_CreateRGBSurface/SDL_CreateRGBSurfaceWithFormat
 * resolve to the DIAG wrappers above in test builds; production builds call
 * SDL directly. */


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

/*
 * Creates the renderer and the streaming texture the System backend presents
 * through, if they are not there yet. Split out of Sdl2_SetVideoMode so
 * Sdl2_ResetRenderer() can rebuild them after SDL invalidates the device: the
 * sizes and the pixel format come from the state Sdl2_SetVideoMode already
 * recorded, so no caller has to repeat that arithmetic. Returns 0 on success,
 * -1 when the renderer or the texture cannot be created.
 */
static int Sdl2_CreateRendererAndTexture(void)
{
    if (g_sdl2Video.renderer == NULL) {
        g_sdl2Video.renderer = SDL_CreateRenderer(g_sdl2Video.window, -1, SDL_RENDERER_ACCELERATED);
        if (g_sdl2Video.renderer == NULL) {
            g_sdl2Video.renderer = SDL_CreateRenderer(g_sdl2Video.window, -1, 0);
        }
        if (g_sdl2Video.renderer == NULL) {
            return -1;
        }
        /* Logical size: the engine's coordinates stay at the game size whatever
         * the window does, exactly as SDL 1.2's surface size did. */
        SDL_RenderSetLogicalSize(g_sdl2Video.renderer, g_sdl2Video.width, g_sdl2Video.height);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest: pixel art */
        SDL_RenderSetIntegerScale(g_sdl2Video.renderer, SDL_FALSE);
        g_sdl2Video.logical_w = g_sdl2Video.width;
        g_sdl2Video.logical_h = g_sdl2Video.height;
    }
    if (g_sdl2Video.texture == NULL) {
        if (g_sdl2Video.texture_format == 0 || g_sdl2Video.storage_w <= 0 ||
            g_sdl2Video.storage_h <= 0) {
            return -1;      /* nothing was ever set up to rebuild from */
        }
        /*
         * Fix (port change): the texture is rebuilt at the size of the storage
         * that survived, not the window's. When a resize failed its allocation
         * the old surface stayed (say 640x480) while g_sdl2Video.width/height
         * had already taken the requested 800x600; a device reset then made an
         * 800x600 texture for a 640x480 surface and the next upload read past
         * the buffer. So the rebuild size is the pair's, not the window's.
         */
        g_sdl2Video.texture = SDL_CreateTexture(g_sdl2Video.renderer, g_sdl2Video.texture_format,
                                                SDL_TEXTUREACCESS_STREAMING, g_sdl2Video.storage_w,
                                                g_sdl2Video.storage_h);
        if (g_sdl2Video.texture == NULL) {
            return -1;
        }
#ifdef MUGEN_SDL2_DIAG
        g_sdl2DiagTextureCreate++;
#endif
        g_sdl2Video.texture_w = g_sdl2Video.storage_w;
        g_sdl2Video.texture_h = g_sdl2Video.storage_h;
    }
    return 0;
}

SDL_Surface *Sdl2_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    Uint32 rmask, gmask, bmask, amask, format;
    int is_gl = (flags & SDL_OPENGL) ? 1 : 0;
    /* M1 step 2 (plan section 5.2): validate before mutating anything. A
     * minimized/zero-size request must not become a 1x1 game buffer -- the
     * caller keeps its storage and the window keeps its size; Video_OnResize
     * already returns early for these, this is the second line of defence. */
    int want_w = width;
    int want_h = height;

    if (!Sdl2_FormatForDepth(bpp, &rmask, &gmask, &bmask, &amask, &format)) {
        Log_Printf("video: %s\n", SDL_GetError());
        return NULL;
    }
    if (width <= 0 || height <= 0) {
        Log_Printf("video: refusing non-positive mode %dx%d\n", width, height);
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
        /* M1 step 1 (plan section 5.2): the physical window and the storage
         * buffer are separate sizes. The window follows the OS request
         * immediately; the texture + surface below follow only if their
         * allocations succeed. On failure the old framebuffer keeps
         * rendering, scaled by the logical-size renderer (step 6). Never
         * claim the storage followed the window when it did not. */
        SDL_SetWindowSize(g_sdl2Video.window, want_w, want_h);
        g_sdl2Video.width = want_w;
        g_sdl2Video.height = want_h;
    }
    /* M1 step 2 continued: refuse sizes the renderer cannot texture before
     * touching the storage pair. Query once the renderer exists; without one
     * yet, SDL_CreateTexture below is the check (it fails the same way). */
    if (!is_gl && g_sdl2Video.renderer != NULL) {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(g_sdl2Video.renderer, &info) == 0 && info.max_texture_width > 0 &&
            info.max_texture_height > 0 &&
            (want_w > info.max_texture_width || want_h > info.max_texture_height)) {
            Log_Printf("video: mode %dx%d exceeds renderer texture limit %dx%d\n", want_w, want_h,
                       info.max_texture_width, info.max_texture_height);
            return NULL;
        }
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
        /*
         * Fix (port change): this used to be a 1x1 placeholder on the theory
         * that the engine only checks the returned surface for NULL. It does
         * more: Gfx_PresentGlScreen passes g_screenSurface->w/h to
         * GlScreen_Draw, so the OpenGLScreen quad was drawn 1x1 in the corner
         * of the window and the rest stayed black. The surface must therefore
         * carry the real presentation size.
         */
        if (g_sdl2Video.placeholder == NULL || g_sdl2Video.placeholder->w != g_sdl2Video.width ||
            g_sdl2Video.placeholder->h != g_sdl2Video.height) {
            /*
             * Fix (port change): the old placeholder was released before the
             * replacement was allocated, so a failed allocation returned NULL
             * with g_sdl2Video.placeholder NULL while Video_OnResize kept the
             * borrowed g_screenSurface/g_videoSurface alias pointing at the
             * freed surface -- the next frame read ->w/->h from released
             * memory (AddressSanitizer: heap-use-after-free). Allocate first
             * and free the old one only once the new one exists, so a refused
             * resize leaves a live display as the caller expects.
             */
            SDL_Surface *new_placeholder = SDL_CreateRGBSurfaceWithFormat(0, g_sdl2Video.width,
                                                                          g_sdl2Video.height, bpp, format);

            if (new_placeholder == NULL) {
                return NULL;
            }
            if (g_sdl2Video.placeholder != NULL) {
                SDL_FreeSurface(g_sdl2Video.placeholder);
            }
            g_sdl2Video.placeholder = new_placeholder;
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

    /*
     * Texture and surface are replaced as one transaction. A failed resize
     * must leave the pair consistent: if the texture kept its old size while
     * the surface was swapped to the new one, Sdl2_Present would upload
     * surface->pixels with a pitch the surviving texture does not match
     * (reads past the allocation -- SIGSEGV on stock SDL2), and a later retry
     * at the same size would see matching texture_w/texture_h metadata and
     * never rebuild. Build both replacements first; commit both only when
     * both succeeded, leaving the old pair untouched on any failure.
     */
    int want_texture = (g_sdl2Video.texture == NULL || g_sdl2Video.texture_format != format ||
                        g_sdl2Video.texture_w != g_sdl2Video.width ||
                        g_sdl2Video.texture_h != g_sdl2Video.height);
    int want_surface = (g_sdl2Video.surface == NULL ||
                        g_sdl2Video.surface->w != g_sdl2Video.width ||
                        g_sdl2Video.surface->h != g_sdl2Video.height ||
                        g_sdl2Video.surface->format->BitsPerPixel != bpp);
    if (want_texture || want_surface) {
        SDL_Texture *new_texture = g_sdl2Video.texture;
        SDL_Surface *new_surface = g_sdl2Video.surface;
        if (want_texture) {
            new_texture = SDL_CreateTexture(g_sdl2Video.renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                            g_sdl2Video.width, g_sdl2Video.height);
            if (new_texture == NULL) {
                Log_Printf("video: SDL_CreateTexture failed: %s\n", SDL_GetError());
                return NULL;    /* the old texture and surface stay paired */
            }
        }
        if (want_surface) {
            new_surface = SDL_CreateRGBSurface(0, g_sdl2Video.width, g_sdl2Video.height, bpp, rmask, gmask,
                                               bmask, amask);
            if (new_surface == NULL) {
                if (new_texture != g_sdl2Video.texture) {
                    SDL_DestroyTexture(new_texture);
                }
                Log_Printf("video: SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
                return NULL;    /* the old texture and surface stay paired */
            }
        }
        if (new_texture != g_sdl2Video.texture) {
            SDL_DestroyTexture(g_sdl2Video.texture);
#ifdef MUGEN_SDL2_DIAG
            g_sdl2DiagTextureDestroy++;
            g_sdl2DiagTextureCreate++;
#endif
            g_sdl2Video.texture = new_texture;
            g_sdl2Video.texture_format = format;
        }
        if (new_surface != g_sdl2Video.surface) {
            SDL_FreeSurface(g_sdl2Video.surface);
            g_sdl2Video.surface = new_surface;
            /* Opaque: the engine writes finished pixels and composites with its own
             * blitters, so upload-time blending would darken the image. */
            SDL_SetSurfaceBlendMode(g_sdl2Video.surface, SDL_BLENDMODE_NONE);
        }
    }
    /*
     * Record what the pair really is. Only reached after both replacements
     * succeeded, so this is the storage a later reset must rebuild from --
     * equal to the window size here, but not after a refused resize.
     */
    if (g_sdl2Video.surface != NULL) {
        g_sdl2Video.storage_w = g_sdl2Video.surface->w;
        g_sdl2Video.storage_h = g_sdl2Video.surface->h;
    }
    g_sdl2Video.texture_w = g_sdl2Video.width;
    g_sdl2Video.texture_h = g_sdl2Video.height;
    return g_sdl2Video.surface;
}

/* Uploads the engine's framebuffer into the texture and draws it.
 *
 * M3 frame boundary (RENDERING_PLAN.md section 7): SDL_UpdateRect only marks
 * the display dirty; the actual upload + present happens in Sdl2_PresentFrame
 * at the frame boundary (Screen_Present) or in Sdl2_Flip (the PageFlip path).
 * A thousand offscreen pixel updates therefore present nothing, and one
 * composed frame presents exactly once. SDL 1.2 presented in UpdateRect
 * itself; the shim splits the two so the engine's call pattern (UpdateRect
 * per primitive, Flip per page-flipped frame) maps onto one present each. */
/*
 * Port addition: SDL_RENDER_DEVICE_RESET / SDL_RENDER_TARGETS_RESET mean the
 * renderer's backing store is gone and every texture must be recreated. Android
 * already handles this (Android_ResetRenderer); the desktop SDL2 backend kept
 * using its cached texture and drew into a dead handle.
 *
 * The resources are recreated here rather than dropped for the next
 * Sdl2_Present() to rebuild: Sdl2_Present returns as soon as the renderer or
 * the texture is NULL, so leaving them NULL froze the window until the next
 * video-mode change -- which on a desktop that never changes mode means
 * forever. Rebuilding immediately also keeps the logical size and scaling
 * hints that Sdl2_SetVideoMode installs.
 */
void Sdl2_ResetRenderer(void)
{
    if (g_sdl2Video.is_gl) {
        /* The GL renderer owns its context; nothing SDL2-side to rebuild. */
        return;
    }
    if (g_sdl2Video.texture != NULL) {
        SDL_DestroyTexture(g_sdl2Video.texture);
        g_sdl2Video.texture = NULL;
    }
    if (g_sdl2Video.renderer != NULL) {
        SDL_DestroyRenderer(g_sdl2Video.renderer);
        g_sdl2Video.renderer = NULL;
    }
    /* texture_format is deliberately KEPT: it is what the replacement texture
     * must be created with. Zeroing it here made Sdl2_CreateRendererAndTexture()
     * bail out and left the window frozen -- the very failure this function
     * exists to prevent. Only the size markers are invalidated. */
    g_sdl2Video.texture_w = 0;
    g_sdl2Video.texture_h = 0;
    if (Sdl2_CreateRendererAndTexture() != 0) {
        Log_Printf("video: renderer reset failed: %s\n", SDL_GetError());
    }
}

static void Sdl2_Present(void)
{
    if (g_sdl2Video.is_gl) {
        if (g_sdl2Video.window != NULL) {
            SDL_GL_SwapWindow(g_sdl2Video.window);
        }
        return;
    }
    /*
     * Self-healing: a previous reset that failed (no new renderer available
     * yet) must not freeze presentation for the rest of the session, which is
     * what a bare early-return did. Retry the rebuild once per frame until it
     * succeeds; the cost of a failed attempt is one SDL call pair.
     */
    if (g_sdl2Video.renderer == NULL || g_sdl2Video.texture == NULL) {
        if (Sdl2_CreateRendererAndTexture() != 0) {
            return;
        }
    }
    if (g_sdl2Video.surface == NULL) {
        return;
    }
    if (SDL_UpdateTexture(g_sdl2Video.texture, NULL, g_sdl2Video.surface->pixels,
                          g_sdl2Video.surface->pitch) != 0) {
        return;
    }
    SDL_RenderClear(g_sdl2Video.renderer);
    SDL_RenderCopy(g_sdl2Video.renderer, g_sdl2Video.texture, NULL, NULL);
    SDL_RenderPresent(g_sdl2Video.renderer);
#ifdef MUGEN_SDL2_DIAG
    g_sdl2DiagPresent++;
#endif
}

void Sdl2_Flip(SDL_Surface *surface)
{
    (void)surface;      /* there is exactly one window; the argument is implicit */
    Sdl2_Present();
    g_sdl2Video.display_dirty = 0;  /* flushed */
}

/*
 * Marks the display dirty without presenting. Offscreen render-target
 * updates (prim.c pixel primitives) land here hundreds of times per frame;
 * presenting each one doubled the upload work and broke the one-present-per-
 * frame contract. The frame boundary (Sdl2_PresentFrame, called from
 * Screen_Present on SDL2 desktop builds) does the single upload+present.
 *
 * Only the display surface dirties the display: UpdateRect on any other
 * surface is a no-op for presentation (its pixels reach the screen through
 * the framebuffer composition in Gfx_Present, which targets the display).
 */
void Sdl2_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (!g_sdl2Video.is_gl && surface != NULL && surface == g_sdl2Video.surface) {
        g_sdl2Video.display_dirty = 1;
    }
}

/*
 * Presents the frame if UpdateRect marked the display dirty since the last
 * present. Called once per Screen_Present on SDL2 desktop builds (see
 * frame.c) so Normal and PageFlip both produce exactly one upload+present
 * for the final frame: Normal via UpdateRect-dirty + this flush, PageFlip
 * via Flip (UpdateRect only dirties, Flip presents and clears).
 */
void Sdl2_PresentFrame(void)
{
    if (!g_sdl2Video.display_dirty) {
        return;
    }
    Sdl2_Present();
    g_sdl2Video.display_dirty = 0;
}

/*
 * Explicit dirty mark for diagnostics that draw outside the frame loop
 * (plan section 7.8): marks the display without presenting, so the next
 * frame-boundary flush shows it. Not used by the engine itself.
 */
void Sdl2_MarkDisplayDirty(void)
{
    if (!g_sdl2Video.is_gl && g_sdl2Video.surface != NULL) {
        g_sdl2Video.display_dirty = 1;
    }
}

/* Test hook: is a present pending? */
int Sdl2_DiagIsDirty(void)
{
    return g_sdl2Video.display_dirty;
}

void Sdl2_VideoQuit(void)
{

    Sdl2_DestroyVideo();
}
/* Note (plan section 5.2 step 10): the engine never calls this on its normal
 * exit path -- cleanup() in sdlmain.c calls SDL_Quit() directly, as the
 * original did, and the OS reclaims the display surface. Sdl2_VideoQuit
 * exists for the test harness and for any future explicit teardown; the
 * borrowed g_videoSurface/g_screenSurface aliases must not be dereferenced
 * after either path runs. */

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
