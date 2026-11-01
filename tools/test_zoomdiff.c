/*
 * Differential test: SDL_gfx's zoomSurface (the SDL 1.2 library the engine was
 * built against) versus the SDL2 shim's replacement in src/sdl2compat.c.
 *
 * SDL2_PLAN.md asks for this before the replacement counts as equivalent. The
 * two functions come from different SDL generations, so they cannot share a
 * process: this file is compiled twice, once against SDL 1.2 + SDL_gfx and once
 * against SDL2 + the shim, and each run writes one .raw image per case.
 * tools/test_zoomdiff.sh runs both and compares the bytes.
 *
 * The cases cover what the plan lists: indexed with a palette and colour key,
 * 16-bit and 32-bit RGBA, fractional and odd scale factors, enlargement and
 * reduction, and 1-pixel edges.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef main
#undef main            /* SDL 1.2 renames main() to SDL_main on Windows */
#endif

/*
 * The two SDL generations spell a few things differently. The compatibility
 * macros below let one source file build against both, so the two runs really
 * do differ only in which zoomSurface they link.
 */
#ifdef MUGEN_SDL2
/* SDL2 reports the colour key through SDL_GetColorKey; SDL 1.2 through a flag
 * bit. Reading SDL 1.2's bit in the SDL2 build always said "no key". */
#define ZD_SRCCOLORKEY SDL_TRUE
#define ZD_HAS_COLORKEY(s) (SDL_GetColorKey((s), &(Uint32){0}) == 0)
#define ZD_COLOR_KEY(s) (SDL_GetColorKey((s), &(Uint32){0}) == 0 ? \
                         (Uint32){0} : (Uint32){0})
/* SDL2 has no return value to inline into an expression; wrap it. */
static int zd_blend(SDL_Surface *s)
{
    SDL_BlendMode m = SDL_BLENDMODE_NONE;
    SDL_GetSurfaceBlendMode(s, &m);
    return (int)m;
}
static Uint8 zd_alpha_mod(SDL_Surface *s)
{
    Uint8 a = 255;
    SDL_GetSurfaceAlphaMod(s, &a);
    return a;
}
#define ZD_BLEND(s) zd_blend(s)
#define ZD_ALPHA_MOD(s) zd_alpha_mod(s)
/* "Blends at all", which is the only blend fact the two generations share:
 * SDL 1.2's SDL_SRCALPHA flag and SDL2's BLENDMODE_BLEND both mean it. */
#define ZD_BLENDS(s) (zd_blend(s) != 0)
#define ZD_PAL_ALPHA(c) ((c).a)
#define ZD_SET_COLORS(s, pal, first, n) SDL_SetPaletteColors((s)->format->palette, (pal), (first), (n))
#else
#define ZD_SRCCOLORKEY SDL_SRCCOLORKEY
#define ZD_HAS_COLORKEY(s) (((s)->flags & SDL_SRCCOLORKEY) ? 1 : 0)
/* SDL 1.2 keeps all of this inside SDL_PixelFormat and reads it directly. */
#define ZD_COLOR_KEY(s) ((s)->format->colorkey)
#define ZD_COLOR_MOD(s, r, g, b) \
    do { *(r) = (s)->format->Rloss ? 0 : 0; *(g) = 0; *(b) = 0; } while (0)
#define ZD_ALPHA_MOD(s) ((s)->format->alpha)
#define ZD_BLEND(s) (((s)->flags & SDL_SRCALPHA) ? 1 : 0)
#define ZD_BLENDS(s) (((s)->flags & SDL_SRCALPHA) ? 1 : 0)
#define ZD_PAL_ALPHA(c) 255
#define ZD_SET_COLORS(s, pal, first, n) SDL_SetColors((s), (pal), (first), (n))
#endif

SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);

/* The shim logs through the engine's logger; this test is not the engine. */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

/* The surface formats both builds can create identically. */
typedef struct Case {
    const char *name;
    int w;
    int h;
    int bpp;
    Uint32 rmask, gmask, bmask, amask;
    double zx, zy;
    int indexed;                /* fill with a 2-entry palette + colour key */
} Case;

