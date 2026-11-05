/* Native Android SDL2 window, renderer and streaming texture. */
#include "android_platform.h"
#include <stdio.h>

/*
 * Engine global (src/globals.c). This file deliberately does not include
 * symbols.h -- it is the platform layer, not an engine module -- so the one
 * global it sets is declared here.
 */
extern int g_rendererFallbackRequested;

static SDL_Window *display_window;
static SDL_Renderer *display_renderer;
#ifdef MUGEN_GLES
/* Set when the window was created for GL: the two presentation paths cannot
 * share a window (plan section 12.2), so the flags are decided at creation and
 * a switch tears the window down. */
static SDL_GLContext display_gl;
static int display_is_gl;
static SDL_Surface *display_placeholder;
#endif
static SDL_Texture *display_texture;
static SDL_Surface *display_pixels;
static int display_dirty;
static int display_background;

static SDL_Texture *CreateTexture(SDL_Surface *pixels)
{
    SDL_Texture *texture = SDL_CreateTexture(display_renderer, pixels->format->format,
                                            SDL_TEXTUREACCESS_STREAMING, pixels->w, pixels->h);
    if (texture != NULL && SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

SDL_Surface *Android_CreateDisplay(int width, int height, int depth, int keep_aspect)
{
    SDL_Surface *pixels;
    SDL_Texture *texture;
    Uint32 format;

    if (width <= 0 || height <= 0 || (depth != 16 && depth != 32)) {
        SDL_SetError("Android display requires positive dimensions and 16 or 32-bit pixels");
        return NULL;
    }
    format = depth == 16 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_RGB888;
    pixels = SDL_CreateRGBSurfaceWithFormat(0, width, height, depth, format);
    if (pixels == NULL) {
        return NULL;
    }
    /* The engine writes rows using width * bytes-per-pixel addressing. */
    if (pixels->pitch != width * pixels->format->BytesPerPixel) {
        SDL_FreeSurface(pixels);
        SDL_SetError("Android display requires a tightly packed SDL2 pitch");
        return NULL;
    }
    SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_NONE);
    SDL_FillRect(pixels, NULL, 0);
#ifdef MUGEN_GLES
    /*
     * Coming back from the GL path: SDL2 cannot convert a GL window into a
     * renderer window, so the window has to go. Without this the renderer was
     * never created and the first texture failed with "Parameter 'renderer' is
     * invalid" -- which is what a failed GL probe falling back to System does.
     */
    if (display_gl != NULL) {
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
        SDL_DestroyWindow(display_window);
        display_window = NULL;
        SDL_FreeSurface(display_placeholder);
        display_placeholder = NULL;
        display_is_gl = 0;
    }
#endif
    if (display_window == NULL) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        display_window = SDL_CreateWindow("M.U.G.E.N", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           width, height, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
        if (display_window == NULL) {
            goto fail;
        }
        display_renderer = SDL_CreateRenderer(display_window, -1, SDL_RENDERER_ACCELERATED);
        if (display_renderer == NULL) {
            display_renderer = SDL_CreateRenderer(display_window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (display_renderer == NULL) {
            Android_VideoQuit();
            goto fail;
        }
    }
    texture = CreateTexture(pixels);
    if (texture == NULL) {
        goto fail;
    }
    /* Logical size is the game resolution, never the physical panel. */
    if (SDL_RenderSetLogicalSize(display_renderer, keep_aspect ? width : 0, keep_aspect ? height : 0) < 0) {
        SDL_DestroyTexture(texture);
        goto fail;
    }
    SDL_DestroyTexture(display_texture);
    SDL_FreeSurface(display_pixels);
    display_texture = texture;
    display_pixels = pixels;
    display_dirty = 0;
    SDL_ShowCursor(SDL_DISABLE);
    return pixels;
fail:
    SDL_FreeSurface(pixels);
    return NULL;
}

#ifdef MUGEN_GLES
/*
 * Creates the GL window and context the GLES renderer draws into.
 *
 * SDL2 cannot change a window's flags after creation, and a window carries
 * either an SDL_Renderer or a GL context, never both. Selecting GL therefore
 * destroys any renderer path first and builds this one; selecting System again
 * destroys this and rebuilds the renderer path. Returns a placeholder surface
 * that carries the engine's render size -- the GLES renderer draws into the
 * default framebuffer, and the engine only uses the surface's dimensions.
 */
SDL_Surface *Android_CreateDisplayGL(int width, int height, int depth, int keep_aspect)
{
    SDL_Surface *surface;
    Uint32 format;

    (void)keep_aspect;
    if (width <= 0 || height <= 0) {
        SDL_SetError("Android GL display requires positive dimensions");
        return NULL;
    }
    if (display_gl != NULL) {
        /* Already a GL window: keep it, just hand back the size. */
        if (display_placeholder != NULL && display_placeholder->w == width && display_placeholder->h == height) {
            return display_placeholder;
        }
    } else {
        /* Coming from (or starting on) the SDL_Renderer path: tear it down. */
        SDL_DestroyTexture(display_texture);
        SDL_DestroyRenderer(display_renderer);
        SDL_DestroyWindow(display_window);
        display_texture = NULL;
        display_renderer = NULL;
        display_window = NULL;
        SDL_FreeSurface(display_pixels);
        display_pixels = NULL;
    }
    format = depth == 32 ? SDL_PIXELFORMAT_RGBA8888 : SDL_PIXELFORMAT_RGB565;
    if (display_gl == NULL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        display_window = SDL_CreateWindow("M.U.G.E.N", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                          height, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP |
                                                      SDL_WINDOW_ALLOW_HIGHDPI);
        if (display_window == NULL) {
            return NULL;
        }
        display_gl = SDL_GL_CreateContext(display_window);
        if (display_gl == NULL) {
            SDL_DestroyWindow(display_window);
            display_window = NULL;
            return NULL;
        }
        if (SDL_GL_MakeCurrent(display_window, display_gl) < 0) {
            SDL_GL_DeleteContext(display_gl);
            display_gl = NULL;
            SDL_DestroyWindow(display_window);
            display_window = NULL;
            return NULL;
        }
        if (gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress) == 0) {
            SDL_GL_DeleteContext(display_gl);
            display_gl = NULL;
            SDL_DestroyWindow(display_window);
            display_window = NULL;
            return NULL;
        }
        display_is_gl = 1;
        SDL_ShowCursor(SDL_DISABLE);
    }
    SDL_FreeSurface(display_placeholder);
    surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, format);
    display_placeholder = surface;
    return surface;
}

/* The GL window's drawable size in pixels: the panel or window size, which is
 * not the game resolution. The GLES presenter builds its viewport from this. */
void Android_GLDrawableSize(int *w, int *h)
{
    int dw = 0;
    int dh = 0;

    if (w != NULL) {
        *w = 0;
    }
    if (h != NULL) {
        *h = 0;
    }
    if (display_window == NULL) {
        return;
    }
    SDL_GL_GetDrawableSize(display_window, &dw, &dh);
    if (dw <= 0 || dh <= 0) {
        SDL_GetWindowSize(display_window, &dw, &dh);
    }
    if (w != NULL) {
        *w = dw;
    }
    if (h != NULL) {
        *h = dh;
    }
}

/* Presents the GL frame. The GLES renderer has already drawn into the default
 * framebuffer, so this is the swap and nothing else. */
/*
 * Re-creates the GL context after Android destroyed it (RENDERING_PLAN 12.6).
 *
 * The window survives a context loss, so only the context and the GL objects
 * have to be rebuilt. Returns 1 when a usable context is current again; the
 * caller then asks the renderer to rebuild its own objects.
 */
int Android_GLRecreateContext(void)
{
    /* Failure rehearsal, paired with the restore and loss hooks: a file named
     * "gl_fail_recreate" makes one recreate fail, so the recovery path for a
     * context that never comes back can be exercised on a device. Renamed on
     * use, so it fires once. */
    {
        FILE *probe = fopen("gl_fail_recreate", "r");

        if (probe != NULL) {
            fclose(probe);
            remove("gl_fail_recreate");
            SDL_Log("mugen: GL context recreate failed on request (gl_fail_recreate)");
            return 0;
        }
    }
    if (display_window == NULL) {
        return 0;
    }
    if (display_gl != NULL) {
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
    }
    display_gl = SDL_GL_CreateContext(display_window);
    if (display_gl == NULL) {
        SDL_Log("mugen: GL context could not be recreated: %s", SDL_GetError());
        return 0;
    }
    if (SDL_GL_MakeCurrent(display_window, display_gl) < 0) {
        SDL_Log("mugen: GL context not current after recreate: %s", SDL_GetError());
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
        return 0;
    }
    if (gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress) == 0) {
        SDL_Log("mugen: GLES entry points not reloadable after recreate");
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
        return 0;
    }
    SDL_Log("mugen: GL context recreated");
    return 1;
}

