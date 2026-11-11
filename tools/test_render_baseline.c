/* SDL2 render regression fixture generator.
 * Expected hashes were recorded from 6753f6b before this migration.
 * See the baseline manifests under tools/fixtures/; the runner never updates expectations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef main
#undef main            /* SDL 1.2 renames main() to SDL_main on Windows */
#endif

#define RB_SET_COLORS(s, pal, first, n) SDL_SetPaletteColors((s)->format->palette, (pal), (first), (n))

SDL_Surface *Sdl2_ScaleSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);

/* The shim logs through the engine's logger; this test is not the engine. */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void write_raw(const char *dir, const char *name, const void *data, size_t len)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    check(f != NULL, path);
    if (f != NULL) {
        check(fwrite(data, 1, len, f) == len, path);
        fclose(f);
    }
}

/* F-RAMP565: 64x64 RGB565 ramp, 0..255 per channel plus a per-row pattern. */
static void fixture_ramp565(const char *dir)
{
    SDL_Surface *s;
    int x, y;

    s = SDL_CreateRGBSurface(0, 64, 64, 16, 0xF800, 0x07E0, 0x001F, 0);
    check(s != NULL, "F-RAMP565 surface");
    if (s == NULL) {
        return;
    }
    for (y = 0; y < 64; y++) {
        unsigned short *row = (unsigned short *)((char *)s->pixels + y * s->pitch);
        for (x = 0; x < 64; x++) {
            unsigned int r = (unsigned int)((x * 255 + 32) / 63);
            unsigned int g = (unsigned int)((y * 255 + 32) / 63);
            unsigned int b = (unsigned int)(((x + y) * 255 + 63) / 126);
            row[x] = (unsigned short)((r >> 3 << 11) | (g >> 2 << 5) | (b >> 3));
        }
    }
    write_raw(dir, "F-RAMP565.raw", s->pixels, (size_t)s->pitch * 64);
    SDL_FreeSurface(s);
}

/* F-IDX8: 4x4 indexed8 with a nontrivial palette. */
static void fixture_idx8(const char *dir)
{
    SDL_Surface *s;
    SDL_Color pal[5];
    unsigned char px[16] = { 0, 1, 2, 3, 3, 2, 1, 0, 1, 1, 4, 4, 4, 2, 0, 1 };
    int i;

    s = SDL_CreateRGBSurface(0, 4, 4, 8, 0, 0, 0, 0);
    check(s != NULL, "F-IDX8 surface");
    if (s == NULL) {
        return;
    }
    pal[0].r = 0;   pal[0].g = 0;   pal[0].b = 0;
    pal[1].r = 255; pal[1].g = 0;   pal[1].b = 0;
    pal[2].r = 0;   pal[2].g = 255; pal[2].b = 0;
    pal[3].r = 0;   pal[3].g = 0;   pal[3].b = 255;
    pal[4].r = 255; pal[4].g = 255; pal[4].b = 0;
    RB_SET_COLORS(s, pal, 0, 5);
    for (i = 0; i < 16; i++) {
        ((unsigned char *)s->pixels)[i] = px[i];
    }
    /*
     * Palette bytes followed by pixel bytes: both must survive the round trip.
     *
     * Fix (port change): these bytes used to come from the local pal[] and px[]
     * arrays, so the dump recorded the input rather than SDL's result -- zeroing
     * every pixel in the surface left all 31 bytes identical and the fixture
     * could not detect a broken 8-bit path. Read the palette and the pixels
     * back out of the surface instead, row by row through its pitch.
     */
    {
        unsigned char buf[5 * 3 + 16];
        SDL_Palette *actual = (s->format != NULL) ? s->format->palette : NULL;
        int y;

        check(actual != NULL && actual->ncolors >= 5, "F-IDX8 surface palette");
        if (actual != NULL && actual->ncolors >= 5) {
            for (i = 0; i < 5; i++) {
                buf[i * 3] = actual->colors[i].r;
                buf[i * 3 + 1] = actual->colors[i].g;
                buf[i * 3 + 2] = actual->colors[i].b;
            }
        } else {
            memset(buf, 0xEE, 15);     /* visible as a mismatch, not as pal[] */
        }
        for (y = 0; y < 4; y++) {
            const unsigned char *row = (const unsigned char *)s->pixels + y * s->pitch;
            memcpy(buf + 15 + y * 4, row, 4);
        }
        write_raw(dir, "F-IDX8.raw", buf, sizeof(buf));
    }
    SDL_FreeSurface(s);
}

/* F-PADX: 5x3 RGB565, an odd width whose pitch is padded to a 4-byte boundary.
 * The SDL1 and SDL2 builds must agree on the padding, row by row. */
static void fixture_padx(const char *dir)
{
    SDL_Surface *s;
    int x, y;

    s = SDL_CreateRGBSurface(0, 5, 3, 16, 0xF800, 0x07E0, 0x001F, 0);
    check(s != NULL, "F-PADX surface");
    if (s == NULL) {
        return;
    }
    for (y = 0; y < 3; y++) {
        unsigned short *row = (unsigned short *)((char *)s->pixels + y * s->pitch);
        for (x = 0; x < 5; x++) {
            row[x] = (unsigned short)(0x1000 * (x + 1) + 0x0100 * (y + 1));
        }
    }
    /* pitch + row bytes: the pitch itself is part of the contract */
    {
        char header[64];
        int n = snprintf(header, sizeof(header), "pitch=%d w=%d h=%d\n", s->pitch, s->w, s->h);
        FILE *f;
        char path[512];

        snprintf(path, sizeof(path), "%s/F-PADX.raw", dir);
        f = fopen(path, "wb");
        check(f != NULL, path);
        if (f != NULL) {
            check(fwrite(header, 1, (size_t)n, f) == (size_t)n, path);
            check(fwrite(s->pixels, 1, (size_t)s->pitch * 3, f) == (size_t)s->pitch * 3, path);
            fclose(f);
        }
    }
    SDL_FreeSurface(s);
}

/* F-QUAD + F-CORNER: coordinate-labelled quad placeholder. The real corner
 * markers and checkerboard need the engine's blitters; this records the
 * geometry contract (game-size coordinates stay put) as a text fixture. */
static void fixture_quad(const char *dir)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "game=640x480 quad=(0,0)-(319,239) center=(160,120)\n"
                     "corners=8x8 markers at (0,0),(312,0),(0,232),(312,232)\n");
    write_raw(dir, "F-QUAD.txt", buf, (size_t)n);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    FILE *cf;
    char cpath[512];

    fixture_ramp565(dir);
    fixture_idx8(dir);
    fixture_padx(dir);
    fixture_quad(dir);

    /* counters.json: no engine run here, so all zero -- the schema the
     * diagnostic builds fill in (draw, upload, present, swap, create,
     * destroy, fallback). */
    snprintf(cpath, sizeof(cpath), "%s/counters.json", dir);
    cf = fopen(cpath, "w");
    check(cf != NULL, cpath);
    if (cf != NULL) {
        fprintf(cf, "{\n"
                    "  \"draw\": 0,\n"
                    "  \"upload\": 0,\n"
                    "  \"present\": 0,\n"
                    "  \"swap\": 0,\n"
                    "  \"resource_create\": 0,\n"
                    "  \"resource_destroy\": 0,\n"
                    "  \"fallback\": 0\n"
                    "}\n");
        fclose(cf);
    }

    if (failures != 0) {
        printf("render-baseline: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-baseline: fixtures written to %s\n", dir);
    return 0;
}
