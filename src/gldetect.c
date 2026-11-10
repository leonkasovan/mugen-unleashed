/*
 * Detection of the OpenGL offsets (original module 0x48DED0-0x48EE6F),
 * run by the renderer at start-up (Gl_DetectOffsets): which sub-pixel
 * offsets make primitives and true colour sprites hit exact pixels, and
 * which palette lookup the indexed sprite shader needs.
 *
 * Primitives: lines drawn on every other row must light exactly those
 * rows. Sprites: a checkerboard sprite drawn full screen and as a quarter
 * (flipped and not) must give only black or white pixels. Each list of
 * candidate offsets ends with -FLT_MAX; when none works the offsets are
 * reset and the failure is logged. Palette lookup: a 16x16 sprite of every
 * index drawn through a test shader must come out as the grey of that index
 * (the standard lookup, then the GeForce 7 one, else "safe").
 *
 * GL 1.1 entry points come from opengl32, the shader ones from mugen.exe's
 * GLEW copy (g_glew*), as in glrender.c.
 */
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#endif
/*
 * No <GL/gl.h> here: glad.h (from mugen.h) provides the whole GL API including
 * the fixed-function entry points this file uses, and glad.h refuses to load if
 * a GL header came first.
 */

#include "mugen.h"
#include "gl_size.h"

#define GLD_TEXTURE0        0x84C0
#define GLD_TEXTURE1        0x84C1
#define GLD_FRAGMENT_SHADER 0x8B30
#define GLD_VERTEX_SHADER   0x8B31

/* End of the candidate offset lists. */
#define GLD_END_OF_LIST     (-3.4028234663852886e38f)

static const char gld_standard_shader[] =
    "#version 120\n"
    "uniform sampler2D texunit0;\n"
    "uniform sampler1D texunit1;\n"
    "void main()\n"
    "{\n"
    "  vec4 idxv = texture2D(texunit0, gl_TexCoord[0].st);\n"
    "  float idx = idxv.r;\n"
    "  vec4 color = texture1D(texunit1, idx);\n"
    "  gl_FragColor = color;\n"
    "}\n";

static const char gld_geforce7_shader[] =
    "#version 120\n"
    "uniform sampler2D texunit0;\n"
    "uniform sampler1D texunit1;\n"
    "void main()\n"
    "{\n"
    "  vec4 idxv = texture2D(texunit0, gl_TexCoord[0].st);\n"
    "  float idx = idxv.r;\n"
    "  vec4 color = texture1D(texunit1, idx * 255.99 / 256.0);\n"
    "  gl_FragColor = color;\n"
    "}\n";

/* White in the render target's format (the original takes the target's surface without checking it). */
static unsigned int GlDetect_White(void)
{
    const Sprite *t;
    SDL_Surface *surface;

    if (g_renderMode == RENDER_MODE_GL || g_renderMode == RENDER_MODE_GL_ALT) {
        return 0xFFFFFF;
    }
    t = (const Sprite *)(g_renderTarget->sprites->data + g_renderTarget->sprites->elem_size * g_renderTargetSprite);
    surface = t != NULL && t->format == SPRITE_FORMAT_SURFACE ? t->pixels : NULL;
    return SDL_MapRGB(surface->format, 0xFF, 0xFF, 0xFF);
}

/* 0x0048DED0: draws to the screen, orthographic, cleared to black, unclipped. */
void GlDetect_Clear(void)
{
    Gfx_SetRenderTarget(NULL, 0);
    g_glProjection = 0;
    Gl_SetupView();
    glDisable(GL_LIGHTING);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Gfx_SetClipRect(0, 0, g_targetWidth, g_targetHeight);
}

/*
 * The rows from first, every other one, must be white and the others black
 * in pixels (g_targetWidth x g_targetHeight RGBA).
 */
static int GlDetect_CheckRows(const unsigned int *pixels, int white_first)
{
    int y, x;

    for (y = white_first; y < g_targetHeight; y += 2) {
        for (x = 0; x < g_targetWidth; x++) {
            if (pixels[y * g_targetWidth + x] != 0xFFFFFFFFu) {
                return 0;
            }
        }
    }
    for (y = !white_first; y < g_targetHeight; y += 2) {
        for (x = 0; x < g_targetWidth; x++) {
            if (pixels[y * g_targetWidth + x] != 0xFF000000u) {
                return 0;
            }
        }
    }
    return 1;
}

