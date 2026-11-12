/*
 * GLES 3.0 renderer for Android and Linux ARM64 (RENDERING_PLAN.md section 12,
 * PROBLEMS.md P1).
 *
 * The desktop renderer (src/glrender.c) is written against GL 2.1 with the
 * compatibility profile: immediate mode (glBegin/glVertex), a fixed-function
 * matrix stack, and 1D palette textures. None of those exist in GLES, so this
 * is a separate backend rather than a recompile of that file -- exactly what
 * section 12.1 asks for. The two are never built together: MUGEN_GLES excludes
 * glrender.c and gldetect.c, and those files define the same Gl_ and Gfx_Gl
 * entry points this one does.
 *
 * Linux ARM64 uses this backend by default because the handhelds it targets
 * (Mali-G31 under ArkOS) expose OpenGL ES only -- their libGL is the GLX stub
 * and the desktop loader resolves nothing against an EGL context.
 *
 * What is here:
 *   - the GlTex_* texture object over GLES (8-bit index, 24-bit RGB, RGBA),
 *     with the palette as a 256x1 2D texture because GLES has no 1D textures;
 *   - shader programs from the .vert/.frag files in res/shaders/gles (GLSL ES 1.00),
 *     deployed next to the game data as the .vert/.frag files in data/gles;
 *   - the sprite path: CPU modelview (src/gl_mvp.c) + VBO quad submission
 *     (src/gles_draw.c);
 *   - the dispatch entries Gfx_SelectRenderer(RENDER_OPENGL) installs:
 *     clear, pixel, line, rect, fill, blend, capture, present;
 *   - g_glInitialized, the engine-wide "a live GL context is ready" flag.
 *     glrender.c sets it on the desktop path; because that file is excluded
 *     here this backend owns it, and the model loader (Model_Load) asks it.
 *
 * Deliberate limits, logged once rather than hidden:
 *   - one shader variant per path (no PERSPECTIVE_* / INDEX_* / PALFX_DISABLE
 *     selection yet): the ES shaders' defaults apply, which are the same
 *     defaults the desktop loader uses for its "no defines" case;
 *   - RGBA sources are uploaded straight, not premultiplied, and the ES
 *     fragment shaders run without PREMUL -- the two agree with each other.
 */
#include "mugen.h"

#ifdef MUGEN_GLES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gles_probe.h"
#include "gles_blend.h"
#include "gles_prim.h"
#include "gles_render.h"
#include "gl_mvp.h"
#include "gles_draw.h"
#include "gl_size.h"
#include "shader_src.h"

/* 1 once the programs are linked on a live context. Declared early because the
 * texture and view entry points test it before the renderer proper. */
static int g_glesReady;

static GlMvp g_glesProjection;
static int g_glesProjectionValid;

/*
 * The single framebuffer object every render target is attached to.
 *
 * File scope rather than a function-local static because a context loss has to
 * invalidate it: the name belongs to the destroyed context, and leaving it set
 * made Gl_AttachFramebuffer skip glGenFramebuffers after a restore and bind a
 * dead name. The status check then reported the target incomplete and the draw
 * fell back to the window, so the screen buffer silently disappeared.
 *
 * One object is enough -- the engine has exactly one current render target
 * (g_renderTarget, a single global, as in the original at 0x005042a4), never a
 * stack of them, and every bind re-specifies the attachment. The desktop
 * renderer does the same with its own single g_glFramebuffer.
 */
static GLuint g_glesFbo;
static GLuint g_glesAttachedTexture, g_glesAttachedDepth;

static void gles_clear_errors(void) { while (glGetError() != GL_NO_ERROR) {} }
static int gles_ok(void)
{
    int ok = 1;
    while (glGetError() != GL_NO_ERROR) ok = 0;
    return ok;
}
static int gles_texture_size(int w, int h)
{
    return GlImage_Size(w, h, 4, NULL, NULL) &&
           (g_glMaxTextureSize <= 0 || (w <= g_glMaxTextureSize && h <= g_glMaxTextureSize));
}
static void gles_tex_account(GlTex *tex, int w, int h, int bytes_per_pixel);

/* Defined below; the context-restore path calls it, so it is declared first. */
static int gles_build_programs(void);

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

/*
 * Registry of live GlTex objects.
 *
 * A context loss invalidates every GL name at once, so the renderer has to be
 * able to walk what it owns and clear those names -- but GlTex has a fixed
 * 32-bit layout that must not gain fields, and the sprite scratch textures are
 * long-lived statics. A small registry of pointers solves both without touching
 * the struct.
 */
static GlTex **g_glesTextures;
static int g_glesTextureCount;
static int g_glesTextureCapacity;

/*
 * Registers a texture for context-loss cleanup.
 *
 * The first version used a fixed 64-slot array and silently dropped anything
 * past it, so with more than 64 live textures the extra ones kept their stale
 * GL names and were never invalidated on a context loss (texture 1 was cleared
 * to id 0 while texture 65 still held id 65). The list grows instead.
 *
 * Returns 1 on success. A NULL return from realloc is propagated so the caller
 * can fail the allocation rather than create a texture it cannot track.
 */
static int gles_register_tex(GlTex *tex)
{
    int i;

    if (tex == NULL) {
        return 0;
    }
    for (i = 0; i < g_glesTextureCount; i++) {
        if (g_glesTextures[i] == tex) {
            return 1;
        }
    }
    if (g_glesTextureCount == g_glesTextureCapacity) {
        int capacity = (g_glesTextureCapacity == 0) ? 64 : g_glesTextureCapacity * 2;
        GlTex **grown = realloc(g_glesTextures, (size_t)capacity * sizeof(*grown));

        if (grown == NULL) {
            Log_Printf("GLES: out of memory growing the texture registry (%d entries)\n",
                       g_glesTextureCount);
            return 0;
        }
        g_glesTextures = grown;
        g_glesTextureCapacity = capacity;
    }
    g_glesTextures[g_glesTextureCount++] = tex;
    return 1;
}

/*
 * Backing pixels for sprites, so a texture can be rebuilt after a context loss.
 *
 * The sprite path uploads once at load time and then frees the CPU pixels
 * (Sprite_Init), which is what stopped the per-frame re-upload that cost the
 * frame budget. The cost is that a context loss left SPRITE_FORMAT_TEXTURE
 * sprites with id 0 and no source to re-upload from, so they drew black.
 *
 * This keeps the already-decoded bytes (the same buffer Sprite_Init would have
 * freed -- no extra copy) plus the upload parameters, and Gl_DrawSprite revives
 * the texture lazily when it sees id 0.
 *
 * Keyed by GlTex pointer in a small growable table, because GlTex has a fixed
 * 32-bit layout that must not gain fields.
 */
typedef struct GlesBacking {
    GlTex *tex;
    unsigned char *pixels;
    size_t bytes;
    int h;
    int w;
    int bpp;
} GlesBacking;

static GlesBacking *g_glesBacking;
static int g_glesBackingCount;
static int g_glesBackingCapacity;

static GlesBacking *gles_backing_find(GlTex *tex)
{
    int i;

    for (i = 0; i < g_glesBackingCount; i++) {
        if (g_glesBacking[i].tex == tex) {
            return &g_glesBacking[i];
        }
    }
    return NULL;
}

/*
 * Takes ownership of `pixels` (which the caller would otherwise free) and
 * records the parameters needed to re-upload it. Returns 0 when the record
 * cannot be stored, in which case the caller still owns `pixels`.
 */
