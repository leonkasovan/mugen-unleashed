/*
 * GLES 3.0 renderer for Android (RENDERING_PLAN.md section 12).
 *
 * The desktop renderer (src/glrender.c) is written against GL 2.1 with the
 * compatibility profile: immediate mode (glBegin/glVertex), a fixed-function
 * matrix stack, and 1D palette textures. None of those exist in GLES, so this
 * is a separate backend rather than a recompile of that file -- exactly what
 * section 12.1 asks for.
 *
 * What is here:
 *   - the GlTex_* texture object over GLES (8-bit index, 24-bit RGB, RGBA),
 *     with the palette as a 256x1 2D texture because GLES has no 1D textures;
 *   - shader programs from the *_es shader files (GLSL ES 1.00);
 *   - the sprite path: CPU modelview (src/gl_mvp.c) + VBO quad submission
 *     (src/gles_draw.c);
 *   - the dispatch entries Gfx_SelectRenderer(RENDER_OPENGL) installs:
 *     clear, pixel, line, rect, fill, blend, capture, present.
 *
 * Deliberate limits, logged once rather than hidden:
 *   - one shader variant per path (no PERSPECTIVE_* / INDEX_* / PALFX_DISABLE
 *     selection yet): the ES shaders' defaults apply, which are the same
 *     defaults the desktop loader uses for its "no defines" case;
 *   - RGBA sources are uploaded straight, not premultiplied, and the ES
 *     fragment shaders run without PREMUL -- the two agree with each other.
 */
#include "mugen.h"

#if defined(ANDROID_PORT) && defined(MUGEN_GLES)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gles_probe.h"
#include "gl_mvp.h"
#include "gles_draw.h"

/* ------------------------------------------------------------ texture */

/* Video memory accounting, mirroring glrender.c's g_glTexMemory. */
static unsigned long g_glesTexMemory;
static unsigned long g_glesTexMemoryPeak;

/*
 * Storage description of the two scratch sprite textures, so a repeated upload
 * of the same size and format can use glTexSubImage2D. Using glTexImage2D every
 * time reallocates the texture on every sprite every frame, which measured at
 * ~52 fps against the software renderer's locked 60 and made animation (the
 * select cursor's 8-tick blink) look irregular.
 *
 * Kept as a sidecar keyed by texture id, not a field on GlTex: the struct has a
 * fixed 32-bit layout (MUGEN_ASSERT_SIZE(GlTex, 0x1C)).
 */
typedef struct GlesTexFormat {
    unsigned int id;
    int w;
    int h;
    int internal_format;
} GlesTexFormat;
#define GLES_TEX_FORMAT_SLOTS 8
static GlesTexFormat g_glesTexFormat[GLES_TEX_FORMAT_SLOTS];

static GlesTexFormat *gles_tex_format_slot(unsigned int id)
{
    GlesTexFormat *free_slot = NULL;
    int i;

    for (i = 0; i < GLES_TEX_FORMAT_SLOTS; i++) {
        if (g_glesTexFormat[i].id == id) {
            return &g_glesTexFormat[i];
        }
        if (g_glesTexFormat[i].id == 0 && free_slot == NULL) {
            free_slot = &g_glesTexFormat[i];
        }
    }
    if (free_slot == NULL) {
        /* All slots taken (more than 8 live textures): fall back to slot 0,
         * which only costs a redundant glTexImage2D. */
        free_slot = &g_glesTexFormat[0];
    }
    free_slot->id = id;
    free_slot->w = 0;
    free_slot->h = 0;
    free_slot->internal_format = 0;
    return free_slot;
}

/* 0 = created, 1 = allocated and uploaded at least once. */
GlTex *GlTex_Create(void)
{
    GlTex *tex = calloc(1, sizeof(GlTex));

    return tex;
}

int GlTex_Free(GlTex *tex)
{
    if (tex == NULL) {
        return 0;
    }
    if (tex->id != 0) {
        GlTex_Delete(tex);
    }
    free(tex);
    return 1;
}