/* Draws white lines on the rows from first, every other one, and checks the screen. */
static int GlDetect_TestLines(int first)
{
    unsigned int white = GlDetect_White();
    unsigned char *pixels;
    int y, ok;

    GlDetect_Clear();
    for (y = first; y < g_targetHeight; y += 2) {
        float fy = (float)y;
        Gfx_GlDrawLine(-10.0f, fy, (float)(g_targetWidth + 10), fy, white);
    }
    pixels = Gl_ReadPixels(g_targetWidth, g_targetHeight);
    if (pixels == NULL) {
        return 0;
    }
    ok = GlDetect_CheckRows((const unsigned int *)pixels, first);
    free(pixels);
    return ok;
}

/* 0x0048DF40: lines on the even rows hit exactly those rows. */
int GlDetect_TestLinesEven(void)
{
    return GlDetect_TestLines(0);
}

/* 0x0048E0E0: lines on the odd rows hit exactly those rows. */
int GlDetect_TestLinesOdd(void)
{
    return GlDetect_TestLines(1);
}

/*
 * 0x0048E280: the first offset of g_glPalOffset, 0.325, 0 with which both
 * line tests pass becomes g_glOffsetX/Y. Returns 0 (offsets reset to
 * g_glPalOffset, logged) when none works.
 */
int GlDetect_PrimitivesOffset(void)
{
    float candidates[4];
    int i = 0;

    candidates[0] = g_glPalOffset;
    candidates[1] = 0.325f;
    candidates[2] = 0.0f;
    candidates[3] = GLD_END_OF_LIST;
    for (;;) {
        int fails = 0;
        g_glOffsetY = candidates[i];
        g_glOffsetX = g_glOffsetY;
        if (!GlDetect_TestLinesEven()) {
            fails = 1;
        }
        if (!GlDetect_TestLinesOdd()) {
            fails++;
        }
        if (fails == 0) {
            return 1;
        }
        i++;
        if ((double)candidates[i] == -3.4028234663852886e38) {
            break;
        }
    }
    g_glOffsetY = g_glPalOffset;
    g_glOffsetX = g_glPalOffset;
    MatchLog_Printf(5, "Failed to detect primitives offset\n");
    return 0;
}

/* The draw parameters of the test sprites: at (x, y), unscaled, flipped by flip. */
static void GlDetect_DrawParams(DrawParams *dp, float x, float y, unsigned int flip)
{
    memset(dp, 0, sizeof(DrawParams));
    dp->scale2_y = 1.0f;
    dp->scale2_x = 1.0f;
    dp->scale_y = 1.0f;
    dp->scale_x = 1.0f;
    dp->alpha_src = 1.0f;
    dp->flags = 0xFFFF;
    dp->bottom_stretch = 1.0f;
    dp->masked = 1;
    dp->top_stretch = 1.0f;
    dp->flip = flip;
    dp->width_ratio = 1.0f;
    dp->width_anchor = 0.5f;
    dp->x = x;
    dp->y = y;
}

/* Port fix: calibration owns its pixels on every path, including allocation
 * failure. Do not use SpriteSet_FromPixels' legacy failure-path leaks here. */
static SpriteSet *GlDetect_MakeSprite(void *pixels, size_t bytes, int w, int h,
                                     int bpp, Palette *pal)
{
    SpriteSet *set = calloc(1, sizeof(*set));
    Sprite spr;
    if (set == NULL) { free(pixels); return NULL; }
    if (!Sprite_Init(SPRITE_INIT_OWN, &spr, SPRITE_FORMAT_RAW, pixels, bytes, w, h, bpp, 0, 0)) {
        SpriteSet_Free(set);
        return NULL; /* Sprite_Init consumed pixels, including on failure. */
    }
    if (pal != NULL) {
        if (!SpriteSet_AddPalette(set, pal, 0, 0)) goto fail;
        memset(pal, 0, sizeof(*pal)); /* Ownership moved into the set. */
        if (!SpriteSet_LinkPalette(set, &spr, 0, 0)) goto fail;
    }
    if (!SpriteSet_AddSprite(set, &spr, 0, 0)) goto fail;
    return set;
fail:
    Sprite_Free(&spr);
    SpriteSet_Free(set);
    return NULL;
}

/* 0x0048E330: draw a checkerboard, not just an arbitrary binary image.
 * Port fix: checked sizes, complete ownership cleanup and reject blank output.
 * The original black/white edge tolerance is retained; this is not a geometry
 * equivalence test. Both checker colours must actually have been rendered. */
