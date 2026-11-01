// =============================================================================
// zoomSurface() — the one SDL_gfx entry point the engine uses.
//
// src/gfx.c and src/font.c call it with `smooth = 0` (nearest neighbour) and
// expect SDL_gfx's contract exactly: a new surface of size
// (src->w * zoomx, src->h * zoomy), in the source's own format, with colour-key
// and alpha flags preserved. Both call sites free the result with
// SDL_FreeSurface.
//
// Why it is reimplemented rather than vendored: SDL_gfx has no maintained SDL2
// port, and the engine needs exactly this function. SDL2's own
// SDL_SoftStretch handles the scaling but not the surface creation or the flag
// copying, so the wrapper is thin.
//
// The `smooth` path is not implemented: no call site asks for it, and silently
// producing an unsmoothed image would be a behavioural lie if one ever did.
// =============================================================================
#ifdef __ANDROID__

#include <math.h>
#include <string.h>

#include <SDL.h>

#ifndef SDL2_GFX_ZOOM_H
#define SDL2_GFX_ZOOM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Declared exactly as the engine declares it (src/gfx.c, src/font.c:
 * `extern SDL_Surface *__cdecl zoomSurface(...)`). __cdecl is meaningless on
 * ARM but harmless, and keeping the signatures identical avoids a link-time
 * mismatch if the engine's declaration ever changes.
 */
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);

#ifdef __cplusplus
}
#endif

#endif /* SDL2_GFX_ZOOM_H */

SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)
{
    int dw;
    int dh;
    SDL_Surface *dst;

    if (src == NULL) {
        return NULL;
    }
    if (zoomx <= 0.0 || zoomy <= 0.0) {
        SDL_SetError("zoomSurface: zoom factors must be positive");
        return NULL;
    }
    if (smooth != 0) {
        /*
         * SDL_gfx would use its interpolating path here. Nothing in this engine
         * asks for it; failing loudly beats returning a nearest-neighbour image
         * that looks almost right.
         */
        SDL_SetError("zoomSurface: smooth scaling is not implemented on Android");
        return NULL;
    }

    dw = (int)(src->w * zoomx);
    dh = (int)(src->h * zoomy);
    if (dw < 1 || dh < 1) {
        SDL_SetError("zoomSurface: resulting size is empty");
        return NULL;
    }

    /*
     * Same format as the source, so the caller can treat the result as a drop-in
     * replacement -- which is how both call sites use it.
     */
    dst = SDL_CreateRGBSurface(0,
                               dw, dh, src->format->BitsPerPixel,
                               src->format->Rmask, src->format->Gmask,
                               src->format->Bmask, src->format->Amask);
    if (dst == NULL) {
        return NULL;
    }

    /*
     * Preserve the colour key, as SDL_gfx does. SDL2 keeps the key on the
     * surface (SDL_HasColorKey / SDL_GetColorKey) rather than in the pixel
     * format, which is where SDL 1.2 stored it.
     */
    {
        Uint32 key;
        if (SDL_GetColorKey(src, &key) == 0) {
            SDL_SetColorKey(dst, SDL_TRUE, key);
        }
    }
    if (src->format->palette != NULL && dst->format->palette != NULL) {
        SDL_SetPaletteColors(dst->format->palette, src->format->palette->colors, 0,
                             src->format->palette->ncolors);
    }

    /*
     * SDL2's blit does the scaling. SDL_BlitScaled with a NULL destination rect
     * fills the whole target, which is exactly (x * zoomx, y * zoomy) rounded
     * down -- SDL_gfx truncates the same way.
     */
    if (SDL_BlitScaled(src, NULL, dst, NULL) != 0) {
        /* Preserve the error; the caller only checks for NULL. */
        SDL_FreeSurface(dst);
        return NULL;
    }
    return dst;
}

#endif /* __ANDROID__ */