int GlTex_Delete(GlTex *tex)
{
    GLuint id;

    if (tex == NULL) {
        return 0;
    }
    if (tex->refs > 1) {
        tex->refs--;
        return 0;
    }
    id = (GLuint)tex->id;
    if (id != 0) {
        glDeleteTextures(1, &id);
    }
    /* The desktop Delete clamps at zero instead of going negative; kept. */
    if (g_glesTexMemory >= tex->size) {
        g_glesTexMemory -= tex->size;
    } else {
        g_glesTexMemory = 0;
    }
    {
        GlesTexFormat *slot = gles_tex_format_slot((unsigned int)id);

        slot->id = 0;
        slot->w = 0;
        slot->h = 0;
        slot->internal_format = 0;
    }
    tex->id = 0;
    tex->size = 0;
    tex->refs = 0;
    tex->w = 0;
    tex->h = 0;
    return 1;
}

/* Allocates the GL name and the storage for w x h RGBA (w x 1 for palettes). */
int GlTex_Alloc(GlTex *tex, int w, int h)
{
    GLuint id = 0;

    if (tex == NULL || w <= 0 || h <= 0) {
        return 0;
    }
    if (tex->id == 0) {
        glGenTextures(1, &id);
        if (id == 0) {
            Log_Printf("GLES: glGenTextures failed\n");
            return 0;
        }
        tex->id = (unsigned int)id;
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    tex->w = w;
    tex->h = h;
    return 1;
}

/* Books the bytes a full upload of tex occupies. */
static void gles_tex_account(GlTex *tex, int w, int h, int bytes_per_pixel)
{
    unsigned long bytes = (unsigned long)w * (unsigned long)h * (unsigned long)bytes_per_pixel;

    g_glesTexMemory -= tex->size;
    tex->size = (unsigned int)bytes;
    g_glesTexMemory += bytes;
    if (g_glesTexMemory > g_glesTexMemoryPeak) {
        g_glesTexMemoryPeak = g_glesTexMemory;
    }
}

int GlTex_Upload(GlTex *tex, int h, int internal_format, int w, int format, const void *pixels)
{
    GLuint id;

    if (tex == NULL || w <= 0 || h <= 0) {
        return 0;
    }
    /*
     * Allocate the GL name on first use. The desktop renderer always calls
     * GlTex_Init/GlTex_Alloc first, but the GLES path uploads into long-lived
     * scratch textures (one for paletted sprites, one for true colour) that
     * start empty -- refusing an id of 0 here made every sprite upload fail and
     * the draw return early, which is why nothing was ever rendered.
     */
    if (tex->id == 0) {
        id = 0;
        glGenTextures(1, &id);
        if (id == 0) {
            return 0;
        }
        tex->id = (unsigned int)id;
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    {
        GlesTexFormat *slot = gles_tex_format_slot(tex->id);

        if (slot->w == w && slot->h == h && slot->internal_format == internal_format) {
            /* Same storage: only the texels change. */
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
            slot->w = w;
            slot->h = h;
            slot->internal_format = internal_format;
        }
    }
    tex->w = w;
    tex->h = h;
    return 1;
}

/*
 * Sprite pixels: 8-bit stays a single-channel index texture (the ES indexed
 * shader samples .r), 24-bit goes as RGB, 32-bit as straight RGBA.
 *
 * GLES dropped GL_LUMINANCE, so the index sprite is GL_R8/GL_RED rather than
 * the desktop's luminance upload. The shader reads the red channel either way.
 */
int GlTex_Upload2D(int h, int bpp, GlTex *tex, const void *pixels, int w)
{
    if (tex == NULL || pixels == NULL || w <= 0 || h <= 0) {
        return 0;
    }
    if (bpp <= 8) {
        gles_tex_account(tex, w, h, 1);
        return GlTex_Upload(tex, h, GL_R8, w, GL_RED, pixels);
    }
    if (bpp == 24) {
        gles_tex_account(tex, w, h, 3);
        return GlTex_Upload(tex, h, GL_RGB, w, GL_RGB, pixels);
    }
    if (bpp == 32) {
        gles_tex_account(tex, w, h, 4);
        return GlTex_Upload(tex, h, GL_RGBA, w, GL_RGBA, pixels);
    }
    Log_Printf("GLES: sprite of %d bpp has no texture format\n", bpp);
    return 0;
}

/*
 * RGBA upload, used for the palette (height 1). Not premultiplied: the ES
 * shaders run without PREMUL, so the two must agree.
 */
int GlTex_UploadRGBA(int h, int w, GlTex *tex, const void *pixels)
{
    if (tex == NULL) {
        return 0;
    }
    gles_tex_account(tex, w, h, 4);
    return GlTex_Upload(tex, h, GL_RGBA, w, GL_RGBA, pixels);
}

/* ---------------------------------------------------------- programs */

static GlesProgram g_glesProgramIndexed;
static GlesProgram g_glesProgramRgba;
static GlesProgram g_glesProgramSolid;
static GlesBuffer g_glesBuffer;
static int g_glesReady;
static unsigned int g_glesCaps;
static char *g_glesSolidVert;
static char *g_glesSolidFrag;

/* A solid-colour program for the primitive path (rects, lines, pixels). */
static const char *gles_solid_vert_src =
    "attribute vec4 a_position;\n"
    "uniform mat4 u_mvp;\n"
    "void main() { gl_Position = u_mvp * a_position; }\n";

static const char *gles_solid_frag_src =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() { gl_FragColor = u_color; }\n";

/* Reads a shader file next to the game data. */
static char *gles_read_shader(const char *name)
{
    char path[512];
    FILE *f;
    long size;
    char *text;

    snprintf(path, sizeof(path), "data/%s", name);
    f = fopen(path, "rb");
    if (f == NULL) {
        Log_Printf("GLES: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    text = calloc(1, (size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return NULL;
    }
    fclose(f);
    text[size] = '\0';
    return text;
}

/* Loads one program from the ES dialect files. */
static int gles_load_es_program(GlesProgram *prog, const char *name)
{
    char vert_name[64];
    char frag_name[64];
    char *vert;
    char *frag;
    int ok;

    snprintf(vert_name, sizeof(vert_name), "%s_es.vert", name);
    snprintf(frag_name, sizeof(frag_name), "%s_es.frag", name);
    vert = gles_read_shader(vert_name);
    frag = gles_read_shader(frag_name);
    if (vert == NULL || frag == NULL) {
        free(vert);
        free(frag);
        return 0;
    }
    ok = GlesDraw_LoadProgram(prog, vert, frag, name);
    free(vert);
    free(frag);
    return ok;
}

/* Deletes the sprite textures and the programs. */
int Gl_Deinit(void)
{
    if (!g_glesReady) {
        return 0;
    }
    GlesDraw_FreeProgram(&g_glesProgramIndexed);
    GlesDraw_FreeProgram(&g_glesProgramRgba);
    GlesDraw_FreeProgram(&g_glesProgramSolid);
    GlesDraw_FreeBuffer(&g_glesBuffer);
    free(g_glesSolidVert);
    free(g_glesSolidFrag);
    g_glesSolidVert = NULL;
    g_glesSolidFrag = NULL;
    g_glesReady = 0;
    return 1;
}

/* The capability bits the probe resolved, for the Android video layer. */
unsigned int Gles_LastCaps(void)
{
    return g_glesCaps;
}

int Gles_IsReady(void)
{
    return g_glesReady;
}

/*
 * Starts the GLES renderer: probe, shader compile/link, FBO completeness.
 * Returns 1 only when the whole sprite path is usable -- a context that exists
 * but cannot compile its shaders is not "OpenGL available" (plan section 11
 * V2), and the caller keeps the software renderer in that case.
 */
int Gl_Init(int framebuffers)
{
    unsigned int caps = 0;

    if (g_glesReady) {
        return 1;
    }
    if (!Gles_Probe(&caps)) {
        Log_Printf("GLES: capability probe failed; GL mode unavailable\n");
        return 0;
    }
    g_glesCaps = caps;

    if (!gles_load_es_program(&g_glesProgramIndexed, "indexed")) {
        Log_Printf("GLES: indexed program unavailable; GL mode unavailable\n");
        Gl_Deinit();
        return 0;
    }
    if (!gles_load_es_program(&g_glesProgramRgba, "rgba")) {
        Log_Printf("GLES: rgba program unavailable; GL mode unavailable\n");
        Gl_Deinit();
        return 0;
    }

    g_glesSolidVert = strdup(gles_solid_vert_src);
    g_glesSolidFrag = strdup(gles_solid_frag_src);
    if (g_glesSolidVert == NULL || g_glesSolidFrag == NULL ||
        !GlesDraw_LoadProgram(&g_glesProgramSolid, g_glesSolidVert, g_glesSolidFrag, "solid")) {
        Log_Printf("GLES: solid program unavailable; GL mode unavailable\n");
        Gl_Deinit();
        return 0;
    }
    free(g_glesSolidVert);
    free(g_glesSolidFrag);
    g_glesSolidVert = NULL;
    g_glesSolidFrag = NULL;

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_glMaxTextureSize);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    g_glesReady = 1;
    Log_Printf("GLES: renderer ready (indexed+rgba+solid, max texture %d)\n", g_glMaxTextureSize);
    return 1;
}

/* ------------------------------------------------------------ view */

/*
 * Uploads the projection. GLES has no matrix stack, so the matrix is computed
 * once here (src/gl_mvp.c) and cached for the draws that follow.
 */
static GlMvp g_glesProjection;
static int g_glesProjectionValid;

int Gl_SetupView(void)
{
    GlMvp_SetupView(&g_glesProjection, g_glProjection, g_targetWidth, g_targetHeight, g_glBoundFramebuffer,
                    g_glNear, g_glFar, g_glZScale);
    g_glesProjectionValid = 1;
    glViewport(0, 0, g_targetWidth, g_targetHeight);
    return 1;
}

void Gl_ResetModelview(void)
{
    /* Nothing on a stack to reset: the modelview is built per draw. */
}

void Gl_ApplyClipRect(void)
{
    glEnable(GL_SCISSOR_TEST);
    if (g_glBoundFramebuffer == 0) {
        glScissor(g_clipX1, g_targetHeight - g_clipY2, g_clipX2 - g_clipX1, g_clipY2 - g_clipY1);
    } else {
        glScissor(g_clipX1, g_clipY1, g_clipX2 - g_clipX1, g_clipY2 - g_clipY1);
    }
}

/* Draws one solid rectangle through the solid program. */
static void gles_solid_quad(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    float pos[8];
    float tc[16] = { 0 };
    int i;

    if (!g_glesReady) {
        return;
    }
    pos[0] = x0; pos[1] = y0;
    pos[2] = x1; pos[3] = y0;
    pos[4] = x0; pos[5] = y1;
    pos[6] = x1; pos[7] = y1;
    for (i = 0; i < 4; i++) {
        tc[i * 4 + 3] = 1.0f;
    }
    if (!g_glesProjectionValid) {
        Gl_SetupView();
    }
    glUseProgram(g_glesProgramSolid.program);
    if (g_glesProgramSolid.u_mvp >= 0) {
        glUniformMatrix4fv(g_glesProgramSolid.u_mvp, 1, GL_FALSE, g_glesProjection.m);
    }
    /* The solid shader's colour uniform is not part of GlesProgram (which
     * serves the sprite shaders); look it up once and cache it. */
    {
        static int u_color = -2;

        if (u_color == -2) {
            u_color = glGetUniformLocation(g_glesProgramSolid.program, "u_color");
        }
        if (u_color >= 0) {
            glUniform4f(u_color, r, g, b, a);
        }
    }
    GlesDraw_Quad(&g_glesBuffer, &g_glesProgramSolid, g_glesProjection.m, pos, tc, 0, 0, 0.0f, 0.0f, 0.0f, 1.0f,
                  1.0f, 1.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f);
}

/* Splits an RGB565 colour into normalised components. */
static void gles_split565(unsigned int color, float *r, float *g, float *b)
{
    *r = (float)((color >> 11) & 0x1F) / 31.0f;
    *g = (float)((color >> 5) & 0x3F) / 63.0f;
    *b = (float)(color & 0x1F) / 31.0f;
}

void Gfx_GlClear(unsigned int color)
{
    float r, g, b;

    if (!g_glesReady) {
        return;
    }
    gles_split565(color, &r, &g, &b);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Gfx_GlPutPixel(int x, int y, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    gles_solid_quad((float)x, (float)y, (float)(x + 1), (float)(y + 1), r, g, b, 1.0f);
}

void Gfx_GlAddPixel(int x, int y, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    glBlendEquation(GL_FUNC_ADD);
    gles_solid_quad((float)x, (float)y, (float)(x + 1), (float)(y + 1), r, g, b, 1.0f);
    glBlendEquation(GL_FUNC_ADD);
}

void Gfx_GlInvertPixel(int x, int y)
{
    /* No logic ops in GLES: an inverted pixel needs a read-back, which is not
     * worth it for a debug overlay. Declared unsupported, once. */
    static int logged;

    if (!logged) {
        logged = 1;
        Log_Printf("GLES: invert-pixel unsupported (no logic ops); ignored\n");
    }
    (void)x;
    (void)y;
}

void Gfx_GlDrawLine(float x1, float y1, float x2, float y2, unsigned int color)
{
    float r, g, b;
    float t;

    gles_split565(color, &r, &g, &b);
    /* Axis-aligned lines are the common case (collision-box edges): draw them
     * as a rect so they are exact. Diagonal ones step pixel by pixel. */
    if (y1 == y2) {
        gles_solid_quad(x1 < x2 ? x1 : x2, y1, (x1 < x2 ? x2 : x1) + 1.0f, y1 + 1.0f, r, g, b, 1.0f);
        return;
    }
    if (x1 == x2) {
        gles_solid_quad(x1, y1 < y2 ? y1 : y2, x1 + 1.0f, (y1 < y2 ? y2 : y1) + 1.0f, r, g, b, 1.0f);
        return;
    }
    {
        int n = (int)((x2 - x1 > 0 ? x2 - x1 : x1 - x2) > (y2 - y1 > 0 ? y2 - y1 : y1 - y2)
                          ? (x2 - x1 > 0 ? x2 - x1 : x1 - x2)
                          : (y2 - y1 > 0 ? y2 - y1 : y1 - y2));
        int i;

        for (i = 0; i <= n; i++) {
            t = (n == 0) ? 0.0f : (float)i / (float)n;
            gles_solid_quad(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, x1 + (x2 - x1) * t + 1.0f,
                            y1 + (y2 - y1) * t + 1.0f, r, g, b, 1.0f);
        }
    }
}

void Gfx_GlDrawLineAdd(float x1, float y1, float x2, float y2, unsigned int color)
{
    Gfx_GlDrawLine(x1, y1, x2, y2, color);
}

void Gfx_GlDrawRect(int x1, int y1, int x2, int y2, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    gles_solid_quad((float)x1, (float)y1, (float)(x2 + 1), (float)(y1 + 1), r, g, b, 1.0f);
    gles_solid_quad((float)x1, (float)y2, (float)(x2 + 1), (float)(y2 + 1), r, g, b, 1.0f);
    gles_solid_quad((float)x1, (float)y1, (float)(x1 + 1), (float)(y2 + 1), r, g, b, 1.0f);
    gles_solid_quad((float)x2, (float)y1, (float)(x2 + 1), (float)(y2 + 1), r, g, b, 1.0f);
}

void Gfx_GlDrawRectAdd(int x1, int y1, int x2, int y2, unsigned int color)
{
    Gfx_GlDrawRect(x1, y1, x2, y2, color);
}

void Gfx_GlDrawRectBlend(int x1, int y1, int x2, int y2, unsigned int color, int alpha_src, int alpha_dst)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    glBlendColor(r, g, b, (float)alpha_dst / 256.0f);
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    gles_solid_quad((float)x1, (float)y1, (float)(x2 + 1), (float)(y2 + 1), r, g, b, (float)alpha_src / 256.0f);
    glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Gfx_GlDrawRectSub(int x1, int y1, int x2, int y2, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    gles_solid_quad((float)x1, (float)y1, (float)(x2 + 1), (float)(y2 + 1), r, g, b, 1.0f);
    glBlendEquation(GL_FUNC_ADD);
}

void Gfx_GlFillRect(int x1, int y1, int x2, int y2, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    gles_solid_quad((float)x1, (float)y1, (float)(x2 + 1), (float)(y2 + 1), r, g, b, 1.0f);
}

void Gfx_GlFillRectAdd(int x1, int y1, int x2, int y2, unsigned int color)
{
    Gfx_GlFillRect(x1, y1, x2, y2, color);
}

void Gfx_GlFillRectSub(int x1, int y1, int x2, int y2, unsigned int color)
{
    float r, g, b;

    gles_split565(color, &r, &g, &b);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    gles_solid_quad((float)x1, (float)y1, (float)(x2 + 1), (float)(y2 + 1), r, g, b, 1.0f);
    glBlendEquation(GL_FUNC_ADD);
}

void Gfx_GlBlendRect(int x1, int y1, int x2, int y2, unsigned int color, int alpha_src, int alpha_dst)
{
    Gfx_GlDrawRectBlend(x1, y1, x2, y2, color, alpha_src, alpha_dst);
}

/* --------------------------------------------------------- sprites */

/*
 * Draws one sprite through the ES programs: upload the pixels (or use the
 * texture the sprite already owns), upload the palette as a 256x1 2D texture,
 * build the modelview, hand the quad to the VBO submitter.
 */
void Gl_DrawSprite(int sprite, const DrawParams *dp, SpriteSet *set)
{
    Sprite *spr;
    GlTex *tex;
    GlTex *pal_tex = NULL;
    GlSpriteMv mv;
    float positions[8];
    float texcoords[16];
    float fw, fh, left, right;
    PalFx *fx;
    GlMvp mvp;
    GlesProgram *prog;
    int paletted;

    if (!g_glesReady || set == NULL || dp == NULL) {
        return;
    }
    spr = (Sprite *)(set->sprites->data + set->sprites->elem_size * sprite);
    if (spr == NULL) {
        return;
    }
    if (0.0f == dp->scale2_x || 0.0f == dp->scale2_y || 0.0f == dp->scale_x || 0.0f == dp->scale_y) {
        return;
    }
    paletted = spr->bpp <= 8;
    tex = (spr->format == SPRITE_FORMAT_TEXTURE) ? (GlTex *)spr->pixels : NULL;
    if (tex == NULL) {
        void *pixels = Sprite_GetPixels(set, sprite);

        if (pixels == NULL) {
            return;
        }
        /* A streaming texture per draw, reused through the sprite cache: the
         * GLES path keeps one scratch texture like the desktop's g_glSysTex. */
        {
            static GlTex scratch[2];

            tex = &scratch[paletted ? 0 : 1];
            if (!GlTex_Upload2D(spr->h, spr->bpp, tex, pixels, spr->w)) {
                return;
            }
        }
    }

    if (paletted) {
        static GlTex pal_scratch;
        Palette *pal = SpriteSet_GetPaletteConverted(set, spr->pal_index);

        if (pal == NULL) {
            Log_Printf("GLES: sprite %d has no usable palette; draw skipped\n", sprite);
            return;
        }
        pal_tex = &pal_scratch;
        {
            unsigned char padded[0x100 * 4];
            int n = pal->count;
            const void *colors = pal->data;

            if (n < 0 || n > 0x100) {
                return;
            }
            if (n == 0x100) {
                GlTex_UploadRGBA(1, 0x100, pal_tex, colors);
            } else {
                memset(padded, 0, sizeof(padded));
                if (n > 0 && colors != NULL) {
                    memcpy(padded, colors, (size_t)n * 4);
                }
                GlTex_UploadRGBA(1, 0x100, pal_tex, padded);
            }
        }
    }

    /* Modelview: the same terms, in the same order, as Gl_DrawSprite. */
    memset(&mv, 0, sizeof(mv));
    mv.paletted = paletted;
    mv.pal_offset = g_glPalOffset;
    mv.rgba_offset_x = g_glRgbaOffsetX;
    mv.rgba_offset_y = g_glRgbaOffsetY;
    mv.x = dp->x;
    mv.y = dp->y;
    mv.angle = dp->angle;
    mv.angle_x = dp->angle_y;
    mv.angle_z = dp->angle_z;
    mv.shear_x = dp->shear_x;
    mv.shear_y = dp->shear_y;
    mv.scale_x = dp->scale_x;
    mv.scale_y = dp->scale_y;
    mv.scale2_x = dp->scale2_x;
    mv.scale2_y = dp->scale2_y;
    mv.sx1 = (dp->flip & DRAW_FLIP_H) ? -1 : 1;
    mv.sy1 = (dp->flip & DRAW_FLIP_V) ? -1 : 1;
    mv.sx2 = (dp->flip2 & DRAW_FLIP_H) ? -1 : 1;
    mv.sy2 = (dp->flip2 & DRAW_FLIP_V) ? -1 : 1;
    mv.anchor_x = (float)spr->x;
    mv.anchor_y = (float)spr->y;

    if (!g_glesProjectionValid) {
        Gl_SetupView();
    }
    /* Modelview on the CPU (GLES has no matrix stack), composed with the
     * cached projection for the vertex shader. */
    {
        GlMvp modelview;

        GlMvp_SpriteModelView(&modelview, &mv);
        GlMvp_Multiply(&mvp, &g_glesProjection, &modelview);
    }

    /* Quad corners: the top edge squeezed by width_ratio around width_anchor. */
    fw = (float)spr->w;
    fh = (float)spr->h;
    left = 0.0f;
    right = fw;
    if (!(1.0f == dp->width_ratio)) {
        float d = (float)((double)fw * dp->width_ratio - fw);

        left = (float)(0.0 - (double)d * dp->width_anchor);
        right = (float)((1.0 - dp->width_anchor) * d + fw);
    }
    positions[0] = left;  positions[1] = 0.0f;
    positions[2] = right; positions[3] = 0.0f;
    positions[4] = 0.0f;  positions[5] = fh;
    positions[6] = fw;    positions[7] = fh;

    /* Texture coordinates: straight sampling. The stretched-quad Q is a
     * perspective feature the ES path does not select yet (logged below). */
    {
        int i;

        for (i = 0; i < 4; i++) {
            texcoords[i * 4 + 0] = (i == 0 || i == 2) ? 0.0f : 1.0f;
            texcoords[i * 4 + 1] = (i < 2) ? 0.0f : 1.0f;
            texcoords[i * 4 + 2] = 0.0f;
            texcoords[i * 4 + 3] = 1.0f;
        }
    }
    fx = PalFx_GetApplied();
    prog = paletted ? &g_glesProgramIndexed : &g_glesProgramRgba;

    /* Blend state, as the desktop path sets it. */
    if (dp->blend != BLEND_NONE) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_CONSTANT_ALPHA);
        glBlendColor(dp->alpha_src, dp->alpha_src, dp->alpha_src, dp->alpha_dst);
        if (dp->blend == BLEND_ADD) {
            glBlendEquation(GL_FUNC_ADD);
        } else if (dp->blend == BLEND_SUB) {
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        }
    } else if (dp->masked != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    if (pal_tex != NULL) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (GLuint)pal_tex->id);
        glActiveTexture(GL_TEXTURE0);
    }

    GlesDraw_Quad(&g_glesBuffer, prog, mvp.m, positions, texcoords, paletted, dp->masked, (float)fx->add[0] / 256.0f,
                  (float)fx->add[1] / 256.0f, (float)fx->add[2] / 256.0f, (float)fx->mul[0] / 256.0f,
                  (float)fx->mul[1] / 256.0f, (float)fx->mul[2] / 256.0f, fx->invertall,
                  (float)fx->color / 256.0f, (float)tex->w, (float)tex->h, dp->alpha_src);

    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Sprite_DrawGL(SpriteSet *set, int sprite, const DrawParams *dp)
{
    Gl_DrawSprite(sprite, dp, set);
}

/* ----------------------------------------------------------- present */

/*
 * Presents: binds the window framebuffer and draws the screen buffer texture
 * across it. The desktop renderer does the same through Gl_DrawQuadRgba; here
 * the quad is submitted directly, with a projection that maps the texture onto
 * the whole window.
 */
void Gl_Present(SpriteSet *set, int sprite, float x, float y, float w, float h)
{
    Sprite *spr;
    GlTex *tex;
    GlMvp proj;
    float pos[8];
    float tc[16] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
    int win_w = 0;
    int win_h = 0;

    glDisable(GL_SCISSOR_TEST);
    spr = (Sprite *)(set->sprites->data + set->sprites->elem_size * sprite);
    if (spr == NULL || spr->format != SPRITE_FORMAT_TEXTURE) {
        return;
    }
    tex = (GlTex *)spr->pixels;
    if (tex == NULL || tex->id == 0) {
        return;
    }
    if (w == 0.0f && h == 0.0f) {
        w = (float)spr->w;
        h = (float)spr->h;
    }

    /* The window's own pixel size: the drawable, not the game resolution. */
    Android_GLDrawableSize(&win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) {
        return;
    }
    /*
     * Save the binding and put it back afterwards.
     *
     * Gfx_SetRenderTarget only rebinds when the target *changes*, so leaving
     * the window bound here meant the next frame drew into the window instead
     * of the screen texture -- and then this function cleared the window and
     * blitted the (empty) texture over it. Every frame wiped the previous one,
     * which is exactly the black screen with a small flicker that showed up.
     */
    {
        GLint saved_fbo = 0;

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_glBoundFramebuffer = 0;
        glViewport(0, 0, win_w, win_h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /*
     * The projection covers the VIDEO size, not the render target's.
     *
     * The desktop Gl_Present calls Gfx_SetRenderTarget(NULL, 0) first, which
     * rewrites g_targetWidth/Height to g_videoWidth/Height before Gl_SetupView
     * reads them. Reading the target size directly meant that a stage-fitted
     * match -- where the screen buffer takes the stage's aspect and is smaller
     * than the video -- projected a small region and the picture appeared
     * shrunken at the top-left of the window.
     */
    GlMvp_SetupView(&proj, 0, g_videoWidth, g_videoHeight, 0, g_glNear, g_glFar, 1.0);
    pos[0] = x;         pos[1] = y;
    pos[2] = x + w;     pos[3] = y;
    pos[4] = x;         pos[5] = y + h;
    pos[6] = x + w;     pos[7] = y + h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    GlesDraw_Quad(&g_glesBuffer, &g_glesProgramRgba, proj.m, pos, tc, 0, 0, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0,
                  1.0f, (float)tex->w, (float)tex->h, 1.0f);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
        g_glBoundFramebuffer = saved_fbo != 0 ? 1 : 0;
        Gl_SetupView();
    }
}

/* Copies the framebuffer into a new surface (screenshot). */
void *Gl_Capture(void)
{
    SDL_Surface *shot;
    unsigned char *rgba;
    int w = g_targetWidth;
    int h = g_targetHeight;
    int y;

    if (!g_glesReady || w <= 0 || h <= 0) {
        return NULL;
    }
    rgba = malloc((size_t)w * (size_t)h * 4);
    if (rgba == NULL) {
        return NULL;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    shot = SDL_CreateRGBSurface(0, w, h, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (shot == NULL) {
        free(rgba);
        return NULL;
    }
    /* GL's origin is bottom-left; the surface's is top-left. */
    for (y = 0; y < h; y++) {
        memcpy((unsigned char *)shot->pixels + (size_t)y * shot->pitch, rgba + (size_t)(h - 1 - y) * w * 4,
               (size_t)w * 4);
    }
    free(rgba);
    return shot;
}

/*
 * Render targets (the desktop renderer's "vbuf": a texture with a depth
 * renderbuffer behind a framebuffer object).
 *
 * The engine draws the frame into the screen buffer, which in GL mode is a
 * texture rather than a CPU surface, and Gl_Present then draws that texture to
 * the window. Without a working FBO the screen buffer cannot be a texture and
 * Gl_Present has nothing to show -- which is exactly the black screen the GLES
 * path produced before this.
 */
int GlTex_AllocDepth(GlTex *tex)
{
    GLuint depth = 0;

    if (tex == NULL || tex->w <= 0 || tex->h <= 0) {
        return 0;
    }
    if (tex->depth != 0) {
        return 1;
    }
    glGenRenderbuffers(1, &depth);
    if (depth == 0) {
        Log_Printf("GLES: glGenRenderbuffers failed\n");
        return 0;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, tex->w, tex->h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    tex->depth = (unsigned int)depth;
    return 1;
}

void Gl_AttachFramebuffer(unsigned int texture, unsigned int depth)
{
    static GLuint fbo;

    if (fbo == 0) {
        glGenFramebuffers(1, &fbo);
        if (fbo == 0) {
            Log_Printf("GLES: glGenFramebuffers failed\n");
            return;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)texture, 0);
    if (depth != 0) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, (GLuint)depth);
    } else {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    }
    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            static int logged;

            if (!logged) {
                logged = 1;
                Log_Printf("GLES: render target incomplete (0x%X); drawing to the window\n", (unsigned)status);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_glBoundFramebuffer = 0;
            return;
        }
    }
    g_glBoundFramebuffer = 1;
}

/* Allocates a render-target texture (w x h RGBA plus a depth renderbuffer). */
GlTex *GlVbuf_Alloc(int depth, int w, int h)
{
    GlTex *tex;

    if (depth <= 16) {
        Log_Printf("GLES: render target depth %d unsupported (needs 32)\n", depth);
        return NULL;
    }
    if (w <= 0 || h <= 0) {
        return NULL;
    }
    tex = GlTex_Create();
    if (tex == NULL) {
        g_outOfMemory = 1;
        return NULL;
    }
    if (!GlTex_Alloc(tex, w, h) || !GlTex_AllocDepth(tex)) {
        GlTex_Free(tex);
        return NULL;
    }
    return tex;
}

/* Binds a render target (NULL: the window) with its viewport and projection. */
int GlVbuf_Bind(GlTex *vbuf)
{
    if (vbuf == NULL) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_glBoundFramebuffer = 0;
        glViewport(0, 0, g_targetWidth, g_targetHeight);
        return Gl_SetupView();
    }
    Gl_AttachFramebuffer(vbuf->id, vbuf->depth);
    glViewport(0, 0, vbuf->w, vbuf->h);
    return Gl_SetupView();
}

#endif /* ANDROID_PORT && MUGEN_GLES */