int GlDetect_TestSprite(int w, int h, int x, int y, int flip)
{
    size_t size;
    unsigned int *pixels, *screen;
    SpriteSet *set;
    DrawParams dp;
    int row, col, black = 0, white = 0, ok = 1;

    if (!GlImage_Size(w, h, 4, NULL, &size)) return 0;
    GlDetect_Clear();
    pixels = malloc(size);
    if (pixels == NULL) return 0;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            pixels[row * w + col] = (x + y + row + col) % 2 != 0 ? 0xFF000000u : 0xFFFFFFFFu;
        }
    }
    set = GlDetect_MakeSprite(pixels, size, w, h, 32, NULL);
    if (set == NULL) return 0;
    GlDetect_DrawParams(&dp, (float)x, (float)y, flip);
    if (g_renderMode == RENDER_MODE_GL) {
        /* Calibration runs before the sprite shaders are loaded. Its fixed
         * function fallback must sample the texture, not draw a solid quad. */
        g_glewActiveTexture(GLD_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        Sprite_DrawGL(set, 0, &dp);
        glDisable(GL_TEXTURE_2D);
    } else {
        Sprite_DrawSoft(set, 0, &dp);
    }
    screen = (unsigned int *)Gl_ReadPixels(g_targetWidth, g_targetHeight);
    if (screen == NULL) { SpriteSet_Free(set); return 0; }
    for (row = 0; row < g_targetHeight; row++) {
        for (col = 0; col < g_targetWidth; col++) {
            unsigned int p = screen[row * g_targetWidth + col];
            if (p == 0xFFFFFFFFu) white = 1;
            else if (p == 0xFF000000u) black = 1;
            else ok = 0;
        }
    }
    free(screen);
    SpriteSet_Free(set);
    return ok && black && white;
}

/* 0x0048E600: the full screen, quarter and flipped quarter sprite tests; returns the number that failed (logged). */
int GlDetect_TestSprites(void)
{
    int fails = 0;

    if (!GlDetect_TestSprite(g_targetWidth, g_targetHeight, 0, 0, 0)) {
        MatchLog_Printf(9, "fullscreen failed ");
        fails = 1;
    }
    if (!GlDetect_TestSprite(g_targetWidth / 4, g_targetHeight / 4, g_targetWidth / 2 + 10, g_targetHeight / 2 + 10,
                             0)) {
        MatchLog_Printf(9, "quarter failed ");
        fails++;
    }
    if (!GlDetect_TestSprite(g_targetWidth / 4, g_targetHeight / 4, g_targetWidth / 2 + 10, g_targetHeight / 2 + 10,
                             DRAW_FLIP_H | DRAW_FLIP_V)) {
        MatchLog_Printf(9, "quarterflip failed ");
        fails++;
    }
    return fails;
}

/*
 * 0x0048E6F0: the first offset of 0, g_glPalOffset, 0.325 with which the
 * sprite tests pass becomes g_glRgbaOffsetX/Y (each try is logged).
 * Returns 0 (offsets reset to 0, logged) when none works.
 */
int GlDetect_SpriteOffset(void)
{
    float candidates[4];
    int i = 0;

    candidates[0] = 0.0f;
    candidates[1] = g_glPalOffset;
    candidates[2] = 0.325f;
    candidates[3] = GLD_END_OF_LIST;
    for (;;) {
        g_glRgbaOffsetY = candidates[i];
        g_glRgbaOffsetX = g_glRgbaOffsetY;
        MatchLog_Printf(9, "DetectSpriteOffset RGBA %.3f %.3f ", (double)g_glRgbaOffsetX, (double)g_glRgbaOffsetX);
        if (GlDetect_TestSprites() == 0) {
            MatchLog_Printf(9, "passed\n");
            return 1;
        }
        MatchLog_Printf(9, "\n");
        i++;
        if ((double)candidates[i] == -3.4028234663852886e38) {
            break;
        }
    }
    g_glRgbaOffsetY = 0.0f;
    g_glRgbaOffsetX = 0.0f;
    MatchLog_Printf(5, "Failed to detect sprite offset\n");
    return 0;
}

/*
 * 0x0048ECF0: draws the quad (x0, y0)-(x1, y1) with tex on unit 0 and the
 * palette texture on unit 1 through program prog (left in use only while
 * drawing).
 */