/*
 * Deliberately destroys the GL context, standing in for Android reclaiming it.
 *
 * RENDERING_PLAN 12.6 asks for the loss path to be exercised. Waiting for a
 * real background/restore cycle is not reproducible, so this exists for the
 * test build: it drops the context exactly as the system would, and the next
 * foreground event must rebuild it.
 */
int Android_GLForceContextLoss(void)
{
    if (display_gl == NULL) {
        return 0;
    }
    SDL_GL_DeleteContext(display_gl);
    display_gl = NULL;
    SDL_Log("mugen: GL context destroyed on request (loss simulation)");
    return 1;
}

/*
 * Tears the GL window and context down, so the System renderer can take the
 * window over (RENDERING_PLAN 12.2: exactly one owner at a time). Used by the
 * GL-to-System recovery in System_Init.
 */
void Android_DestroyGLContext(void)
{
    if (display_gl != NULL) {
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
    }
    if (display_window != NULL) {
        SDL_DestroyWindow(display_window);
        display_window = NULL;
    }
    SDL_Log("mugen: GL window and context destroyed");
}

/*
 * Asks the engine to leave the screen it is on so System_Init runs again and
 * performs the verified GL -> System recovery (RENDERING_PLAN 12.6).
 *
 * Set here rather than acted on immediately: the event pump runs inside the
 * screen loop, and tearing the renderer down under a live draw would use names
 * the engine is about to draw with.
 */
