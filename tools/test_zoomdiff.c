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
#define ZD_SET_COLORS(s, pal, first, n) SDL_SetPaletteColors((s)->format->palette, (pal), (first), (n))
#else
#define ZD_SRCCOLORKEY SDL_SRCCOLORKEY
#define ZD_HAS_COLORKEY(s) (((s)->flags & SDL_SRCCOLORKEY) ? 1 : 0)
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
        /* No fifth byte: SDL2's SDL_Color is four; SDL 1.2's is five and the
         * extra byte is only padding. Zeroing the whole array covers both. */
        memset(pal, 0, sizeof(pal));
        pal[0].r = 10;
        pal[0].g = 20;
        pal[0].b = 30;
        pal[1].r = 200;
        pal[1].g = 150;
        pal[1].b = 100;
        ZD_SET_COLORS(s, pal, 0, 2);
        SDL_SetColorKey(s, ZD_SRCCOLORKEY, 0);
    }
    fill(s, c);
    return s;
}

/* Writes the pixels, the size and the palette, so a lost palette is visible
 * even when the indices came out right. */
static int dump(SDL_Surface *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;

    if (f == NULL) {
        return 0;
    }
    fprintf(f, "%dx%d bpp=%d pal=%d key=%d\n", s->w, s->h, s->format->BitsPerPixel,
            s->format->palette != NULL ? s->format->palette->ncolors : 0,
            ZD_HAS_COLORKEY(s) ? 1 : 0);
    if (s->format->palette != NULL) {
        for (i = 0; i < s->format->palette->ncolors && i < 256; i++) {
            fprintf(f, "%02x%02x%02x ", s->format->palette->colors[i].r, s->format->palette->colors[i].g,
                    s->format->palette->colors[i].b);
        }
        fprintf(f, "\n");
    }
    /*
     * Every pixel is written as ARGB in canonical order, using the surface's own
     * masks. SDL_gfx returns 32-bit surfaces with BGR masks (measured:
     * R=00FF, G=FF00, B=FF0000), so dumping raw bytes would report a channel
     * swap that says nothing about the scaling.
     */
    for (i = 0; i < s->h; i++) {
        int x;
        const Uint8 *row = (Uint8 *)s->pixels + i * s->pitch;

        for (x = 0; x < s->w; x++) {
            Uint8 out[4] = {0, 0, 0, 0};

            if (s->format->BytesPerPixel == 4) {
                Uint32 v;
                memcpy(&v, row + x * 4, 4);
                out[0] = 0xFF;
                out[1] = (Uint8)(((v & s->format->Rmask) >> 16) & 0xFF);
                out[2] = (Uint8)(((v & s->format->Gmask) >> 8) & 0xFF);
                out[3] = (Uint8)((v & s->format->Bmask) & 0xFF);
                fwrite(out, 1, 4, f);
            } else if (s->format->BytesPerPixel == 2) {
                Uint16 v;
                memcpy(&v, row + x * 2, 2);
                out[0] = 0xFF;
                out[1] = (Uint8)((((v & s->format->Rmask) >> 11) * 255) / 31);
                out[2] = (Uint8)((((v & s->format->Gmask) >> 5) * 255) / 63);
                out[3] = (Uint8)(((v & s->format->Bmask) * 255) / 31);
                fwrite(out, 1, 4, f);
            } else {
                fwrite(row + x, 1, 1, f);
            }
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
        if (!dump(dst, path)) {
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
