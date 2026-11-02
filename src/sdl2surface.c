#if defined(MUGEN_SDL2) || defined(ANDROID_PORT) || defined(MUGEN_ANDROID_SW)
/*
 * Shared SDL2 nearest-neighbour scaler, proven against libSDL_gfx.
 *
 * Extracted from src/sdl2compat.c (PLAN Android B4, desktop SDL2_PLAN Stage 4)
 * so SDL2 consumers that must NOT pull in the desktop window/present
 * implementation (Android, Vita SDL2) can still scale identically. The
 * sampling rules, palette handling, colour-key capture, blend-mode
 * inheritance and 16-bit promotion are byte-identical to the desktop shim;
 * tools/test_zoomdiff.sh (19 differential cases) is the proof. No window,
 * renderer, texture or audio state here -- pure surface math.
 */
#include "sdl2surface.h"
#include <math.h>
#include <limits.h>

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
        /* SDL 1.2 has no per-entry alpha and reports 255 for every entry, so
         * the pad colour is opaque black rather than transparent black. */
        for (i = 0; i < 256; i++) {
            black[i].r = 0;
            black[i].g = 0;
            black[i].b = 0;
            black[i].a = 255;
        }
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
    SDL_SetSurfaceAlphaMod(dst, a);
    /*
     * The blend *mode* is copied as "does this surface blend at all", not as
     * SDL2's enum value. SDL_gfx returns surfaces carrying SDL_SRCALPHA when the
     * source had per-pixel alpha, and the engine's next blit composites
     * differently depending on that. Inheriting BLENDMODE_NONE instead made
     * scaled semi-transparent text opaque, which the differential test caught
     * as a metadata difference once it started recording transparency.
     */
    SDL_SetSurfaceBlendMode(dst, blend);
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
/*
 * Source coordinate for output column x, using SDL_gfx's two sampling rules.
 *
 * They are genuinely different and must not be unified:
 *
 *  - The RGBA path (_zoomSurfaceRGBA) truncates the step once to 16.16
 *    fixed point and then extracts each coordinate with >> 16:
 *        step = floor(65536 * src / dst)
 *        sx   = (x * step) >> 16
 *    The quantised step makes the mapping drift by a pixel on some ratios: for
 *    16 -> 12 the step is 87381, so x=3 gives 262143 >> 16 = 3 where exact
 *    arithmetic gives 4. A floating-point accumulator cannot reproduce that,
 *    because the error is introduced by truncating the step, not accumulated.
 *
 *  - The indexed path (_zoomSurfaceY) walks integer remainders instead:
 *    pos += src per output pixel, emitting pos for each multiple of dst.
 *
 * Both were confirmed against the kit's libSDL_gfx.a image by image.
 */
#define SDLGFX_FP_ONE 65536

static int Sdl2_ZoomStep(int src_len, int dst_len)
{
    return (SDLGFX_FP_ONE * src_len) / dst_len;
}

static int Sdl2_ZoomRgbaCoord(int x, int step)
{
    return (x * step) >> 16;
}

/* Integer remainder accumulation, emitting `pos` when the running numerator
 * reaches dst_len. `rem` carries across calls. */
static int Sdl2_ZoomIndexedCoord(int src_len, int dst_len, int *rem, int *pos)
{
    int out = *pos;

    *rem += src_len;
    while (*rem >= dst_len) {
        *rem -= dst_len;
        (*pos)++;
    }
    return out;
}

/*
 * RGB555 (15bpp) source -> ARGB8888 by shifting.
 *
 * SDL 1.2's blitter widened a 5-bit channel with >>loss << loss, which for a
 * 5-bit value is a plain << 3: pixel 0x0025 reads as B=0x28. SDL2's converter
 * scales instead and gives 0x29, so the RGB555 case is handled here. RGB565 is
 * 16 bits wide, takes the other path in SDL 1.2, and agrees with SDL2's
 * converter -- it uses that.
 */