void GlDetect_DrawQuad(unsigned int prog, const GlTex *tex, const GlTex *pal_tex, float x0, float y0, float x1,
                       float y1)
{
    int loc0, loc1;

    if (g_glCurrentProgram != prog) {
        g_glewUseProgram(prog);
        g_glCurrentProgram = prog;
    }
    g_glewActiveTexture(GLD_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    GlTex_SetParameter(tex, GL_TEXTURE_WRAP_S, GL_CLAMP);
    g_glewActiveTexture(GLD_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, pal_tex->id);
    loc0 = g_glewGetUniformLocation(prog, "texunit0");
    loc1 = g_glewGetUniformLocation(prog, "texunit1");
    g_glewUniform1i(loc0, 0);
    g_glewUniform1i(loc1, 1);
    glBegin(GL_TRIANGLE_STRIP);
    g_glewMultiTexCoord2f(GLD_TEXTURE0, 0.0f, 0.0f);
    glVertex2f(x0, y0);
    g_glewMultiTexCoord2f(GLD_TEXTURE0, 1.0f, 0.0f);
    glVertex2f(x1, y0);
    g_glewMultiTexCoord2f(GLD_TEXTURE0, 0.0f, 1.0f);
    glVertex2f(x0, y1);
    g_glewMultiTexCoord2f(GLD_TEXTURE0, 1.0f, 1.0f);
    glVertex2f(x1, y1);
    glEnd();
    if (g_glCurrentProgram != 0) {
        g_glewUseProgram(0);
        g_glCurrentProgram = 0;
    }
}

/*
 * 0x0048EBF0: draws sprite 0 of set (a paletted texture sprite) through
 * program prog at dp's position, as the renderer places paletted sprites.
 * The palette is used without a NULL check.
 */
void GlDetect_DrawSprite(SpriteSet *set, const DrawParams *dp, unsigned int prog)
{
    const Sprite *spr = (const Sprite *)set->sprites->data;
    const GlTex *tex = spr->format == SPRITE_FORMAT_TEXTURE ? spr->pixels : NULL;
    int w = spr->w;
    int h = spr->h;
    Palette *pal = SpriteSet_GetPalette(set, spr->pal_index);
    const GlTex *pal_tex;

    if (pal != NULL) {
        Pal_Convert(pal);
    }
    pal_tex = pal->is_texture == 1 ? pal->data : NULL;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(g_glPalOffset, g_glPalOffset, 0.0f);
    glTranslatef(dp->x, dp->y, 0.0f);
    glTranslatef((float)-spr->x, (float)-spr->y, 0.0f);
    glDisable(GL_BLEND);
    GlDetect_DrawQuad(prog, tex, pal_tex, 0.0f, 0.0f, (float)w, (float)h);
}

/*
 * 0x0048E7C0: draws a 16x16 sprite of each index (255 to 0) with a grey
 * palette through the program of frag_src and checks its centre pixel.
 * Returns 1 when every index gives its grey (mismatches are logged), 0
 * otherwise, including compilation, allocation or readback failure.
 * Port fix: partial shader/program handles are released on every failure.
 */
int GlDetect_TestIndexShader(const char *frag_src)
{
    unsigned char colors[256 * 4];
    Palette pal;
    unsigned int vs = 0, fs = 0, prog = 0;
    GLint status = 0;
    int last, err, errors = 0;
    int i, v;

    while (glGetError() != GL_NO_ERROR) {
    }
    vs = g_glewCreateShader(GLD_VERTEX_SHADER);
    if (vs == 0) goto fail;
    g_glewShaderSource(vs, 1, (const char **)&g_glDetectVertexShader, NULL);
    g_glewCompileShader(vs);
    g_glewGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (!status) goto fail;
    fs = g_glewCreateShader(GLD_FRAGMENT_SHADER);
    if (fs == 0) goto fail;
    g_glewShaderSource(fs, 1, &frag_src, NULL);
    g_glewCompileShader(fs);
    g_glewGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (!status) goto fail;
    prog = g_glewCreateProgram();
    if (prog == 0) goto fail;
    g_glewAttachShader(prog, vs);
    g_glewAttachShader(prog, fs);
    g_glewLinkProgram(prog);
    g_glewGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) goto fail;
    err = GL_NO_ERROR;
    do {
        last = err;
        err = glGetError();
    } while (err != GL_NO_ERROR);
    if (last != GL_NO_ERROR) {
        goto fail;
    }
    for (i = 0; i < 256; i++) {
        colors[i * 4 + 2] = (unsigned char)i;
        colors[i * 4 + 1] = (unsigned char)i;
        colors[i * 4] = (unsigned char)i;
        colors[i * 4 + 3] = 0xFF;
    }
    for (v = 0xFF; v >= 0; v--) {
        unsigned char *pixels;
        unsigned int *screen;
        SpriteSet *set;
        DrawParams dp;

        if (!Pal_Init(&pal, 256, colors, PAL_COPY)) goto fail;
        GlDetect_Clear();
        pixels = malloc(0x100);
        if (pixels == NULL) { Pal_Free(&pal); goto fail; }
        memset(pixels, v, 0x100);
        set = GlDetect_MakeSprite(pixels, 0x100, 16, 16, 8, &pal);
        Pal_Free(&pal);
        if (set == NULL) goto fail;
        GlDetect_DrawParams(&dp, 0.0f, 0.0f, 0);
        GlDetect_DrawSprite(set, &dp, prog);
        screen = (unsigned int *)Gl_ReadPixels(16, 16);
        if (screen == NULL) {
            SpriteSet_Free(set);
            goto fail; /* A failed readback is not a successful comparison. */
        }
        {
            unsigned int got = screen[0x88];
            unsigned int grey;
            free(screen);
            if (g_renderMode == RENDER_MODE_GL || g_renderMode == RENDER_MODE_GL_ALT) {
                grey = ((unsigned int)v << 8 | v) << 8 | v;
            } else {
                const Sprite *t = (const Sprite *)(g_renderTarget->sprites->data +
                                                   g_renderTarget->sprites->elem_size * g_renderTargetSprite);
                SDL_Surface *surface = t != NULL && t->format == SPRITE_FORMAT_SURFACE ? t->pixels : NULL;
                grey = SDL_MapRGB(surface->format, (Uint8)v, (Uint8)v, (Uint8)v);
            }
            if (got != (grey | 0xFF000000u)) {
                MatchLog_Printf(7, "Index correction detected color %i as %i\n", v, got & 0xFF);
                errors = 1;
            }
        }
        SpriteSet_Free(set);
    }
    g_glewDeleteProgram(prog);
    g_glewDeleteShader(fs);
    g_glewDeleteShader(vs);
    if (errors) {
        MatchLog_Printf(7, "Index correction test failed\n");
    }
    return errors == 0;
fail:
    /* Port fix: compiler/link/readback failures release every partial handle. */
    if (g_glCurrentProgram == prog && prog != 0) {
        g_glewUseProgram(0); g_glCurrentProgram = 0;
    }
    if (prog != 0) g_glewDeleteProgram(prog);
    if (fs != 0) g_glewDeleteShader(fs);
    if (vs != 0) g_glewDeleteShader(vs);
    return 0;
}