int GlTex_SetBacking(GlTex *tex, void *pixels, size_t bytes, int h, int w, int bpp)
{
    GlesBacking *slot;

    if (tex == NULL || pixels == NULL || bytes == 0) {
        return 0;
    }
    slot = gles_backing_find(tex);
    if (slot == NULL) {
        if (g_glesBackingCount == g_glesBackingCapacity) {
            int capacity = (g_glesBackingCapacity == 0) ? 64 : g_glesBackingCapacity * 2;
            GlesBacking *grown = realloc(g_glesBacking, (size_t)capacity * sizeof(*grown));

            if (grown == NULL) {
                return 0;
            }
            g_glesBacking = grown;
            g_glesBackingCapacity = capacity;
        }
        slot = &g_glesBacking[g_glesBackingCount++];
        slot->tex = tex;
        slot->pixels = NULL;
    }
    free(slot->pixels);
    slot->pixels = pixels;
    slot->bytes = bytes;
    slot->h = h;
    slot->w = w;
    slot->bpp = bpp;
    return 1;
}

static void gles_backing_release(GlTex *tex)
{
    GlesBacking *slot = gles_backing_find(tex);

    if (slot == NULL) {
        return;
    }
    free(slot->pixels);
    *slot = g_glesBacking[g_glesBackingCount - 1];
    g_glesBackingCount--;
}

/*
 * Re-uploads a texture whose GL name was dropped by a context loss.
 *
 * Returns 1 when the texture has a live name afterwards (including the case
 * where it never lost one), 0 when it cannot be revived.
 */
int GlTex_Revive(GlTex *tex)
{
    GlesBacking *slot;

    if (tex == NULL || !g_glesReady) {
        return 0;
    }
    if (tex->id != 0) {
        return 1;
    }
    slot = gles_backing_find(tex);
    if (slot == NULL) {
        return 0;
    }
    return GlTex_Upload2D(slot->h, slot->bpp, tex, slot->pixels, slot->w);
}

static void gles_unregister_tex(GlTex *tex)
{
    int i;

    for (i = 0; i < g_glesTextureCount; i++) {
        if (g_glesTextures[i] == tex) {
            /* Order does not matter here, so move the last entry down instead
             * of shifting the tail. */
            g_glesTextures[i] = g_glesTextures[g_glesTextureCount - 1];
            g_glesTextureCount--;
            return;
        }
    }
}

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

    if (tex == NULL) {
        return NULL;
    }
    if (!gles_register_tex(tex)) {
        /* Cannot track it, so cannot invalidate its name on a context loss --
         * fail the allocation rather than create an uncorrectable object. */
        free(tex);
        return NULL;
    }
    return tex;
}

int GlTex_Free(GlTex *tex)
{
    if (tex == NULL) {
        return 1;
    }
    /*
     * Check ownership even when the GL name is zero. This used to run only when tex->id was
     * non-zero, which skipped GlTex_Delete entirely after a context loss (loss
     * clears every id), so the backing bytes and their table entry survived the
     * free. The GL name and the object are different lifetimes: Delete already
     * handles id == 0 and reference counting, so it is always safe to call.
     */
    /* Port fix: a retained texture still owns its object and backing. */
    if (!GlTex_Delete(tex)) return 0;
    gles_unregister_tex(tex);
    free(tex);
    return 1;
}

int GlTex_Delete(GlTex *tex)
{
    GLuint id;

    if (tex == NULL) {
        return 0;
    }
    if (tex->refs > 0) {
        return 0;
    }
    id = (GLuint)tex->id;
    if (id != 0) {
        glDeleteTextures(1, &id);
        if (g_glesAttachedTexture == id) g_glesAttachedTexture = g_glesAttachedDepth = 0;
    }
    /*
     * The depth renderbuffer a render target owns was never freed, so every
     * screen/match switch leaked one. glIsRenderbuffer() stayed true after
     * Sprite_Free() because of this, not because of the texture.
     */
    if (tex->depth != 0) {
        GLuint depth = (GLuint)tex->depth;

        glDeleteRenderbuffers(1, &depth);
        tex->depth = 0;
    } else {
        tex->depth = 0;
    }
    /* The desktop Delete clamps at zero instead of going negative; kept. */
    g_glesTexMemory = g_glesTexMemory >= tex->size ? g_glesTexMemory - tex->size : 0;
    {
        int i;
        for (i = 0; i < GLES_TEX_FORMAT_SLOTS; i++) {
            if (g_glesTexFormat[i].id == id) memset(&g_glesTexFormat[i], 0, sizeof(g_glesTexFormat[i]));
        }
    }
    gles_backing_release(tex);
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
    /* Port fix: allocation and upload share the same checked transaction. */
    return GlTex_Upload(tex, h, GL_RGBA, w, GL_RGBA, NULL);
}

/* Books the bytes a full upload of tex occupies. */
static void gles_tex_account(GlTex *tex, int w, int h, int bytes_per_pixel)
{
    unsigned long bytes = (unsigned long)w * (unsigned long)h * (unsigned long)bytes_per_pixel;

    g_glesTexMemory = g_glesTexMemory >= tex->size ? g_glesTexMemory - tex->size : 0;
    tex->size = (unsigned int)bytes;
    g_glesTexMemory += bytes;
    if (g_glesTexMemory > g_glesTexMemoryPeak) {
        g_glesTexMemoryPeak = g_glesTexMemory;
    }
}

int GlTex_Upload(GlTex *tex, int h, int internal_format, int w, int format, const void *pixels)
{
    GLuint id;
    int fresh, bpp;
    GlesTexFormat *slot;
    if (tex == NULL || !g_glesReady || !gles_texture_size(w, h)) return 0;
    bpp = format == GL_RED ? 1 : (format == GL_RGB ? 3 : 4);
    id = (GLuint)tex->id;
    fresh = id == 0;
    gles_clear_errors();
    if (fresh) glGenTextures(1, &id);
    if (id == 0 || !gles_ok()) {
        if (fresh && id != 0) glDeleteTextures(1, &id);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    slot = gles_tex_format_slot(id);
    if (pixels != NULL && slot->w == w && slot->h == h && slot->internal_format == internal_format) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
    }
    if (!gles_ok()) {
        memset(slot, 0, sizeof(*slot));
        if (fresh) glDeleteTextures(1, &id);
        return 0;
    }
    /* Publish only after the driver accepted storage and data. */
    slot->id = id; slot->w = w; slot->h = h; slot->internal_format = internal_format;
    tex->id = id; tex->w = w; tex->h = h;
    gles_tex_account(tex, w, h, bpp);
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
        return GlTex_Upload(tex, h, GL_R8, w, GL_RED, pixels);
    }
    if (bpp == 24) {
        return GlTex_Upload(tex, h, GL_RGB, w, GL_RGB, pixels);
    }
    if (bpp == 32) {
        return GlTex_Upload(tex, h, GL_RGBA, w, GL_RGBA, pixels);
    }
    Log_Printf("GLES: sprite of %d bpp has no texture format\n", bpp);
    return 0;
}

/*
 * RGBA upload: straight (not premultiplied) alpha, which is what the ES
 * shaders' non-PREMUL path expects. Also used for the palette (height 1).
 *
 * Why straight and not premultiplied like the desktop: the engine's sprites
 * are almost all binary-alpha (0 or 255), where the two are exactly equal --
 * for a = 1 the premultiplied colour equals the straight colour and
 * GL_ONE * c + (1-1) * dst == GL_SRC_ALPHA * c + (1-a) * dst -- so blending is
 * correct either way. Premultiplying here while also handing the shaders
 * straight-alpha maths made the frame wash out (mean RGB 240 against the
 * software renderer's 172 on the same screen), measured on device; keeping the
 * upload straight and leaving PREMUL undefined matches the software renderer
 * to within one unit per channel.
 */
