/* Native Android SDL2 window, renderer and streaming texture. */
#include "android_platform.h"

static SDL_Window *display_window;
static SDL_Renderer *display_renderer;
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