/* 0x0048EB00: picks the palette lookup of the indexed sprite shaders (g_glIndexMode). */
void GlDetect_IndexCorrection(void)
{
    MatchLog_Printf(7, "Index correction standard test\n");
    if (GlDetect_TestIndexShader(gld_standard_shader)) {
        g_glIndexMode = 0;
        return;
    }
    MatchLog_Printf(7, "Index correction GeForce7 test\n");
    g_glIndexMode = GlDetect_TestIndexShader(gld_geforce7_shader) ? 1 : 2;
}

/* 0x0048EB60: detects the offsets and the palette lookup, and logs them. */
void Gl_DetectOffsets(void)
{
    const char *mode;

    Gfx_SetRenderTarget(NULL, 0);
    GlDetect_PrimitivesOffset();
    GlDetect_SpriteOffset();
    GlDetect_IndexCorrection();
    if (g_glIndexMode == 0) {
        mode = "Standard";
    } else if (g_glIndexMode == 1) {
        mode = "GeForce7";
    } else {
        mode = "Safe";
    }
    MatchLog_Printf(7, "Primitives offset: %.3f %.3f\nIndexed sprite offset: %.3f %.3f\nRGBA sprite offset: %.3f %.3f\n"
                       "Index correction: %s\n",
                    (double)g_glOffsetX, (double)g_glOffsetY, 0.375, 0.375, (double)g_glRgbaOffsetX,
                    (double)g_glRgbaOffsetY, mode);
}