int GlTex_UploadRGBA(int h, int w, GlTex *tex, const void *pixels)
{
    if (tex == NULL) {
        return 0;
    }
    return GlTex_Upload(tex, h, GL_RGBA, w, GL_RGBA, pixels);
}

/* ---------------------------------------------------------- programs */


static GlesProgram g_glesProgramIndexed;
static GlesProgram g_glesProgramRgba;
static GlesProgram g_glesProgramSolid;
static GlesBuffer g_glesBuffer;
static unsigned int g_glesCaps;

/* A solid-colour program for the primitive path (rects, lines, pixels). */
static const char *gles_solid_vert_src =
    "attribute vec4 a_position;\n"
    "uniform mat4 u_mvp;\n"
    "void main() { gl_Position = u_mvp * a_position; }\n";

static const char *gles_solid_frag_src =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() { gl_FragColor = u_color; }\n";

/* Reads a shader file from data/gles/, where the port stages the dialect
 * sources (res/shaders/gles/) next to the game data. */
static char *gles_read_shader(const char *name)
{
    char path[512];
    FILE *f;
    int n = snprintf(path, sizeof(path), "data/gles/%s", name);
    if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        /* No trailing newline on purpose: the next GLES line starts with one,
         * so two failures in a row do not leave a blank line between them. */
        Log_Printf("\nGLES: cannot open %s", path);
        return NULL;
    }
    return ShaderSrc_ReadFile(f);
}

/*
 * Dialect variant: one entry per define the ES shaders are compiled with.
 * This mirrors the desktop loader's "no extra defines" program -- INDEX_SAFE
 * (the #else branch), PERSPECTIVE_EVEN (the mode the desktop uses for
 * stretched quads) and full PalFX. Spelled out as data so a future variant is
 * a table edit rather than a new call site (RENDERING_PLAN.md section 12,
 * item 3). PREMUL is absent deliberately; see the call site.
 */
static const char *const g_glesShaderDefines[] = {
    "INDEX_SAFE",
    "PERSPECTIVE_EVEN",
    NULL
};

/* Joins the table into the "#define X\n" block both loaders insert at the
 * marker. The table is fixed and tiny, so truncation cannot happen today; the
 * guard keeps a future entry from overflowing the buffer silently. */
static void gles_build_defines(char *out, size_t cap)
{
    size_t n = 0;
    int i;

    out[0] = '\0';
    for (i = 0; g_glesShaderDefines[i] != NULL; i++) {
        int wrote = snprintf(out + n, cap - n, "#define %s\n", g_glesShaderDefines[i]);

        if (wrote < 0 || (size_t)wrote >= cap - n) {
            out[n] = '\0';
            Log_Printf("\nGLES: variant define list does not fit (%s)", g_glesShaderDefines[i]);
            return;
        }
        n += (size_t)wrote;
    }
}

/* Loads one program from the ES dialect files. */
static int gles_load_es_program(GlesProgram *prog, const char *name)
{
    char vert_name[64];
    char frag_name[64];
    char *vert;
    char *frag;
    char defines[256];
    int ok;

    snprintf(vert_name, sizeof(vert_name), "%s.vert", name);
    snprintf(frag_name, sizeof(frag_name), "%s.frag", name);
    vert = gles_read_shader(vert_name);
    frag = gles_read_shader(frag_name);
    if (vert == NULL || frag == NULL) {
        free(vert);
        free(frag);
        return 0;
    }
    /*
     * Variant defines, built from the table above.
     *
     * PERSPECTIVE_EVEN is what the desktop's special-quad variants use and is
     * the ES shaders' default; normal quads have q = 1 so it changes nothing
     * for them, and stretched quads sample correctly.
     *
     * PREMUL is deliberately NOT defined: uploads carry straight alpha (see
     * GlTex_UploadRGBA), so the shaders must use their non-PREMUL maths. The
     * engine's sprites are almost all binary-alpha, where premultiplied and
     * straight blending are exactly equal, so nothing is lost. Defining it
     * against straight-alpha uploads washed the frame out (mean RGB 240
     * against the software renderer's 172 on the same screen), measured on
     * device.
     */
    gles_build_defines(defines, sizeof(defines));
    ok = GlesDraw_LoadProgram(prog, vert, frag, name, defines);
    free(vert);
    free(frag);
    return ok;
}

/* Deletes the sprite textures and the programs. */
static void gles_free_programs(void)
{
    GlesDraw_FreeProgram(&g_glesProgramIndexed);
    GlesDraw_FreeProgram(&g_glesProgramRgba);
    GlesDraw_FreeProgram(&g_glesProgramSolid);
    GlesDraw_FreeBuffer(&g_glesBuffer);
}

int Gl_Deinit(void)
{
    int i;
#ifdef MUGEN_MODEL3D
    Model_RendererQuit();
#endif
    /* Port fix: a live-context cleanup includes partially initialized state. */
    gles_free_programs();
    for (i = 0; i < g_glesTextureCount; i++) {
        GlTex *tex = g_glesTextures[i];
        if (tex->id != 0) glDeleteTextures(1, &tex->id);
        if (tex->depth != 0) glDeleteRenderbuffers(1, &tex->depth);
    }
    if (g_glesFbo != 0) glDeleteFramebuffers(1, &g_glesFbo);
    Gles_OnContextLost();
    return 1;
}

/*
 * Context loss (RENDERING_PLAN.md 12.6).
 *
 * Android destroys the GL context when the app is backgrounded or the driver
 * resets. Every name the renderer holds becomes invalid -- drawing with them
 * goes nowhere and deleting them is undefined -- so the names are dropped here
 * while the sizes stay, letting each object be recreated on next use. Nothing
 * has to be re-queued from the engine: sprite pixels, palettes and draw
 * parameters are CPU state and survive.
 */
void Gles_OnContextLost(void)
{
    int i;
    /*
     * What was actually held, captured before the reset below. Gl_Init calls
     * this on entry to drop a previous session's names, and on a cold start
     * there is nothing to drop -- logging "context lost" then would report an
     * event that did not happen, and it lands on the end of the "video: vsync"
     * line, which does not end with a newline.
     */
    int was_ready = g_glesReady;
    int tracked = g_glesTextureCount;
#ifdef MUGEN_MODEL3D
    Model_OnContextLost();
#endif

    /*
     * Programs and the vertex buffer are simply forgotten. Calling
     * glDeleteProgram/glDeleteBuffers here would use names that belong to a
     * context that no longer exists.
     */
    memset(&g_glesProgramIndexed, 0, sizeof(g_glesProgramIndexed));
    memset(&g_glesProgramRgba, 0, sizeof(g_glesProgramRgba));
    memset(&g_glesProgramSolid, 0, sizeof(g_glesProgramSolid));
    memset(&g_glesBuffer, 0, sizeof(g_glesBuffer));
    /* The framebuffer name is dead with the context; a zero here makes the
     * next attach generate a fresh one instead of binding the stale name. */
    g_glesFbo = 0;
    g_glesAttachedTexture = g_glesAttachedDepth = 0;
    g_glBoundFramebuffer = 0;
    g_glesTexMemory = 0;
    g_glesReady = 0;
    /*
     * Port fix: g_glInitialized is the engine-wide "the GL renderer is up"
     * flag (the original's 0x00504758) and the model loader asks it, not
     * g_glesReady, before touching a context. Only the desktop renderer
     * (glrender.c) ever set it, and that file is not compiled here, so every
     * Model_Load on a GLES build was refused with "requires the OpenGL
     * renderer" -- a stage whose background is only a .glb drew nothing.
     * Kept in step with g_glesReady in all three places that change it.
     */
    g_glInitialized = 0;
    g_glesProjectionValid = 0;

    /* Textures keep their dimensions so GlTex_Upload/GlVbuf_Bind can revive
     * them; only the names and the depth renderbuffer go. */
    for (i = 0; i < g_glesTextureCount; i++) {
        GlTex *tex = g_glesTextures[i];

        if (tex == NULL) {
            continue;
        }
        tex->id = 0;
        tex->depth = 0;
        tex->size = 0;
    }
    memset(g_glesTexFormat, 0, sizeof(g_glesTexFormat));
    if (was_ready || tracked > 0) {
        Log_Printf("GLES: context lost; object names dropped (%d textures tracked)\n", tracked);
    }
}