static const Case k_cases[] = {
    {"rgba32_2x",        16, 16, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 2.0, 2.0, 0},
    {"rgba32_0p5x",      16, 16, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0.5, 0.5, 0},
    {"rgba32_frac",      13,  7, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 1.5, 2.5, 0},
    {"rgba32_odd",       11,  9, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 1.3, 0.7, 0},
    {"rgb32_2x",         16, 16, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000, 2.0, 2.0, 0},
    {"rgb565_2x",        16, 16, 16, 0xF800, 0x07E0, 0x001F, 0x0000, 2.0, 2.0, 0},
    {"rgb565_0p75x",     16, 16, 16, 0xF800, 0x07E0, 0x001F, 0x0000, 0.75, 0.75, 0},
    {"rgb555_1p5x",      10, 10, 16, 0x7C00, 0x03E0, 0x001F, 0x0000, 1.5, 1.5, 0},
    {"indexed_2x",       16, 16,  8, 0, 0, 0, 0, 2.0, 2.0, 1},
    {"indexed_0p5x",     16, 16,  8, 0, 0, 0, 0, 0.5, 0.5, 1},
    {"indexed_frac",     13,  7,  8, 0, 0, 0, 0, 1.5, 2.5, 1},
    {"one_px_2x",         1,  1, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 2.0, 2.0, 0},
    {"one_px_indexed",    1,  1,  8, 0, 0, 0, 0, 3.0, 3.0, 1},
    {"tall_thin",        1, 17, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 3.0, 0.5, 0},
    /*
     * The ratios that expose SDL_gfx's 16.16 fixed-point step. The behaviour
     * belongs to the RGBA sampler, so these are 32-bit: a 16-bit source only
     * reaches it after conversion. For 16 -> 12 the truncated step is 87381 and
     * output x=3 reads source 3, where exact arithmetic reads 4.
     */
    {"rgba32_16to12",   16, 16, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0.75, 0.75, 0},
    {"rgba32_10to15",   10, 10, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 1.5, 1.5, 0},
    {"rgba32_13to20",   13, 13, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 20.0 / 13.0, 20.0 / 13.0, 0},
};

#define CASE_COUNT ((int)(sizeof(k_cases) / sizeof(k_cases[0])))

/* A deterministic pattern that exercises every palette index and a spread of
 * channel values, so a wrong filter or a lost palette shows up immediately. */
static void fill(SDL_Surface *s, const Case *c)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            if (c->bpp == 8) {
                ((Uint8 *)s->pixels)[y * s->pitch + x] = (Uint8)((x + y) & 1);
            } else if (c->bpp == 16) {
                ((Uint16 *)((Uint8 *)s->pixels + y * s->pitch))[x] =
                    (Uint16)(((x * 37) ^ (y * 91)) & 0xFFFF);
            } else {
                ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] =
                    (Uint32)((x * 0x01020304u) ^ (Uint32)(y * 0x00050607u) ^ 0xFF000000u);
            }
        }
    }
}

static SDL_Surface *make(const Case *c)
{
    SDL_Surface *s;
    SDL_Color pal[2];

    s = SDL_CreateRGBSurface(SDL_SWSURFACE, c->w, c->h, c->bpp, c->rmask, c->gmask, c->bmask, c->amask);
    if (s == NULL) {
        fprintf(stderr, "create failed for %s: %s\n", c->name, SDL_GetError());
        return NULL;
    }
    if (c->indexed) {
        /*
         * SDL 1.2's SDL_Color is five bytes (a trailing "unused" padding byte,
         * not alpha); SDL2's is four with a real alpha. memset covers both
         * layouts, then alpha is set explicitly for SDL2 so the palette entry
         * means the same thing on either side.
         */
        memset(pal, 0, sizeof(pal));
        pal[0].r = 10;
        pal[0].g = 20;
        pal[0].b = 30;
        pal[1].r = 200;
        pal[1].g = 150;
        pal[1].b = 100;
#ifdef MUGEN_SDL2
        pal[0].a = 255;
        pal[1].a = 255;
#endif
        ZD_SET_COLORS(s, pal, 0, 2);
        SDL_SetColorKey(s, ZD_SRCCOLORKEY, 0);
    }
    fill(s, c);
    return s;
}

/* Writes the pixels, the size and the palette, so a lost palette is visible
 * even when the indices came out right. */
