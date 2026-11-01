/*
 * Host test for the SDL2 shim's pure functions: key translation both ways and
 * zoomSurface's palette handling. Built and run by tools/test_sdl2shim.sh, not
 * part of the engine.
 *
 * These are the parts of src/sdl2compat.c that need no window, so they can be
 * checked without a display. The engine must still be run for anything that
 * touches video or audio.
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>

/* Only the declarations, not the engine's headers: this test links against the
 * shim and SDL2, not the engine. */
int Sdl2_EngineKey(int sdl_keycode);

/* The shim logs through the engine's logger; this test is not the engine. */
void Log_Printf(const char *fmt, ...);
SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key);
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);

#define ENGINE_KEY_COUNT 0x200   /* sizeof(g_keyState) - 1, events.h */

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* SDL 1.2's numbers, which are what the engine indexes with. */
static void test_translation(void)
{
    struct {
        const char *name;
        int sdl2;
        int engine;
    } cases[] = {
        {"SDLK_UP", SDLK_UP, 273},
        {"SDLK_DOWN", SDLK_DOWN, 274},
        {"SDLK_RIGHT", SDLK_RIGHT, 275},
        {"SDLK_LEFT", SDLK_LEFT, 276},
        {"SDLK_F1", SDLK_F1, 282},
        {"SDLK_F12", SDLK_F12, 293},
        {"SDLK_LCTRL", SDLK_LCTRL, 306},
        {"SDLK_RCTRL", SDLK_RCTRL, 305},
        {"SDLK_LALT", SDLK_LALT, 308},
        {"SDLK_RALT", SDLK_RALT, 307},
        {"SDLK_LSHIFT", SDLK_LSHIFT, 304},
        {"SDLK_PAUSE", SDLK_PAUSE, 19},
        {"SDLK_HOME", SDLK_HOME, 278},
        {"SDLK_END", SDLK_END, 279},
        {"SDLK_INSERT", SDLK_INSERT, 277},
        {"SDLK_DELETE", SDLK_DELETE, 127},
        {"SDLK_ESCAPE", SDLK_ESCAPE, 27},
        {"SDLK_RETURN", SDLK_RETURN, 13},
        {"SDLK_SPACE", SDLK_SPACE, 32},
        {"SDLK_a", SDLK_a, 'a'},
        {"SDLK_z", SDLK_z, 'z'},
        {"SDLK_COMMA", SDLK_COMMA, ','},
        {"SDLK_QUOTE", SDLK_QUOTE, '\''},
        {"SDLK_KP_ENTER", SDLK_KP_ENTER, 271},
        {"SDLK_CAPSLOCK", SDLK_CAPSLOCK, 301},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = Sdl2_EngineKey(cases[i].sdl2);
        char msg[128];

        snprintf(msg, sizeof(msg), "%s translates to %d, got %d", cases[i].name, cases[i].engine, got);
        check(got == cases[i].engine, msg);
        /* And back again, which is what keyboard.c's focus resync needs. */
        snprintf(msg, sizeof(msg), "%s reverse-translates to 0x%X, got 0x%X", cases[i].name, (unsigned)cases[i].sdl2,
                 (unsigned)Sdl2_SdlKeyForEngineKey(cases[i].engine));
        check(Sdl2_SdlKeyForEngineKey(cases[i].engine) == cases[i].sdl2, msg);
    }

    /* Every translation must be indexable in g_keyState[0x201]. */
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char msg[128];
        int engine = Sdl2_EngineKey(cases[i].sdl2);

        snprintf(msg, sizeof(msg), "%s -> %d is outside the engine key space", cases[i].name, engine);
        check(engine >= 0 && engine < ENGINE_KEY_COUNT, msg);
    }

    /*
     * The property that matters for memory safety: no SDL2 keycode whatsoever
     * may translate to something the engine would index g_keyState[0x201]
     * with. Sweep every 32-bit keycode SDL2 can produce -- 1..SDL_NUM_SCANCODES
     * scancodes (both the masked and the plain form) plus all of keycode space
     * -- and require the result to be 0 or inside 0x201.
     */
    {
        unsigned int k;

        /* The full 32-bit space, sampled: a stride keeps the test instant while
         * still crossing every boundary the table has. */
        for (k = 0; k < 0xFFFFFFFFu; k += 0x10001u) {
            int engine = Sdl2_EngineKey((int)k);

            if (!(engine == 0 || (engine >= 0 && engine < ENGINE_KEY_COUNT))) {
                printf("FAIL: SDL2 keycode 0x%X translated to out-of-range engine key %d\n", k, engine);
                failures++;
                return;
            }
        }
        /* Also the dense low range and every scancode-derived keycode. */
        for (k = 0; k < 0x2000u; k++) {
            int engine = Sdl2_EngineKey((int)k);

            if (!(engine == 0 || (engine >= 0 && engine < ENGINE_KEY_COUNT))) {
                printf("FAIL: SDL2 keycode 0x%X translated to out-of-range engine key %d\n", k, engine);
                failures++;
                return;
            }
        }
        for (k = 0; k < SDL_NUM_SCANCODES; k++) {
            int engine = Sdl2_EngineKey((int)((k | SDLK_SCANCODE_MASK)));

            if (!(engine == 0 || (engine >= 0 && engine < ENGINE_KEY_COUNT))) {
                printf("FAIL: scancode %u keycode translated to out-of-range engine key %d\n", k, engine);
                failures++;
                return;
            }
        }
    }

    /* A key SDL 1.2 did not have is reported as 0 ("ignore"), not as a bogus
     * index. SDLK_LGUI arrived with SDL2. */
    check(Sdl2_EngineKey(SDLK_LGUI) == 0, "SDLK_LGUI has no SDL 1.2 number and must translate to 0");
    check(Sdl2_EngineKey(SDLK_PRINTSCREEN) == 0, "SDLK_PRINTSCREEN has no SDL 1.2 number");
    check(Sdl2_SdlKeyForEngineKey(0) == SDLK_UNKNOWN, "engine key 0 has no SDL2 key");
}