/*
 * Rebuilds the renderer on a context that is already current.
 *
 * The caller has created the context and loaded the GLES entry points; this
 * recompiles the programs, re-establishes the GL state and forgets the cached
 * texture storage descriptions so the next upload reallocates instead of
 * calling glTexSubImage2D on a dead name. Returns 1 when the renderer is usable
 * again, 0 when it is not (the caller keeps the System renderer then).
 */
int Gles_OnContextRestore(int drawable_w, int drawable_h)
{
    unsigned int caps = 0;

    if (g_glesReady) {
        return 1;
    }
    /*
     * Failure rehearsal, alongside the context-loss hook in frame.c: a file
     * named "gl_fail_restore" makes one rebuild fail, so the GL -> System
     * recovery can be exercised on a device without breaking a shader by hand.
     * Renamed on use, so it fires once per run.
     */
    {
        FILE *probe = fopen("gl_fail_restore", "r");

        if (probe != NULL) {
            fclose(probe);
            remove("gl_fail_restore");
            Log_Printf("GLES: rebuild failed on request (gl_fail_restore)\n");
            return 0;
        }
    }
    if (!Gles_Probe(&caps)) {
        Log_Printf("GLES: restore failed: no usable context\n");
        return 0;
    }
    g_glesCaps = caps;
    if (!gles_build_programs()) {
        /* The replacement context is live; delete partial programs on it. */
        gles_free_programs();
        Log_Printf("GLES: restore failed: programs did not rebuild\n");
        return 0;
    }
    memset(g_glesTexFormat, 0, sizeof(g_glesTexFormat));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_glMaxTextureSize);
    if (drawable_w > 0 && drawable_h > 0) {
        glViewport(0, 0, drawable_w, drawable_h);
    }
    g_glesProjectionValid = 0;
    g_glesReady = 1;
    g_glInitialized = 1;        /* port fix, see Gles_OnContextLost */
    Log_Printf("GLES: context restored (%dx%d drawable, max texture %d)\n", drawable_w, drawable_h,
               g_glMaxTextureSize);
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
/*
 * Compiles and links the three programs and puts the GL state the renderer
 * assumes in place. Split out of Gl_Init so a context restore rebuilds through
 * exactly the same path rather than a second, drifting copy.
 */
static int gles_build_programs(void)
{
    if (!gles_load_es_program(&g_glesProgramIndexed, "indexed") ||
        !gles_load_es_program(&g_glesProgramRgba, "rgba") ||
        !GlesDraw_LoadProgram(&g_glesProgramSolid, gles_solid_vert_src, gles_solid_frag_src, "solid", NULL) ||
        g_glesProgramSolid.u_color < 0) {
        Log_Printf("\nGLES: program set unavailable; GL mode unavailable\n");
        gles_free_programs();
        return 0;
    }
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    return 1;
}

int Gl_Init(int framebuffers)
{
    unsigned int caps = 0;

    if (g_glesReady) {
        return 1;
    }
    /*
     * Getting here with a live context but no programs means a previous GL
     * session was torn down without Gles_OnContextLost (a mode reload, say).
     * Rebuilding is the whole point of this call, so drop the stale program and
     * buffer state first -- refusing early made an in-match GL -> System switch
     * impossible: the caller saw "unavailable" while nothing had been tried.
     */
    Gl_Deinit();
    if (!Gles_Probe(&caps)) {
        Log_Printf("\nGLES: capability probe failed; GL mode unavailable\n");
        return 0;
    }
    g_glesCaps = caps;
    if (!gles_build_programs()) {
        Gl_Deinit();
        return 0;
    }
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_glMaxTextureSize);
    g_glesReady = 1;
    g_glInitialized = 1;        /* port fix, see Gles_OnContextLost */
    /*
     * Leading newline, like the "video:" lines above it: "video: vsync ..."
     * and the device/version line are printed without a trailing one, so a
     * message that starts here would otherwise be spliced onto the end of
     * whichever of them came last (mugen.log is read line by line).
     */
    Log_Printf("\nGLES: renderer ready (indexed+rgba+solid, max texture %d)\n", g_glMaxTextureSize);
    return 1;
}

/* ------------------------------------------------------------ view */

/*
 * Uploads the projection. GLES has no matrix stack, so the matrix is computed
 * once here (src/gl_mvp.c) and cached for the draws that follow.
 */

int Gl_SetupView(void)
{
    if (!g_glesReady) {
        g_glesProjectionValid = 0;
        return 0;
    }
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
    if (g_clipX1 == 0 && g_clipY1 == 0 && g_clipX2 == g_targetWidth && g_clipY2 == g_targetHeight) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    if (g_glBoundFramebuffer == 0 && g_targetWidth > 0 && g_targetHeight > 0) {
        GLint view[4];
        int x1, x2, y1, y2;
        glGetIntegerv(GL_VIEWPORT, view);
        /* Port fix: overlays use logical video coordinates on a HiDPI drawable. */
        x1 = Ftol(floor((double)g_clipX1 * view[2] / g_targetWidth));
        x2 = Ftol(ceil((double)g_clipX2 * view[2] / g_targetWidth));
        y1 = Ftol(floor((double)(g_targetHeight - g_clipY2) * view[3] / g_targetHeight));
        y2 = Ftol(ceil((double)(g_targetHeight - g_clipY1) * view[3] / g_targetHeight));
        glScissor(view[0] + x1, view[1] + y1, x2 - x1, y2 - y1);
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
    /* Port fix: uniform locations are owned by this linked program. */
    if (g_glesProgramSolid.u_color >= 0) glUniform4f(g_glesProgramSolid.u_color, r, g, b, a);
    GlesDraw_Quad(&g_glesBuffer, &g_glesProgramSolid, g_glesProjection.m, pos, tc, 0, 0, 0.0f, 0.0f, 0.0f, 1.0f,
                  1.0f, 1.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f);
}

/*
 * Primitives (rectangles, lines, pixels).
 *
 * These mirror the desktop's prim.c exactly; only the submission differs, since
 * GLES has no immediate mode. Two things were wrong before, and both showed on
 * the title screen's active-cursor bar, which is drawn through
 * g_gfxFillRectColor (mapped to Gfx_GlFillRectAdd for GL in globals.c):
 *
 *   - The colour was decoded as RGB565. Video_MapRGB returns 0x00BBGGRR when a
 *     GL renderer is active (video.c), which is what Prim_GlColor reads, so
 *     every primitive in the GLES backend had the wrong hue: the pulsing blue
 *     cursor came out a solid dark navy instead of the pale blue the desktop
 *     produces.
 *
 *   - The blend model was wrong. prim.c uses
 *
 *         rgb * alpha_src                     (Prim_GlColor's scale)
 *         glBlendFunc(GL_ONE, GL_SRC_ALPHA)
 *         fragment alpha = alpha_dst
 *
 *     which gives dst' = rgb*alpha_src + dst*alpha_dst -- the same equation the
 *     engine uses for sprites. The GLES path used CONSTANT_ALPHA /
 *     ONE_MINUS_CONSTANT_ALPHA, i.e. dst' = rgb*a + dst*(1-a), and it mapped
 *     g_gfxFillRectColor to an opaque fill, so an additive highlight painted
 *     over the menu instead of adding to it.
 */

/* Maps the pure enum onto the GL equation it names. */
static GLenum gles_prim_gl_eq(GlesPrimEq eq)
{
    return (eq == GLES_PRIM_EQ_REVERSE_SUBTRACT) ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD;
}

/* Prim_GlBlend: blending on, GL_ONE against the fragment alpha, and the
 * equation the kind resolved to. */
static void gles_prim_begin(const GlesPrimState *st)
{
    if (!st->blend) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_SRC_ALPHA);
    glBlendEquation(gles_prim_gl_eq(st->eq));
}