static SDL_Surface *Sdl2_Promote16(SDL_Surface *src)
{
    SDL_Surface *dst;
    int x, y;

    dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (dst == NULL) {
        return NULL;
    }
    if (SDL_LockSurface(src) != 0) {
        SDL_FreeSurface(dst);
        return NULL;
    }
    if (SDL_LockSurface(dst) != 0) {
        SDL_UnlockSurface(src);
        SDL_FreeSurface(dst);
        return NULL;
    }
    for (y = 0; y < src->h; y++) {
        const Uint16 *srow = (const Uint16 *)((const Uint8 *)src->pixels + y * src->pitch);
        Uint32 *drow = (Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch);

        for (x = 0; x < src->w; x++) {
            Uint16 v = srow[x];
            Uint32 r = ((v & src->format->Rmask) >> src->format->Rshift) << src->format->Rloss;
            Uint32 g = ((v & src->format->Gmask) >> src->format->Gshift) << src->format->Gloss;
            Uint32 b = ((v & src->format->Bmask) >> src->format->Bshift) << src->format->Bloss;

            drow[x] = 0xFF000000u | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    return dst;
}

static SDL_Surface *Sdl2_ZoomNearest(SDL_Surface *src, int dw, int dh)
{
    SDL_Surface *dst;
    int x;
    int spitch, dpitch, bpp;
    int indexed = src->format->palette != NULL;
    int step_x = 0, step_y = 0;

    if (!indexed) {
        step_x = Sdl2_ZoomStep(src->w, dw);
        step_y = Sdl2_ZoomStep(src->h, dh);
    }
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
    if (indexed) {
        int rem_y = 0, pos_y = 0;
        int y;

        for (y = 0; y < dh; y++) {
            int sy = Sdl2_ZoomIndexedCoord(src->h, dh, &rem_y, &pos_y);
            int rem_x = 0, pos_x = 0;

            if (sy >= src->h) {
                sy = src->h - 1;
            }
            for (x = 0; x < dw; x++) {
                int sx = Sdl2_ZoomIndexedCoord(src->w, dw, &rem_x, &pos_x);

                if (sx >= src->w) {
                    sx = src->w - 1;
                }
                memcpy((Uint8 *)dst->pixels + y * dpitch + x * bpp,
                       (const Uint8 *)src->pixels + sy * spitch + sx * bpp, (size_t)bpp);
            }
        }
    } else {
        int y;

        for (y = 0; y < dh; y++) {
            int sy = Sdl2_ZoomRgbaCoord(y, step_y);

            if (sy >= src->h) {
                sy = src->h - 1;
            }
            for (x = 0; x < dw; x++) {
                int sx = Sdl2_ZoomRgbaCoord(x, step_x);

                if (sx >= src->w) {
                    sx = src->w - 1;
                }
                memcpy((Uint8 *)dst->pixels + y * dpitch + x * bpp,
                       (const Uint8 *)src->pixels + sy * spitch + sx * bpp, (size_t)bpp);
            }
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    return dst;
}

SDL_Surface *Sdl2_ScaleSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)
{
    int dw, dh;
    SDL_Surface *dst;

    if (src == NULL) {
        return NULL;
    }
    if (!(zoomx > 0.0) || !(zoomy > 0.0) || !isfinite(zoomx) || !isfinite(zoomy) ||
        !isfinite((double)src->w * zoomx) || !isfinite((double)src->h * zoomy) ||
        (double)src->w * zoomx + 0.5 > INT_MAX || (double)src->h * zoomy + 0.5 > INT_MAX) {
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
    /*
     * SDL_gfx promotes 16-bit sources to 32-bit output (measured: an RGB565
     * surface comes back as 32bpp), so the scaling itself runs on 32-bit
     * pixels. The conversion uses SDL2's own converter for the same reason
     * SDL_gfx uses SDL_ConvertSurface: any channel-expansion rounding then
     * comes from SDL, not from this shim, and the remaining comparison is
     * about the scaling.
     */
    if (src->format->BytesPerPixel == 2) {
        /*
         * RGB565 goes through SDL2's converter, which scales and which the
         * differential test has proven identical. RGB555 takes the shift path
         * below: it is 15 bits wide, so SDL 1.2's blitter widened it with a
         * shift rather than a scale (5 -> 0x28, not 0x29), and SDL2's converter
         * does not reproduce that.
         */
        SDL_Surface *promoted = (src->format->BitsPerPixel == 16)
                                    ? SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0)
                                    : Sdl2_Promote16(src);
        int had_alpha = src->format->Amask != 0;

        if (promoted == NULL) {
            return NULL;
        }
        dst = Sdl2_ZoomNearest(promoted, dw, dh);
        SDL_FreeSurface(promoted);
        if (dst != NULL && !had_alpha) {
            SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_NONE);
        }
        return dst;
    }
    return Sdl2_ZoomNearest(src, dw, dh);
}

#ifndef ANDROID_PORT
/* Desktop SDL_gfx entry point retained for the reconstructed desktop path. */
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth)
{
    return Sdl2_ScaleSurface(src, zoomx, zoomy, smooth);
}
#endif

#endif /* SDL2 consumers only */