void Android_RequestRendererFallback(void)
{
    g_rendererFallbackRequested = 1;
    SDL_Log("mugen: renderer fallback requested; restarting the renderer");
}

/* 1 when a GL context is current (0 when the GL path is not in use). */
int Android_GLContextValid(void)
{
    return display_gl != NULL;
}

int Android_GLPresent(void)
{
    if (display_window == NULL || display_gl == NULL) {
        return 0;
    }
    SDL_GL_SwapWindow(display_window);
    return 1;
}
#endif /* MUGEN_GLES */

void Android_MarkDisplayDirty(SDL_Surface *surface)
{
    if (surface != NULL && surface == display_pixels) {
        display_dirty = 1;
    }
}

int Android_PresentFrame(void)
{
    if (!display_dirty || display_background || display_pixels == NULL || display_renderer == NULL) {
        return 0;
    }
    if (display_texture == NULL) {
        display_texture = CreateTexture(display_pixels);
        if (display_texture == NULL) {
            return -1;
        }
    }
    if (SDL_UpdateTexture(display_texture, NULL, display_pixels->pixels, display_pixels->pitch) < 0 ||
        SDL_SetRenderDrawColor(display_renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(display_renderer) < 0 ||
        SDL_RenderCopy(display_renderer, display_texture, NULL, NULL) < 0) {
        return -1;
    }
    SDL_RenderPresent(display_renderer);
    display_dirty = 0;
    return 1;
}

void Android_SetBackground(int background)
{
    display_background = background;
    if (!background && display_pixels != NULL) {
        display_dirty = 1;
    }
}

void Android_ResetRenderer(void)
{
    SDL_DestroyTexture(display_texture);
    display_texture = NULL;
    display_dirty = display_pixels != NULL;
}

int Android_SetFullscreen(int fullscreen)
{
    if (display_window == NULL) {
        return 0;
    }
    return SDL_SetWindowFullscreen(display_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0;
}

void Android_VideoQuit(void)
{
#ifdef MUGEN_GLES
    if (display_gl != NULL) {
        SDL_GL_DeleteContext(display_gl);
        display_gl = NULL;
    }
    SDL_FreeSurface(display_placeholder);
    display_placeholder = NULL;
    display_is_gl = 0;
#endif
    SDL_DestroyTexture(display_texture);
    SDL_DestroyRenderer(display_renderer);
    SDL_DestroyWindow(display_window);
    SDL_FreeSurface(display_pixels);
    display_texture = NULL;
    display_renderer = NULL;
    display_window = NULL;
    display_pixels = NULL;
    display_dirty = 0;
    display_background = 0;
}