/* Prim_GlRect: ordered corners, extended by 0.9999 so a rect covers its last
 * row and column without spilling into the next one. */
static void gles_prim_rect(int x1, int y1, int x2, int y2, float *fx1, float *fy1, float *fx2, float *fy2)
{
    float t;

    *fx1 = (float)x1;
    *fy1 = (float)y1;
    *fx2 = (float)x2;
    *fy2 = (float)y2;
    if (*fx2 < *fx1) {
        t = *fx1;
        *fx1 = *fx2;
        *fx2 = t;
    }
    if (*fy2 < *fy1) {
        t = *fy1;
        *fy1 = *fy2;
        *fy2 = t;
    }
    *fx2 = (float)((double)*fx2 + 0.9999);
    *fy2 = (float)((double)*fy2 + 0.9999);
}

/* Submits one primitive with the colour and blend state its kind resolves to. */
static void gles_prim_draw(GlesPrimKind kind, int alpha_src, int alpha_dst, float x1, float y1, float x2, float y2,
                           unsigned int color)
{
    GlesPrimState st = GlesPrim_Resolve(kind, alpha_src, alpha_dst);
    float r, g, b, a;

    GlesPrim_Color(color, st.scale, st.alpha, &r, &g, &b, &a);
    gles_prim_begin(&st);
    gles_solid_quad(x1, y1, x2, y2, r, g, b, a);
    glDisable(GL_BLEND);
}

void Gfx_GlClear(unsigned int color)
{
    unsigned char src[4], dst[4];
    unsigned int mapped;
    float r, g, b;

    if (!g_glesReady) {
        return;
    }
    /* Same path as the desktop: palettised then mapped, cleared with alpha 0. */
    src[0] = (unsigned char)color;
    src[1] = (unsigned char)(color >> 8);
    src[2] = (unsigned char)(color >> 16);
    src[3] = 0xFF;
    PalFx_ApplyColor(src, dst);
    mapped = Video_MapRGB(dst[0], dst[1], dst[2]);
    GlesPrim_SplitColor(mapped, &r, &g, &b);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    Gl_ApplyClipRect();
}

void Gfx_GlPutPixel(int x, int y, unsigned int color)
{
    gles_prim_draw(GLES_PRIM_PUT_PIXEL, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, (float)x, (float)y,
                   (float)x + 0.9999f, (float)y + 0.9999f, color);
}