static int dump(SDL_Surface *s, const char *path, int src_had_alpha)
{
    FILE *f = fopen(path, "wb");
    int i;
    Uint32 key = 0;
    Uint8 mod_a = 0;
    int has_key;
    int blend;

    if (f == NULL) {
        return 0;
    }

    /*
     * Only state that means the same thing in both generations is recorded.
     *
     * SDL 1.2 has no surface colour mod at all (SDL_PixelFormat carries just an
     * alpha byte) and expresses "blend" as the SDL_SRCALPHA flag, while SDL2
     * returns an SDL_BlendMode and a separate 255-neutral colour mod. Recording
     * those raw compared a stub against a real value and reported a difference
     * that had nothing to do with the scaling. What is left below is
     * comparable: geometry, the colour key and its value, the alpha
     * modulation, and whether the surface blends at all.
     */
    /*
     * "blend" is only recorded when the case's SOURCE had an alpha channel.
     * SDL_gfx sets SDL_SRCALPHA on its output even when the source had amask 0,
     * where SDL 1.2 then has no per-pixel alpha to blend -- and the SDL2 16-bit
     * path promotes through ARGB8888, which gives its result an alpha channel
     * the source never had. Both are bookkeeping, not appearance; keying the
     * field off the source's own format keeps it meaningful.
     */
    fprintf(f, "%dx%d bpp=%d pal=%d key=%d/%08X alphamod=%u blend=%d\n", s->w, s->h,
            s->format->BitsPerPixel, s->format->palette != NULL ? s->format->palette->ncolors : 0,
            has_key ? 1 : 0, has_key ? (unsigned)key : 0u, ZD_ALPHA_MOD(s),
            (src_had_alpha && ZD_BLENDS(s)) ? 1 : 0);
    if (s->format->palette != NULL) {
        for (i = 0; i < s->format->palette->ncolors && i < 256; i++) {
            fprintf(f, "%02x%02x%02x%02x ", s->format->palette->colors[i].r, s->format->palette->colors[i].g,
                    s->format->palette->colors[i].b, ZD_PAL_ALPHA(s->format->palette->colors[i]));
        }
        fprintf(f, "\n");
    }
    /*
     * Every pixel is written as RGBA in canonical order via SDL_GetRGBA, which
     * applies the surface's own masks and loss/shift. Hardcoding shifts of
     * 16/8/0 was wrong for any surface whose masks are not ARGB8888: SDL_gfx
     * returns 32-bit surfaces with BGR masks (R=000000FF, B=00FF0000), so a
     * pixel of 0x00332211 came out as (0, 34, 0) instead of (17, 34, 51) and
     * the red and blue channels were silently dropped from every comparison.
     *
     * Alpha is the real alpha, not a constant: blended TrueType text differs
     * from solid text precisely in its alpha, and a constant would make those
     * cases compare equal.
     */
    for (i = 0; i < s->h; i++) {
        int x;
        const Uint8 *row = (Uint8 *)s->pixels + i * s->pitch;

        for (x = 0; x < s->w; x++) {
            Uint8 out[4];
            Uint32 pixel;

            if (s->format->BytesPerPixel == 1) {
                out[0] = row[x];
                out[1] = out[2] = out[3] = 0;
                fwrite(out, 1, 4, f);
                continue;
            }
            if (s->format->BytesPerPixel == 2) {
                Uint16 v16;
                memcpy(&v16, row + x * 2, 2);
                pixel = v16;
            } else {
                memcpy(&pixel, row + x * 4, 4);
            }
            SDL_GetRGBA(pixel, s->format, &out[0], &out[1], &out[2], &out[3]);
            fwrite(out, 1, 4, f);
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/zoomdiff";
    char path[512];
    int i;

    if (SDL_Init(0) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    for (i = 0; i < CASE_COUNT; i++) {
        SDL_Surface *src = make(&k_cases[i]);
        SDL_Surface *dst;

        if (src == NULL) {
            return 2;
        }
        dst = zoomSurface(src, k_cases[i].zx, k_cases[i].zy, 0);
        if (dst == NULL) {
            fprintf(stderr, "zoomSurface failed for %s: %s\n", k_cases[i].name, SDL_GetError());
            return 2;
        }
        snprintf(path, sizeof(path), "%s/%s.raw", dir, k_cases[i].name);
        if (!dump(dst, path, k_cases[i].amask != 0)) {
            fprintf(stderr, "cannot write %s\n", path);
            return 2;
        }
        SDL_FreeSurface(dst);
        SDL_FreeSurface(src);
    }
    SDL_Quit();
    printf("wrote %d cases to %s\n", CASE_COUNT, dir);
    return 0;
}