/*
 * zoomSurface must keep an indexed surface's palette. TTF_RenderUTF8_Solid
 * returns an 8-bit palettised surface whose entry 1 is the text colour; before
 * the fix the scaled copy got SDL's default grey ramp, so scaled TrueType text
 * changed colour.
 */
static void test_zoom_palette(void)
{
    SDL_Surface *src;
    SDL_Surface *dst;
    SDL_Color colors[2];
    Uint8 text_index;

    src = SDL_CreateRGBSurfaceWithFormat(0, 4, 4, 8, SDL_PIXELFORMAT_INDEX8);
    if (src == NULL) {
        printf("SKIP: no 8-bit surface support (%s)\n", SDL_GetError());
        return;
    }
    colors[0].r = 0;
    colors[0].g = 0;
    colors[0].b = 0;
    colors[0].a = 255;
    colors[1].r = 255;              /* the "text colour" */
    colors[1].g = 128;
    colors[1].b = 16;
    colors[1].a = 255;
    SDL_SetPaletteColors(src->format->palette, colors, 0, 2);
    /* Mark entry 0 as the colour key, the way TTF_RenderUTF8_Solid does. */
    memset(src->pixels, 0, (size_t)src->pitch * 4);
    SDL_SetColorKey(src, SDL_TRUE, 0);

    dst = zoomSurface(src, 2.0, 2.0, 0);
    check(dst != NULL, "zoomSurface returned NULL for an indexed surface");
    if (dst != NULL) {
        check(dst->w == 8 && dst->h == 8, "zoomSurface doubled the size");
        check(dst->format->BitsPerPixel == 8, "zoomSurface kept the 8-bit format");
        check(dst->format->palette != NULL, "the destination has a palette");
        if (dst->format->palette != NULL) {
            check(dst->format->palette->colors[1].r == colors[1].r,
                  "zoomed surface kept palette entry 1 red");
            check(dst->format->palette->colors[1].g == colors[1].g,
                  "zoomed surface kept palette entry 1 green");
            check(dst->format->palette->colors[1].b == colors[1].b,
                  "zoomed surface kept palette entry 1 blue");
        }
        /* The colour key has to survive too, or the background stops being
         * transparent. */
        if (SDL_GetColorKey(dst, (Uint32 *)&text_index) != 0) {
            check(0, "zoomed surface lost its colour key");
        }
        SDL_FreeSurface(dst);
    }
    SDL_FreeSurface(src);
}

void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

int main(void)
{
    if (SDL_Init(0) != 0) {
        printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    test_translation();
    test_zoom_palette();
    SDL_Quit();
    if (failures == 0) {
        printf("ok: sdl2 shim tests passed\n");
    }
    return failures != 0;
}