void Gfx_GlAddPixel(int x, int y, unsigned int color)
{
    gles_prim_draw(GLES_PRIM_ADD_PIXEL, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, (float)x, (float)y,
                   (float)x + 0.9999f, (float)y + 0.9999f, color);
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

/* Lines use quads to preserve pixel coverage, although GLES supports GL_LINES.
 * Axis-aligned edges (collision boxes)
 * stay exact; diagonals step pixel by pixel, as the software renderer does. */
static void gles_prim_line(float x1, float y1, float x2, float y2, unsigned int color, GlesPrimKind kind)
{
    GlesPrimState st = GlesPrim_Resolve(kind, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE);
    float r, g, b, a;
    float t;

    GlesPrim_Color(color, st.scale, st.alpha, &r, &g, &b, &a);
    gles_prim_begin(&st);
    if (y1 == y2) {
        gles_solid_quad(x1 < x2 ? x1 : x2, y1, (x1 < x2 ? x2 : x1) + 0.9999f, y1 + 0.9999f, r, g, b, a);
    } else if (x1 == x2) {
        gles_solid_quad(x1, y1 < y2 ? y1 : y2, x1 + 0.9999f, (y1 < y2 ? y2 : y1) + 0.9999f, r, g, b, a);
    } else {
        float dx = (x2 - x1 > 0) ? x2 - x1 : x1 - x2;
        float dy = (y2 - y1 > 0) ? y2 - y1 : y1 - y2;
        int n = (int)(dx > dy ? dx : dy);
        int i;

        for (i = 0; i <= n; i++) {
            t = (n == 0) ? 0.0f : (float)i / (float)n;
            gles_solid_quad(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, x1 + (x2 - x1) * t + 0.9999f,
                            y1 + (y2 - y1) * t + 0.9999f, r, g, b, a);
        }
    }
    glDisable(GL_BLEND);
}

void Gfx_GlDrawLine(float x1, float y1, float x2, float y2, unsigned int color)
{
    gles_prim_line(x1, y1, x2, y2, color, GLES_PRIM_DRAW_LINE);
}

void Gfx_GlDrawLineAdd(float x1, float y1, float x2, float y2, unsigned int color)
{
    gles_prim_line(x1, y1, x2, y2, color, GLES_PRIM_DRAW_LINE_ADD);
}

/*
 * Rectangle outline with the alpha pair its kind selects.
 *
 * The four edges are emitted so that no pixel is covered twice: the horizontal
 * rows run the full width, and the vertical sides sit strictly between them.
 * Drawing each edge as a full-height/full-width quad instead made the four
 * corners take part in two draws, which is invisible for an opaque outline but
 * doubles an additive or subtractive one -- a red 64 additive outline read back
 * as 128 at the corners and 64 along the edges.
 *
 * Degenerate shapes are handled explicitly: a zero-height rect is one row, a
 * zero-width one is a column, and a 1x1 is a single pixel.
 */
static void gles_prim_outline(GlesPrimKind kind, int alpha_src, int alpha_dst, int x1, int y1, int x2, int y2,
                              unsigned int color)
{
    GlesPrimState st = GlesPrim_Resolve(kind, alpha_src, alpha_dst);
    float fx1, fy1, fx2, fy2;
    float row, col;
    float r, g, b, a;

    gles_prim_rect(x1, y1, x2, y2, &fx1, &fy1, &fx2, &fy2);
    GlesPrim_Color(color, st.scale, st.alpha, &r, &g, &b, &a);
    gles_prim_begin(&st);

    /* One pixel of thickness, clamped to the rect itself so a 1x1 does not
     * extend past its own corner. */
    row = (float)((double)fy1 + 0.9999);
    if (row > fy2) {
        row = fy2;
    }
    col = (float)((double)fx1 + 0.9999);
    if (col > fx2) {
        col = fx2;
    }

    if (fy2 - fy1 <= 0.9999f) {
        /* Single row (includes the 1x1 case). */
        gles_solid_quad(fx1, fy1, fx2, fy2, r, g, b, a);
    } else if (fx2 - fx1 <= 0.9999f) {
        /* Single column. */
        gles_solid_quad(fx1, fy1, fx2, fy2, r, g, b, a);
    } else {
        /* Top and bottom rows, full width. */
        gles_solid_quad(fx1, fy1, fx2, row, r, g, b, a);
        gles_solid_quad(fx1, fy2 - 0.9999f, fx2, fy2, r, g, b, a);
        /* Sides, strictly between the rows: their corners are already drawn. */
        if (fy2 - 0.9999f > row) {
            gles_solid_quad(fx1, row, col, fy2 - 0.9999f, r, g, b, a);
            gles_solid_quad(fx2 - 0.9999f, row, fx2, fy2 - 0.9999f, r, g, b, a);
        }
    }
    glDisable(GL_BLEND);
}

void Gfx_GlDrawRect(int x1, int y1, int x2, int y2, unsigned int color)
{
    gles_prim_outline(GLES_PRIM_DRAW_RECT, GLES_PRIM_ALPHA_ONE, 0, x1, y1, x2, y2, color);
}

void Gfx_GlDrawRectAdd(int x1, int y1, int x2, int y2, unsigned int color)
{
    gles_prim_outline(GLES_PRIM_DRAW_RECT_ADD, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, x1, y1, x2, y2, color);
}

void Gfx_GlDrawRectBlend(int x1, int y1, int x2, int y2, unsigned int color, int alpha_src, int alpha_dst)
{
    gles_prim_outline(GLES_PRIM_DRAW_RECT_BLEND, alpha_src, alpha_dst, x1, y1, x2, y2, color);
}

void Gfx_GlDrawRectSub(int x1, int y1, int x2, int y2, unsigned int color)
{
    gles_prim_outline(GLES_PRIM_DRAW_RECT_SUB, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, x1, y1, x2, y2, color);
}

void Gfx_GlFillRect(int x1, int y1, int x2, int y2, unsigned int color)
{
    /* Desktop: Gfx_GlBlendRect(..., 0x100, 0). */
    float fx1, fy1, fx2, fy2;

    gles_prim_rect(x1, y1, x2, y2, &fx1, &fy1, &fx2, &fy2);
    gles_prim_draw(GLES_PRIM_FILL_RECT, GLES_PRIM_ALPHA_ONE, 0, fx1, fy1, fx2, fy2, color);
}

void Gfx_GlFillRectAdd(int x1, int y1, int x2, int y2, unsigned int color)
{
    /* Desktop: Gfx_GlBlendRect(..., 0x100, 0x100). This is what
     * g_gfxFillRectColor maps to for GL (globals.c), so it is the title
     * screen's active-cursor bar: additive, not opaque. */
    float fx1, fy1, fx2, fy2;

    gles_prim_rect(x1, y1, x2, y2, &fx1, &fy1, &fx2, &fy2);
    gles_prim_draw(GLES_PRIM_FILL_RECT_ADD, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, fx1, fy1, fx2, fy2, color);
}

void Gfx_GlFillRectSub(int x1, int y1, int x2, int y2, unsigned int color)
{
    float fx1, fy1, fx2, fy2;

    gles_prim_rect(x1, y1, x2, y2, &fx1, &fy1, &fx2, &fy2);
    gles_prim_draw(GLES_PRIM_FILL_RECT_SUB, GLES_PRIM_ALPHA_ONE, GLES_PRIM_ALPHA_ONE, fx1, fy1, fx2, fy2, color);
}

void Gfx_GlBlendRect(int x1, int y1, int x2, int y2, unsigned int color, int alpha_src, int alpha_dst)
{
    float fx1, fy1, fx2, fy2;

    gles_prim_rect(x1, y1, x2, y2, &fx1, &fy1, &fx2, &fy2);
    gles_prim_draw(GLES_PRIM_BLEND_RECT, alpha_src, alpha_dst, fx1, fy1, fx2, fy2, color);
}

/* --------------------------------------------------------- sprites */

/*
 * Draws one sprite through the ES programs: upload the pixels (or use the
 * texture the sprite already owns), upload the palette as a 256x1 2D texture,
 * build the modelview, hand the quad to the VBO submitter.
 */
/* Maps the pure blend-factor choice onto the GL enum it names. Kept next to the
 * draw code so GlesBlend_Resolve (src/gles_blend.h) stays testable without GL --
 * tools/test_blend_math.sh links it on the host and checks it against the
 * engine's own blend equation. */
static GLenum gles_gl_factor(GlesBlendFactor f)
{
    switch (f) {
    case GLES_BLEND_FACTOR_ZERO:
        return GL_ZERO;
    case GLES_BLEND_FACTOR_ONE:
        return GL_ONE;
    case GLES_BLEND_FACTOR_SRC_ALPHA:
        return GL_SRC_ALPHA;
    case GLES_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    case GLES_BLEND_FACTOR_CONSTANT_ALPHA:
        return GL_CONSTANT_ALPHA;
    case GLES_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    default:
        return GL_ONE;
    }
}

void Gl_DrawSprite(int sprite, const DrawParams *dp, SpriteSet *set)
{
    Sprite *spr;
    GlTex *tex;
    GlTex *pal_tex = NULL;
    GlSpriteMv mv;
    float positions[8];
    float texcoords[16];
    float fw, fh, left, right;
    float c0 = 1.0f;
    float c1 = 1.0f;
    int special;
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
    /*
     * Stretched / tinted quad: same test as Glr_QuadSpecial, so the same quads
     * are "special" on both backends and get the same texcoords.
     */
    special = !(1.0f == dp->top_stretch && 1.0f == dp->bottom_stretch && 0.0f == dp->top_offset &&
                0.0f == dp->bottom_offset && 1.0f == dp->width_ratio);
    c0 = dp->top_stretch;
    c1 = dp->bottom_stretch;
    if (!(0.0f < c0)) {
        c0 = 1.0f;
    }
    if (!(0.0f < c1)) {
        c1 = 1.0f;
    }
    tex = (spr->format == SPRITE_FORMAT_TEXTURE) ? (GlTex *)spr->pixels : NULL;
    if (tex != NULL && tex->id == 0) {
        /*
         * A context loss dropped this sprite's GL name. The sprite path uploads
         * once at load time and frees the CPU pixels, so without the backing
         * sidecar there is nothing to rebuild from and the sprite drew black --
         * the renderer reported ready while every SPRITE_FORMAT_TEXTURE sprite
         * still had id 0. Revive from the backing bytes; if that fails, fall
         * through to the per-draw scratch upload below, which still works.
         */
        if (!GlTex_Revive(tex)) {
            tex = NULL;
        }
    }
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
            /* Statics never pass through GlTex_Create, so register them here or
             * a context loss would leave them with dead names forever. */
            if (!gles_register_tex(tex)) return;
            if (!GlTex_Upload2D(spr->h, spr->bpp, tex, pixels, spr->w)) {
                return;
            }
        }
    }

    if (paletted) {
        static GlTex pal_scratch;
        Palette *pal;

        if (!gles_register_tex(&pal_scratch)) return;
        pal = SpriteSet_GetPaletteConverted(set, spr->pal_index);

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
                if (!GlTex_UploadRGBA(1, 0x100, pal_tex, colors)) return;
            } else {
                memset(padded, 0, sizeof(padded));
                if (n > 0 && colors != NULL) {
                    memcpy(padded, colors, (size_t)n * 4);
                }
                if (!GlTex_UploadRGBA(1, 0x100, pal_tex, padded)) return;
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

    /*
     * Texture coordinates through the shared quad builder, so a stretched or
     * tinted quad carries its projective Q exactly as the desktop path does.
     * Straight sampling (q = 1 everywhere) was the earlier behaviour, which
     * flattened the trapezoid: parallax and top/bottom-stretched quads sampled
     * a linear ramp instead of the perspective-correct one.
     */
    {
        GlQuadVerts quad;
        float tc_flat[GL_QUAD_VERTEX_COUNT * 4];
        int i;

        GlQuad_Build(&quad, positions, special, (double)c0, (double)c1, (double)dp->top_offset,
                     (double)dp->bottom_offset, dp->width_ratio);
        for (i = 0; i < GL_QUAD_VERTEX_COUNT; i++) {
            tc_flat[i * 4 + 0] = quad.tc[i][0];
            tc_flat[i * 4 + 1] = quad.tc[i][1];
            tc_flat[i * 4 + 2] = quad.tc[i][2];
            tc_flat[i * 4 + 3] = quad.tc[i][3];
            texcoords[i * 4 + 0] = tc_flat[i * 4 + 0];
            texcoords[i * 4 + 1] = tc_flat[i * 4 + 1];
            texcoords[i * 4 + 2] = tc_flat[i * 4 + 2];
            texcoords[i * 4 + 3] = tc_flat[i * 4 + 3];
        }
    }
    fx = PalFx_GetApplied();
    prog = paletted ? &g_glesProgramIndexed : &g_glesProgramRgba;

    /*
     * Blend state. The factors come from GlesBlend_ResolveOp (src/gles_blend.h),
     * which tools/test_blend_math.sh checks against the engine's own equation --
     * dst = src * alpha_src + dst * alpha_dst -- so the GPU and the software
     * renderer cannot drift apart without a test failing.
     *
     * The shader folds alpha_src into its output alpha (`color.a *=
     * src_alphamul`), which is why the source factor is GL_SRC_ALPHA and the
     * constant only carries alpha_dst.
     */
    {
        GlesBlendState blend = GlesBlend_ResolveOp(dp->blend, dp->masked, dp->blend == BLEND_SUB);

        if (!blend.enabled) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(gles_gl_factor(blend.src), gles_gl_factor(blend.dst));
            glBlendColor(dp->alpha_src, dp->alpha_src, dp->alpha_src, dp->alpha_dst);
            glBlendEquation(blend.eq == GLES_BLEND_EQ_REVERSE_SUBTRACT ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    if (pal_tex != NULL) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (GLuint)pal_tex->id);
        glActiveTexture(GL_TEXTURE0);
    }

    /* PalFX scalars exactly as Glr_FxTerms computes them: add is 0..255 and
     * mul / color are fixed point with 256 = 1.0. */
    GlesDraw_Quad(&g_glesBuffer, prog, mvp.m, positions, texcoords, paletted, dp->masked, (float)fx->add[0] / 255.0f,
                  (float)fx->add[1] / 255.0f, (float)fx->add[2] / 255.0f, (float)fx->mul[0] * (1.0f / 256.0f),
                  (float)fx->mul[1] * (1.0f / 256.0f), (float)fx->mul[2] * (1.0f / 256.0f), fx->invertall,
                  (float)fx->color * (1.0f / 256.0f), (float)tex->w, (float)tex->h, dp->alpha_src);

    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Sprite_DrawGL(SpriteSet *set, int sprite, const DrawParams *dp)
{
    Gl_DrawSprite(sprite, dp, set);
}

/* ----------------------------------------------------------- present */

/*
 * Size of the drawable the context renders into, in pixels.
 *
 * Android owns its window through ANativeWindow, so it asks that. Everywhere
 * else the window belongs to src/sdl2video.c, which wraps SDL_GL_GetDrawableSize.
 * It is not the window size on a scaled or high-DPI display, and the presenter
 * below must clear and viewport the drawable or the picture lands in a corner.
 */
static void Gles_DrawableSize(int *w, int *h)
{
#ifdef ANDROID_PORT
    Android_GLDrawableSize(w, h);
#else
    Sdl2_GLDrawableSize(w, h);
#endif
}

/*
 * Presents: binds the window framebuffer and draws the screen buffer texture
 * across it. The desktop renderer does the same through Gl_DrawQuadRgba; here
 * the quad is submitted directly, with a projection that maps the texture onto
 * the whole window.
 */
void Gl_Present(SpriteSet *set, int sprite, float x, float y, float w, float h)
{
    SpriteSet *saved_target = g_renderTarget;
    int saved_sprite = g_renderTargetSprite, saved_projection = g_glProjection;
    int cx1 = g_clipX1, cy1 = g_clipY1, cx2 = g_clipX2, cy2 = g_clipY2;
    int win_w = 0, win_h = 0;
    GLint saved_fbo = 0;
    int saved_bound = g_glBoundFramebuffer;
    Sprite *spr = NULL;
    GlTex *tex = NULL;
    GlMvp proj;
    float pos[8];
    float tc[16] = {0,0,0,1, 1,0,0,1, 0,1,0,1, 1,1,0,1};
    if (!g_glesReady) return;
    if (set != NULL) {
        if (set->sprites == NULL || set->sprites->data == NULL || sprite < 0 || sprite >= set->sprites->capacity) return;
        spr = (Sprite *)(set->sprites->data + set->sprites->elem_size * sprite);
        if (spr->format != SPRITE_FORMAT_TEXTURE || spr->pixels == NULL) return;
        tex = spr->pixels;
        if (tex->id == 0) return;
        if (w == 0.0f && h == 0.0f) { w = (float)spr->w; h = (float)spr->h; }
    }
    Gles_DrawableSize(&win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) {
        /*
         * Silent black screen guard: without a drawable there is nowhere to
         * present, and returning quietly made that indistinguishable from a
         * renderer that draws nothing. Once per run, not per frame.
         */
        static int warned;

        if (!warned) {
            warned = 1;
            Log_Printf("\nGLES: drawable size is %dx%d; the frame cannot be presented\n", win_w, win_h);
        }
        return;
    }
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    g_glProjection = 0; /* Overlays and the screen quad use video coordinates. */
    if (!Gfx_SetRenderTarget(NULL, 0)) { g_glProjection = saved_projection; return; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Gl_SetupView();
    g_glBoundFramebuffer = 0;
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    if (tex != NULL) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        GlMvp_SetupView(&proj, 0, g_videoWidth, g_videoHeight, 0, g_glNear, g_glFar, 1.0);
        pos[0]=x; pos[1]=y; pos[2]=x+w; pos[3]=y;
        pos[4]=x; pos[5]=y+h; pos[6]=x+w; pos[7]=y+h;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        GlesDraw_Quad(&g_glesBuffer, &g_glesProgramRgba, proj.m, pos, tc, 0, 0, 0,0,0, 1,1,1, 0,1,
                      (float)tex->w, (float)tex->h, 1);
    }
    /* Port fix: pause/training/debug/console overlays share the desktop hook. */
    if (g_gfxPrePresent != NULL) g_gfxPrePresent(NULL, 0);
#ifndef ANDROID_PORT
    /*
     * Present the frame that was just drawn.
     *
     * Android swaps at the frame boundary (Android_GLPresent, called from
     * Screen_EndFrame) and has done since M8, so the presenter never swapped
     * itself. Nothing else does it on the SDL2 desktop: g_gfxPresent for
     * RENDER_OPENGL *is* this function, and Sdl2_PresentFrame only flushes the
     * System renderer's software surface -- it returns early for a GL window,
     * because Sdl2_MarkDisplayDirty deliberately ignores one. So the frame went
     * into the window's back buffer and was never shown: mugen.log ran to mode
     * select, the frame counter advanced, and the panel stayed black.
     *
     * The desktop GL renderer swaps at the same place in its own present
     * (src/glrender.c Gl_Present); this is that tail. Screenshots did not catch
     * it because Gl_Capture reads the render *target* (the screen-buffer FBO),
     * which was always correct -- the missing step was between that texture and
     * the panel.
     *
     * Swapped here, before the target is restored: the frame is complete at
     * this point, and the restore below must still run. A skipped frame never
     * reaches the presenter at all, so it presents nothing, as on Android.
     */
    if ((g_videoFlags & VIDEO_HWBLIT) && (g_videoSdlFlags & DISPLAY_DOUBLEBUF)) {
        Video_SwapBuffers();
    } else {
        /*
         * Unreachable today: config.c forces VIDEO_HWBLIT for RENDER_OPENGL and
         * Video_SetMode turns that into DISPLAY_DOUBLEBUF. Reported rather than
         * assumed, because the failure it would produce -- a framed, healthy
         * log and a black panel -- is indistinguishable from the bug above.
         */
        static int warned;

        if (!warned) {
            warned = 1;
            Log_Printf("\nGLES: GL mode is not double buffered; the frame cannot be presented\n");
        }
    }
#endif
    g_glProjection = saved_projection;
    if (!Gfx_SetRenderTarget(saved_target, saved_sprite)) {
        /* Do not force a rejected FBO back into use after the overlay. */
        Log_Printf("GLES: could not restore the render target after present\n");
        g_glProjection = 0;
        Gfx_SetRenderTarget(NULL, 0);
        Gl_SetupView();
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    g_glBoundFramebuffer = saved_bound;
    Gl_SetupView();
    Gfx_SetClipRect(cx1, cy1, cx2, cy2);
}

/*
 * Copies the framebuffer into a caller-owned RGBA buffer (screenshot).
 *
 * The contract is GfxCaptureFn: a raw malloc'ed buffer of the target's size,
 * top row first, which Gfx_GetScreenSurface copies out and then frees. An
 * earlier version returned an SDL_Surface, and the caller -- reading it as the
 * raw buffer the contract promises -- walked off the end of the SDL_Surface
 * struct: an 8x8 probe read 256 bytes from a 96-byte allocation.
 *
 * The row order is flipped here because GL's origin is bottom-left; SDL, PNG
 * and the desktop renderer's captures are all top-left.
 */
void *Gl_Capture(void)
{
    unsigned char *rgba;
    unsigned char *flipped;
    int w = g_targetWidth;
    int h = g_targetHeight;
    int y;
    size_t row, bytes;

    if (!g_glesReady || !GlImage_Size(w, h, 4, &row, &bytes)) {
        return NULL;
    }
    rgba = malloc(bytes);
    if (rgba == NULL) {
        return NULL;
    }
    gles_clear_errors();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!gles_ok()) { free(rgba); return NULL; }

    /*
     * Row order depends on which framebuffer was read, because the engine
     * projects the two differently (GlMvp_SetupView): the window framebuffer
     * gets an inverted ortho (top = 0), a render target gets the ordinary one
     * (top = height). glReadPixels always returns GL's bottom-up order, so
     *
     *   window  -> rows come out bottom-up, flip to the top-down order SDL and
     *              PNG use;
     *   FBO     -> the engine already drew them inverted, so GL's order is the
     *              top-down order and flipping again would turn the image
     *              upside down (a red-on-top, blue-on-bottom fixture came out
     *              blue-on-top).
     *
     * Flipped in place by swapping rows so the result is still one buffer, as
     * the caller's free() expects.
     */
    if (g_glBoundFramebuffer == 0) {
        flipped = malloc(row);
        if (flipped == NULL) {
            free(rgba);
            return NULL;
        }
        for (y = 0; y < h / 2; y++) {
            unsigned char *top = rgba + (size_t)y * w * 4;
            unsigned char *bottom = rgba + (size_t)(h - 1 - y) * w * 4;

            memcpy(flipped, top, (size_t)w * 4);
            memcpy(top, bottom, (size_t)w * 4);
            memcpy(bottom, flipped, (size_t)w * 4);
        }
        free(flipped);
    }
    return rgba;
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

    if (tex == NULL || !gles_texture_size(tex->w, tex->h)) {
        return 0;
    }
    if (!g_glesReady) {
        return 0;
    }
    if (tex->depth != 0) {
        return 1;
    }
    gles_clear_errors();
    glGenRenderbuffers(1, &depth);
    if (depth == 0) {
        Log_Printf("GLES: glGenRenderbuffers failed\n");
        return 0;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, tex->w, tex->h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (!gles_ok()) { glDeleteRenderbuffers(1, &depth); return 0; }
    tex->depth = (unsigned int)depth;
    return 1;
}

int Gl_AttachFramebuffer(unsigned int texture, unsigned int depth)
{
    GLint previous = 0;
    int previous_bound = g_glBoundFramebuffer;
    GLenum status;
    if (!g_glesReady) return 0;
    gles_clear_errors();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    if (texture == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (gles_ok()) { g_glBoundFramebuffer = 0; return 1; }
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
        return 0;
    }
    if (g_glesFbo == 0) glGenFramebuffers(1, &g_glesFbo);
    if (g_glesFbo == 0) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, g_glesFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (!gles_ok() || status != GL_FRAMEBUFFER_COMPLETE) {
        Log_Printf("GLES: render target incomplete (0x%X)\n", (unsigned int)status);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_glesAttachedTexture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_glesAttachedDepth);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
        g_glBoundFramebuffer = previous_bound;
        return 0;
    }
    g_glesAttachedTexture = texture; g_glesAttachedDepth = depth;
    g_glBoundFramebuffer = 1;
    return 1;
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
    if (!g_glesReady) return 0;
    if (vbuf != NULL && vbuf->id == 0) {
        /*
         * Context loss dropped this render target's GL name but kept its size.
         * Revive it here, before the framebuffer attaches it -- attaching name
         * 0 is what reported GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
         * (0x8CD7) after a restore.
         */
        if (vbuf->w <= 0 || vbuf->h <= 0) {
            return 0;
        }
        if (!GlTex_Alloc(vbuf, vbuf->w, vbuf->h) || !GlTex_AllocDepth(vbuf)) {
            Log_Printf("GLES: could not revive the render target (%dx%d)\n", vbuf->w, vbuf->h);
            return 0;
        }
    }
    if (vbuf == NULL) {
        if (!Gl_AttachFramebuffer(0, 0)) return 0;
        glViewport(0, 0, g_targetWidth, g_targetHeight);
        return Gl_SetupView();
    }
    if (!Gl_AttachFramebuffer(vbuf->id, vbuf->depth)) return 0;
    glViewport(0, 0, vbuf->w, vbuf->h);
    return Gl_SetupView();
}

#endif /* MUGEN_GLES */
