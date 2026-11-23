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
 *   - shader programs from the embedded .vert/.frag sources in src/shaders/gles
 *     (GLSL ES 1.00), compiled in by tools/embed_shader.sh;
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
#include "sffatlas.h"

#ifdef MUGEN_GLES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "batchprof.h"
#include "gputime.h"

#include "gles_probe.h"
#include "gles_blend.h"
#include "gles_prim.h"
#include "gles_render.h"
#include "gl_mvp.h"
#include "gles_draw.h"
#include "gl_size.h"
#include "shader_src.h"

/* Embedded GLES sprite shaders (src/shaders/gles/, tools/embed_shader.sh). */
#include "gles/indexed.vert.h"
#include "gles/indexed.frag.h"
#include "gles/rgba.vert.h"
#include "gles/rgba.frag.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "../third_party/stb/stb_rect_pack.h"

/* 1 once the programs are linked on a live context. Declared early because the
 * texture and view entry points test it before the renderer proper. */
static int g_glesReady;
static int g_batch_count;
static int g_limit_stage_pairs;
static int g_indexed_atlas_inverse_size;
static int g_atlasResidentPages;
static GlesProgram g_glesProgramAtlas, g_glesProgramAtlasRgba;
static int g_spriteAtlasDefault;
static int g_spriteBatchDefault = -1;

void Gles_ConfigureSprites(int atlas, int batch)
{
    g_spriteAtlasDefault = atlas == 1;
    g_spriteBatchDefault = (batch == 0 || batch == 1) ? batch : -1;
}

static int gles_atlas_requested(void)
{
    const char *value = getenv("MUGEN_SPRITEATLAS");

    return value != NULL ? strcmp(value, "1") == 0 : g_spriteAtlasDefault;
}

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
static int g_glesTargetWidth, g_glesTargetHeight;

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
/*
 * Hash of a 256-colour RGBA palette, for the upload cache below. FNV-1a over
 * the bytes: 1 KiB per paletted draw, which is nothing next to the upload it
 * can skip, and it cannot be fooled by an in-place edit the way a pointer can.
 */
static unsigned int gles_palette_hash(const unsigned char *bytes)
{
    unsigned int hash = 2166136261u;
    int i;

    for (i = 0; i < 0x100 * 4; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

/*
 * Palettes currently on the GPU, by content hash.
 *
 * A single scratch palette texture was enough to be correct, but a fight draws
 * sprites that alternate between several palettes, so a one-entry cache missed
 * constantly and re-uploaded on nearly every draw. One texture per distinct
 * palette turns that into an upload the first time each palette is seen.
 *
 * Port fix: any palette can use any of the 32 slots. Cached bytes verify
 * hash matches, and only a full cache evicts its least recently used entry.
 */
#define GLES_PALETTE_SLOTS 32

/*
 * Textures for sprites that are not SPRITE_FORMAT_TEXTURE, keyed by the pixels
 * they were built from.
 *
 * A sprite that arrives here is either a compressed one the loader did not
 * decode into a texture (SFF v2 RLE/LZ/PNG) or an SDL surface; its pixels come
 * from Sprite_GetPixels and are the same buffer every frame. One scratch
 * texture per draw meant re-uploading that buffer before *every* draw of the
 * sprite -- on a mode-select screen that tiles an 800x320 RLE8 background ~150
 * times, measured at 300 ms of CPU per frame on an RG351MP (one 256 KiB upload
 * per tile, per frame), against ~2 ms for the whole 2D frame elsewhere.
 *
 * The key is the image's own identity, not the decoded buffer's address: the
 * decompression pool recycles payload addresses (DecompBuffer_Alloc), so a key
 * built from the address Sprite_GetPixels returns can hit another image's slot
 * and skip the upload -- the same extent, a different picture. `source` is
 * therefore the sprite's own source span (encoded data for a compressed
 * sprite, the surface for an SDL one), with codec, width, height and depth as
 * the rest of the key (SpriteTexKey_Matches, plan C1). A context loss bumps the
 * generation, which invalidates every slot's contents without touching the
 * names (those are dropped by Gles_OnContextLost).
 */
/* Port addition: 32 slots keep most of the measured ARM64 match working set
 * resident even with standalone font glyphs. See port/arm64/OPTIMIZING_ARM64_PLAN.md. */
#define GLES_STREAM_SLOTS 32

typedef struct GlesStreamSlot {
    GlTex tex;
    const void *source;     /* stable image identity, never a decoded pool address */
    int format;
    int w, h, bpp;
    int valid;
    unsigned long tick;
    unsigned int generation;
} GlesStreamSlot;

static GlesStreamSlot g_glesStream[GLES_STREAM_SLOTS];
static unsigned int g_glesStreamGeneration;
static unsigned long g_glesStreamClock;

typedef struct GlesPalSlot {
    GlTex tex;
    unsigned int hash;
    uint64_t last_used;
    unsigned char bytes[0x100 * 4];
} GlesPalSlot;

static GlesPalSlot g_glesPalSlots[GLES_PALETTE_SLOTS];
static uint64_t g_glesPaletteClock;

static int gles_register_tex(GlTex *tex);

/* Port addition: a bounded indexed atlas per SFF. The sidecar owns GL
 * pages and maps sprite slots to immutable regions, preserving engine layouts. */
#define GLES_ATLAS_PAGE_SIZE 1024
#define GLES_ATLAS_SET_PAGES 8
#define GLES_ATLAS_RESIDENT_PAGES 16

typedef struct GlesAtlasSource {
    uintptr_t pixels;
    int format, bytes, w, h;
    int sprite;
} GlesAtlasSource;

typedef struct GlesAtlasRegion {
    int page, x, y;
    unsigned int generation;
    int uploaded;
    GlTex *standalone;
} GlesAtlasRegion;

typedef struct GlesAtlasPage {
    GlTex *tex;
    int w, h;
} GlesAtlasPage;

typedef struct GlesSpriteAtlas {
    const SpriteSet *set;
    int capacity;
    int region_count;
    int *sprite_regions;
    GlesAtlasRegion *regions;
    GlesAtlasPage pages[GLES_ATLAS_SET_PAGES];
    struct GlesSpriteAtlas *next;
} GlesSpriteAtlas;

static GlesSpriteAtlas *g_glesAtlases;

static int gles_atlas_page_limit(void)
{
    return g_glMaxTextureSize < GLES_ATLAS_PAGE_SIZE ? g_glMaxTextureSize : GLES_ATLAS_PAGE_SIZE;
}

int Gles_AtlasUsesSource(int w, int h, int bpp)
{
    int limit = gles_atlas_page_limit();

    return gles_atlas_requested() && g_glesProgramAtlas.program != 0 && g_renderMode == RENDER_MODE_GL &&
           bpp == 8 && w > 0 && h > 0 && w <= limit && h <= limit;
}

static int gles_atlas_compare_sources(const void *a, const void *b)
{
    const GlesAtlasSource *x = a, *y = b;

    if (x->pixels != y->pixels) return x->pixels < y->pixels ? -1 : 1;
    if (x->format != y->format) return x->format < y->format ? -1 : 1;
    if (x->bytes != y->bytes) return x->bytes < y->bytes ? -1 : 1;
    if (x->w != y->w) return x->w < y->w ? -1 : 1;
    if (x->h != y->h) return x->h < y->h ? -1 : 1;
    return 0;
}

void Gles_AtlasPlan(const SpriteSet *set)
{
    GlesSpriteAtlas *atlas = NULL;
    GlesAtlasSource *sources = NULL;
    stbrp_rect *rects = NULL;
    stbrp_node *nodes = NULL;
    const KeyedArray *sprites;
    int limit = gles_atlas_page_limit();
    int i, count = 0, unique = 0, page;

    if (set == NULL || set->sprites == NULL || !Gles_AtlasUsesSource(1, 1, 8)) return;
    sprites = set->sprites;
    if (sprites->count == 0) return;
    sources = calloc((size_t)sprites->count, sizeof(*sources));
    if (sources == NULL) goto done;
    for (i = 0; i < sprites->capacity; i++) {
        const Sprite *spr = (const Sprite *)(sprites->data + (size_t)i * sprites->elem_size);

        if (!sprites->slots[i].used || spr->pixels == NULL || spr->pixels_size <= 0 ||
            !Gles_AtlasUsesSource(spr->w, spr->h, spr->bpp)) continue;
        if (spr->format == SPRITE_FORMAT_RAW) {
            if (spr->pixels_size < spr->w * spr->h) continue;
        } else if (spr->format < SPRITE_FORMAT_DECODE_MIN || spr->format > SPRITE_FORMAT_DECODE_MAX) {
            continue;
        }
        sources[count++] = (GlesAtlasSource){ (uintptr_t)spr->pixels, spr->format,
                                             spr->pixels_size, spr->w, spr->h, i };
    }
    if (count == 0) goto done;
    qsort(sources, (size_t)count, sizeof(*sources), gles_atlas_compare_sources);

    atlas = calloc(1, sizeof(*atlas));
    rects = calloc((size_t)count, sizeof(*rects));
    nodes = calloc((size_t)limit, sizeof(*nodes));
    if (atlas == NULL || rects == NULL || nodes == NULL) goto done;
    atlas->sprite_regions = calloc((size_t)sprites->capacity, sizeof(*atlas->sprite_regions));
    atlas->regions = calloc((size_t)count, sizeof(*atlas->regions));
    if (atlas->sprite_regions == NULL || atlas->regions == NULL) goto done;

    for (i = 0; i < count; i++) {
        const GlesAtlasSource *source = &sources[i];

        if (i == 0 || gles_atlas_compare_sources(source, &sources[i - 1]) != 0) {
            rects[unique] = (stbrp_rect){ .id = unique, .w = source->w, .h = source->h };
            atlas->regions[unique++].page = -1;
        }
        atlas->sprite_regions[source->sprite] = unique;  /* zero means no region */
    }
    for (count = unique, page = 0; unique > 1 && count > 0 && page < GLES_ATLAS_SET_PAGES; page++) {
        stbrp_context packer;
        GlesAtlasPage *out = &atlas->pages[page];
        int remaining = 0;

        stbrp_init_target(&packer, limit, limit, nodes, limit);
        stbrp_pack_rects(&packer, rects, count);
        for (i = 0; i < count; i++) {
            stbrp_rect rect = rects[i];
            GlesAtlasRegion *region = &atlas->regions[rect.id];

            if (!rect.was_packed) {
                rects[remaining++] = rect;
                continue;
            }
            region->page = page;
            region->x = rect.x;
            region->y = rect.y;
            if (out->w < rect.x + rect.w) out->w = rect.x + rect.w;
            if (out->h < rect.y + rect.h) out->h = rect.y + rect.h;
        }
        count = remaining;
    }
    atlas->set = set;
    atlas->capacity = sprites->capacity;
    atlas->region_count = unique;
    atlas->next = g_glesAtlases;
    g_glesAtlases = atlas;
    Log_Printf("GLES: SFF atlas planned %d/%d indexed images across %d page(s)\n", unique - count, unique, page);
    atlas = NULL;

done:
    if (atlas != NULL) {
        free(atlas->sprite_regions);
        free(atlas->regions);
        free(atlas);
    }
    free(nodes);
    free(rects);
    free(sources);
}

static void gles_atlas_free(const SpriteSet *set)
{
    GlesSpriteAtlas **link;

    for (link = &g_glesAtlases; *link != NULL; link = &(*link)->next) {
        GlesSpriteAtlas *atlas = *link;
        int i;

        if (atlas->set != set) continue;
        *link = atlas->next;
        for (i = 0; i < GLES_ATLAS_SET_PAGES; i++) {
            GlTex *tex = atlas->pages[i].tex;

            if (tex != NULL && tex->id != 0) g_atlasResidentPages--;
            GlTex_Free(tex);
        }
        for (i = 0; i < atlas->region_count; i++) {
            GlTex_Free(atlas->regions[i].standalone);
        }
        free(atlas->sprite_regions);
        free(atlas->regions);
        free(atlas);
        return;
    }
}

/* Port fix: overflow keeps one standalone GL texture per distinct source.
 * Keep the SFF source for context recovery and software drawing. */
static GlTex *gles_atlas_standalone(SpriteSet *set, int sprite, const Sprite *spr, GlesAtlasRegion *region)
{
    GlTex *tex = region->standalone;
    const void *pixels;
    unsigned long long start;

    if (tex != NULL && tex->id != 0) return tex;
    start = BATCHPROF_NOW();
    pixels = Sprite_GetPixels(set, sprite);
    if (pixels == NULL) return NULL;
    if (tex == NULL) {
        tex = GlTex_Create();
        if (tex == NULL) return NULL;
    }
    if (!GlTex_Upload2D(spr->h, spr->bpp, tex, pixels, spr->w)) {
        if (region->standalone == NULL) GlTex_Free(tex);
        return NULL;
    }
    region->standalone = tex;
    {
        unsigned long long elapsed = BATCHPROF_NOW() - start;

        BatchProf_Split(BATCHPROF_SPLIT_UPLOAD, elapsed);
        BatchProf_Split(BATCHPROF_SPLIT_UPLOAD_SPRITE, elapsed);
    }
    return tex;
}

static GlTex *gles_atlas_texture(SpriteSet *set, int sprite, const Sprite *spr, int allow_atlas,
                                 float rect[4], int *atlased)
{
    GlesSpriteAtlas *atlas;
    GlesAtlasRegion *region;
    GlesAtlasPage *page;
    int index;

    if (g_glesAtlases == NULL || spr->bpp != 8 || spr->format == SPRITE_FORMAT_TEXTURE) return NULL;
    while (set->shared != NULL) set = set->shared;
    for (atlas = g_glesAtlases; atlas != NULL && atlas->set != set; atlas = atlas->next) {}
    if (atlas == NULL || sprite < 0 || sprite >= atlas->capacity) return NULL;
    index = atlas->sprite_regions[sprite];
    if (index == 0) return NULL;
    region = &atlas->regions[index - 1];
    if (region->page < 0 || !allow_atlas || g_glesProgramAtlas.program == 0) goto standalone;
    page = &atlas->pages[region->page];

    if (page->tex == NULL || page->tex->id == 0 ||
        !region->uploaded || region->generation != g_glesStreamGeneration) {
        const void *pixels;
        unsigned long long start = BATCHPROF_NOW();

        if ((page->tex == NULL || page->tex->id == 0) &&
            g_atlasResidentPages >= GLES_ATLAS_RESIDENT_PAGES) goto standalone;
        pixels = Sprite_GetPixels(set, sprite);
        if (pixels == NULL) return NULL;

        /* Uploads can change bindings. Flush first to keep queued draws
         * ordered, including calls using a recycled standalone texture. */
        GlesBatch_Flush();
        glActiveTexture(GL_TEXTURE0);
        if (page->tex == NULL) page->tex = GlTex_Create();
        if (page->tex == NULL) goto standalone;
        if (page->tex->id == 0) {
            if (!GlTex_Upload(page->tex, page->h, GL_R8, page->w, GL_RED, NULL)) goto standalone;
            g_atlasResidentPages++;
        }
        gles_clear_errors();
        glBindTexture(GL_TEXTURE_2D, page->tex->id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, region->x, region->y, spr->w, spr->h,
                        GL_RED, GL_UNSIGNED_BYTE, pixels);
        if (!gles_ok()) goto standalone;
        {
            unsigned long long elapsed = BATCHPROF_NOW() - start;

            BatchProf_Split(BATCHPROF_SPLIT_UPLOAD, elapsed);
            BatchProf_Split(BATCHPROF_SPLIT_UPLOAD_SPRITE, elapsed);
        }
        region->uploaded = 1;
        region->generation = g_glesStreamGeneration;
    }

    rect[0] = (float)region->x;
    rect[1] = (float)region->y;
    rect[2] = (float)spr->w;
    rect[3] = (float)spr->h;
    *atlased = 1;
    return page->tex;

standalone:
    return gles_atlas_standalone(set, sprite, spr, region);
}

/*
 * Drops every streaming-texture key. Called when a sprite set is freed, because
 * that releases the decoded pixel buffers the keys point at (gles_render.h).
 */
void Gles_OnSpriteSetFreed(const SpriteSet *set)
{
    int i;

    /* Queued quads may reference this set's textures. */
    GlesBatch_Flush();
    gles_atlas_free(set);

    for (i = 0; i < GLES_STREAM_SLOTS; i++) {
        g_glesStream[i].valid = 0;
        g_glesStream[i].source = NULL;
    }
}

/*
 * The slot that already holds `pixels`, or a recycled one to upload into.
 * *hit reports which, so the caller knows whether an upload is needed.
 */
static GlesStreamSlot *gles_stream_slot(const void *source, int format, int w, int h, int bpp, int *hit)
{
    GlesStreamSlot *free_slot = NULL;
    GlesStreamSlot *lru = NULL;
    int i;

    for (i = 0; i < GLES_STREAM_SLOTS; i++) {
        GlesStreamSlot *slot = &g_glesStream[i];

        if (slot->valid && slot->generation == g_glesStreamGeneration &&
            SpriteTexKey_Matches(slot->source, slot->format, slot->w, slot->h, slot->bpp,
                                 source, format, w, h, bpp)) {
            slot->tick = ++g_glesStreamClock;
            *hit = 1;
            return slot;
        }
        if (!slot->valid) {
            if (free_slot == NULL) {
                free_slot = slot;
            }
        } else if (slot->generation != g_glesStreamGeneration) {
            /* Contents died with the old context: reuse this slot now. */
            if (free_slot == NULL) {
                free_slot = slot;
            }
        } else if (lru == NULL || slot->tick < lru->tick) {
            lru = slot;
        }
    }
    *hit = 0;
    if (free_slot != NULL) {
        return free_slot;
    }
    return lru;
}

static GlTex *gles_palette_texture(const unsigned char *bytes)
{
    unsigned int hash = gles_palette_hash(bytes);
    GlesPalSlot *oldest = &g_glesPalSlots[0];
    unsigned long long upload_start;
    int i;

    for (i = 0; i < GLES_PALETTE_SLOTS; i++) {
        GlesPalSlot *slot = &g_glesPalSlots[i];

        if (slot->last_used != 0 && slot->hash == hash &&
            memcmp(slot->bytes, bytes, sizeof(slot->bytes)) == 0) {
            slot->last_used = ++g_glesPaletteClock;
            return &slot->tex;
        }
        if (slot->last_used < oldest->last_used) {
            oldest = slot;
        }
    }
    if (!gles_register_tex(&oldest->tex)) {
        return NULL;
    }
    oldest->last_used = 0;
    upload_start = BATCHPROF_NOW();
    if (!GlTex_UploadRGBA(1, 0x100, &oldest->tex, bytes)) {
        return NULL;
    }
    BatchProf_Split(BATCHPROF_SPLIT_UPLOAD, BATCHPROF_NOW() - upload_start);
    oldest->hash = hash;
    memcpy(oldest->bytes, bytes, sizeof(oldest->bytes));
    oldest->last_used = ++g_glesPaletteClock;
    return &oldest->tex;
}

/* A new context invalidates every slot's contents. */
static void gles_palette_slots_reset(void)
{
    int i;

    for (i = 0; i < GLES_PALETTE_SLOTS; i++) {
        g_glesPalSlots[i].last_used = 0;
    }
    g_glesPaletteClock = 0;
}

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
    gles_backing_release(tex);
    tex->id = 0;
    tex->gles_internal_format = 0;
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
    /* Uploading rebinds texture units. */
    GlesBatch_Flush();
    GLuint id;
    int fresh, bpp;
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
    /* Port fix: keep the storage format for the lifetime of the texture name. */
    if (!fresh && pixels != NULL && tex->w == w && tex->h == h &&
        tex->gles_internal_format == internal_format) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
    }
    if (!gles_ok()) {
        tex->gles_internal_format = 0;
        if (fresh) glDeleteTextures(1, &id);
        return 0;
    }
    /* Publish only after the driver accepted storage and data. */
    tex->id = id; tex->w = w; tex->h = h;
    tex->gles_internal_format = internal_format;
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

/* Returns the embedded source for a sprite shader by its file name (e.g.
 * "indexed.vert"). The GLES dialects are compiled in (src/shaders/gles/), so
 * there is no data/gles/ file to read; a name with no match returns NULL. The
 * returned pointer is the static literal, not an allocation -- callers must not
 * free it. */
static const char *gles_read_shader(const char *name)
{
    static const struct {
        const char *name;
        const char *src;
    } shaders[] = {
        { "indexed.vert", g_shader_gles_indexed_vert },
        { "indexed.frag", g_shader_gles_indexed_frag },
        { "rgba.vert", g_shader_gles_rgba_vert },
        { "rgba.frag", g_shader_gles_rgba_frag }
    };
    size_t i;

    for (i = 0; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].name, name) == 0) return shaders[i].src;
    }
    /* No trailing newline on purpose: the next GLES line starts with one, so
     * two failures in a row do not leave a blank line between them. */
    Log_Printf("\nGLES: no embedded shader named %s", name);
    return NULL;
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

/* Loads one program from the embedded ES dialect sources. */
static int gles_load_es_program(GlesProgram *prog, const char *name, int atlas)
{
    char vert_name[64];
    char frag_name[64];
    const char *vert;
    const char *frag;
    char defines[256];
    int ok, inverse_size = 0;

    snprintf(vert_name, sizeof(vert_name), "%s.vert", name);
    snprintf(frag_name, sizeof(frag_name), "%s.frag", name);
    vert = gles_read_shader(vert_name);
    frag = gles_read_shader(frag_name);
    if (vert == NULL || frag == NULL) {
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
    if (atlas && (!strstr(vert, "v_atlas_rect") || !strstr(frag, "v_atlas_rect"))) {
        return 0;
    }
    if (atlas) {
        size_t used = strlen(defines);
        inverse_size = prog == &g_glesProgramAtlas &&
                       strstr(vert, "#ifdef SPRITE_ATLAS_INVERSE_SIZE") &&
                       strstr(frag, "#ifdef SPRITE_ATLAS_INVERSE_SIZE");

        snprintf(defines + used, sizeof(defines) - used, "#define SPRITE_ATLAS\n%s",
                 inverse_size ? "#define SPRITE_ATLAS_INVERSE_SIZE\n" : "");
    }
    ok = GlesDraw_LoadProgram(prog, vert, frag, atlas ? "indexed atlas" : name, defines);
    if (prog == &g_glesProgramAtlas) g_indexed_atlas_inverse_size = ok && inverse_size;
    return ok;
}

/* Deletes the sprite textures and the programs. */
static void gles_free_programs(void)
{
    GlesDraw_FreeProgram(&g_glesProgramAtlas);
    GlesDraw_FreeProgram(&g_glesProgramAtlasRgba);
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
    g_batch_count = 0;
    g_atlasResidentPages = 0;
    memset(&g_glesProgramAtlas, 0, sizeof(g_glesProgramAtlas));
    memset(&g_glesProgramAtlasRgba, 0, sizeof(g_glesProgramAtlasRgba));
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
    /* The scratch palette textures' contents go with the context, so the
     * upload cache must not claim they still hold these bytes. */
    gles_palette_slots_reset();
    /* Streaming sprite textures' contents go with the context too. */
    g_glesStreamGeneration++;
    /* In-flight GPU timer queries die with it. */
    GpuTime_Reset();
    /* Every program name is gone, so the cached bind is meaningless. */
    GlesDraw_InvalidateState();

    /* Textures keep their dimensions so GlTex_Upload/GlVbuf_Bind can revive
     * them; only the names and the depth renderbuffer go. */
    for (i = 0; i < g_glesTextureCount; i++) {
        GlTex *tex = g_glesTextures[i];

        if (tex == NULL) {
            continue;
        }
        tex->id = 0;
        tex->gles_internal_format = 0;
        tex->depth = 0;
        tex->size = 0;
    }
    if (was_ready || tracked > 0) {
        Log_Printf("GLES: context lost; object names dropped (%d textures tracked)\n", tracked);
    }
}

/*
 * Rebuilds the renderer on a context that is already current.
 *
 * The caller has created the context and loaded the GLES entry points; this
 * recompiles the programs and re-establishes the GL state. The loss handler
 * cleared the texture names, so the next upload allocates fresh storage.
 * Returns 1 when the renderer is usable
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
    g_indexed_atlas_inverse_size = 0;
#if defined(__linux__) && defined(__aarch64__) && !defined(ANDROID_PORT)
    const char *driver = SDL_GetCurrentVideoDriver();
    const char *renderer = (const char *)glGetString(GL_RENDERER);

    g_limit_stage_pairs = driver && strcmp(driver, "KMSDRM") == 0 &&
                          renderer && strcmp(renderer, "Mali-G31") == 0;
#endif
    if (!gles_load_es_program(&g_glesProgramIndexed, "indexed", 0) ||
        !gles_load_es_program(&g_glesProgramRgba, "rgba", 0) ||
        !GlesDraw_LoadProgram(&g_glesProgramSolid, gles_solid_vert_src, gles_solid_frag_src, "solid", NULL) ||
        g_glesProgramSolid.u_color < 0) {
        Log_Printf("\nGLES: program set unavailable; GL mode unavailable\n");
        gles_free_programs();
        return 0;
    }
    {
        GLint texture_units = 0, vertex_attribs = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &vertex_attribs);
        if (texture_units >= 2 * GLES_ATLAS_TEXTURE_SLOTS &&
            vertex_attribs > GLES_ATTR_ATLAS_FX &&
            glDrawArraysInstanced != NULL && glVertexAttribDivisor != NULL &&
            gles_load_es_program(&g_glesProgramAtlas, "indexed", 1) &&
            g_glesProgramAtlas.u_mvp < 0) {
            if (!gles_load_es_program(&g_glesProgramAtlasRgba, "rgba", 1) || g_glesProgramAtlasRgba.u_mvp >= 0)
                GlesDraw_FreeProgram(&g_glesProgramAtlasRgba);
            Log_Printf("\nGLES: native sprite atlas available\n");
        } else {
            GlesDraw_FreeProgram(&g_glesProgramAtlas);
            Log_Printf("\nGLES: indexed sprite atlas unavailable; using standalone sprites\n");
        }
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
#ifdef MUGEN_DEBUG
    /* What this driver actually offers, so optimisation decisions rest on the
     * device instead of on assumptions (gles_probe.h). */
    Gles_LogCapabilities();
#endif
    /* GPU-side timing, when the driver can do it (gputime.h). */
    GpuTime_Init();
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
    glViewport(0, 0, g_glBoundFramebuffer ? g_glesTargetWidth : g_targetWidth,
                     g_glBoundFramebuffer ? g_glesTargetHeight : g_targetHeight);
    return 1;
}

void Gl_ResetModelview(void)
{
    /* Nothing on a stack to reset: the modelview is built per draw. */
}

void Gl_ApplyClipRect(void)
{
    /* The scissor is about to change: queued quads belong to the old one. */
    GlesBatch_Flush();
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
    } else if (g_glBoundFramebuffer && g_targetWidth > 0 && g_targetHeight > 0 &&
               (g_glesTargetWidth != g_targetWidth || g_glesTargetHeight != g_targetHeight)) {
        int x1 = (int)floor((double)g_clipX1 * g_glesTargetWidth / g_targetWidth);
        int x2 = (int)ceil((double)g_clipX2 * g_glesTargetWidth / g_targetWidth);
        int y1 = (int)floor((double)g_clipY1 * g_glesTargetHeight / g_targetHeight);
        int y2 = (int)ceil((double)g_clipY2 * g_glesTargetHeight / g_targetHeight);

        glScissor(x1, y1, g_clipX2 == g_clipX1 ? 0 : x2 - x1,
                         g_clipY2 == g_clipY1 ? 0 : y2 - y1);
    } else {
        glScissor(g_clipX1, g_clipY1, g_clipX2 - g_clipX1, g_clipY2 - g_clipY1);
    }
}

/* Draws one solid rectangle through the solid program. */
static void gles_solid_quad(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    /* A solid quad is not a queued sprite: queued quads must land first. */
    GlesBatch_Flush();
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
    /* Bound here because the colour uniform is set before the quad call, which
     * would otherwise rebind from its cache and leave the cache stale. */
    GlesDraw_InvalidateState();
    glUseProgram(g_glesProgramSolid.program);
    BatchProf_Program(g_glesProgramSolid.program);
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
    GlesBatch_Flush();
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
    GlesBatch_Flush();
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

/* Port addition: FIFO sprite submission. Standalone quads replay one at a
 * time. Atlas quads with compatible consecutive state share an instanced
 * draw. Flush before operations that affect recorded state or textures. */
#define GLES_BATCH_MAX GLES_MAX_ATLAS_INSTANCES

typedef struct GlesSpriteCall {
    /* batch key */
    const GlesProgram *prog;
    int limit_pairs;
    int blend_enabled;
    unsigned int blend_src, blend_dst, blend_eq;
    int mask;
    /* what to draw */
    const GlTex *tex;
    const GlTex *pal;
    int paletted;
    float mvp[16];
    float pos[8];
    float tc[16];
    float add[3], mul[3];
    float colorbal, alphamul;
    int invertall;
    float texsize[2];
    float alpha_dst;
    float atlas_rect[4];
    /* the clip rectangle in force when the call was recorded */
    int clip[4];
} GlesSpriteCall;

static GlesSpriteCall g_batch[GLES_BATCH_MAX];
static int g_batch_enabled = -2;    /* -2 unread, -1 use renderer default */
/* Re-entrancy guard: the replay calls Gl_ApplyClipRect, which itself flushes. */
static int g_batch_flushing;

static int gles_batch_enabled(int native_atlas)
{
    if (g_batch_enabled == -2) {
        const char *v = getenv("MUGEN_SPRITEBATCH");

        /* Compatible ordinary and atlas sprites use the FIFO by default.
         * The override keeps a one-draw-per-sprite comparison path. */
        g_batch_enabled = v != NULL ? strcmp(v, "1") == 0 : g_spriteBatchDefault;
    }
    return g_batch_enabled >= 0 ? g_batch_enabled : native_atlas || (gles_atlas_requested() && g_glesProgramAtlas.program != 0);
}

static int gles_atlas_can_join(const GlesSpriteCall *first, const GlesSpriteCall *next)
{
    return next->prog == first->prog &&
           next->limit_pairs == first->limit_pairs &&
           next->mask == first->mask && next->blend_enabled == first->blend_enabled &&
           next->blend_src == first->blend_src && next->blend_dst == first->blend_dst &&
           /* Source alpha is an instance shader value. The blend state only
            * reads glBlendColor's alpha_dst through GL_CONSTANT_ALPHA. */
           next->blend_eq == first->blend_eq &&
           next->alpha_dst == first->alpha_dst &&
           memcmp(next->clip, first->clip, sizeof(first->clip)) == 0;
}

static int gles_atlas_submit_run(int start)
{
    const GlesSpriteCall *first = &g_batch[start];
    static const float corners[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    GlesAtlasBatch batch;
    const GlTex *pages[GLES_ATLAS_TEXTURE_SLOTS];
    const GlTex *palettes[GLES_ATLAS_TEXTURE_SLOTS];
    int slots = 0;
    int end = start;
    /* Keep the two-pair guard for older and RGBA stage shaders on Mali-G31. */
    int fast_indexed = first->prog == &g_glesProgramAtlas && g_indexed_atlas_inverse_size;
    int slot_limit = first->limit_pairs && !fast_indexed ? 2 : GLES_ATLAS_TEXTURE_SLOTS;

    memcpy(batch.corners, corners, sizeof(corners));
    while (end < g_batch_count) {
        const GlesSpriteCall *call = &g_batch[end];
        int slot;

        if (end != start && !gles_atlas_can_join(first, call)) break;
        for (slot = 0; slot < slots; ++slot)
            if (pages[slot] == call->tex && palettes[slot] == call->pal) break;
        if (slot == slots) {
            if (slots == slot_limit) break;
            pages[slots] = call->tex;
            palettes[slots] = call->pal;
            ++slots;
        }

        memcpy(batch.sprites[end - start].mvp, call->mvp, sizeof(call->mvp));
        memcpy(batch.sprites[end - start].rect, call->atlas_rect, sizeof(call->atlas_rect));
        batch.sprites[end - start].params[0] = call->texsize[0];
        batch.sprites[end - start].params[1] = call->texsize[1];
        batch.sprites[end - start].params[2] = (float)slot;
        batch.sprites[end - start].params[3] = 0.0f;
        memcpy(batch.sprites[end - start].add, call->add, sizeof(call->add));
        batch.sprites[end - start].add[3] = call->alphamul;
        memcpy(batch.sprites[end - start].mul, call->mul, sizeof(call->mul));
        batch.sprites[end - start].mul[3] = call->colorbal;
        batch.sprites[end - start].fx[0] = (float)call->invertall;
        batch.sprites[end - start].fx[1] = 0.0f;
        batch.sprites[end - start].fx[2] = 0.0f;
        batch.sprites[end - start].fx[3] = 0.0f;
        end++;
    }

    /* Bind the distinct sprite/palette pairs to their fixed sampler units.
     * Pair identity is not part of the batch key, so painter's order stays
     * intact across texture and palette changes until the seven-slot budget. */
    for (int slot = 0; slot < slots; ++slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, (GLuint)pages[slot]->id);
        if (palettes[slot]) {
            glActiveTexture(GL_TEXTURE0 + GLES_ATLAS_TEXTURE_SLOTS + slot);
            glBindTexture(GL_TEXTURE_2D, (GLuint)palettes[slot]->id);
        }
    }
    glActiveTexture(GL_TEXTURE0);
    BatchProf_Texture(0, pages[0]->id);
    if (palettes[0]) BatchProf_Texture(1, palettes[0]->id);

    GlesDraw_Atlas(&g_glesBuffer, first->prog, &batch, end - start, first->mask, first->add, first->mul,
                   first->invertall, first->colorbal, first->texsize[0], first->texsize[1], first->alphamul);
    return end;
}

void GlesBatch_Flush(void)
{
    int saved_clip[4];
    int prev_clip[4] = { -1, -1, -1, -1 };
    int i;

    if (g_batch_count == 0 || g_batch_flushing) {
        return;
    }
    g_batch_flushing = 1;
    saved_clip[0] = g_clipX1;
    saved_clip[1] = g_clipY1;
    saved_clip[2] = g_clipX2;
    saved_clip[3] = g_clipY2;

    for (i = 0; i < g_batch_count;) {
        const GlesSpriteCall *c = &g_batch[i];

        /* The scissor the call was recorded with. */
        if (c->clip[0] != prev_clip[0] || c->clip[1] != prev_clip[1] ||
            c->clip[2] != prev_clip[2] || c->clip[3] != prev_clip[3]) {
            g_clipX1 = c->clip[0];
            g_clipY1 = c->clip[1];
            g_clipX2 = c->clip[2];
            g_clipY2 = c->clip[3];
            Gl_ApplyClipRect();
            prev_clip[0] = c->clip[0];
            prev_clip[1] = c->clip[1];
            prev_clip[2] = c->clip[2];
            prev_clip[3] = c->clip[3];
        }

        /* Blend, exactly as the immediate path set it. */
        if (!c->blend_enabled) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc((GLenum)c->blend_src, (GLenum)c->blend_dst);
            glBlendColor(c->alphamul, c->alphamul, c->alphamul, c->alpha_dst);
            glBlendEquation((GLenum)c->blend_eq);
        }
        if (c->prog == &g_glesProgramAtlas || c->prog == &g_glesProgramAtlasRgba) {
            i = gles_atlas_submit_run(i);
        } else {
            /* Standalone shaders still use units 0 and 1. */
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, (GLuint)c->tex->id);
            BatchProf_Texture(0, (unsigned int)c->tex->id);
            if (c->pal != NULL) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, (GLuint)c->pal->id);
                BatchProf_Texture(1, (unsigned int)c->pal->id);
                glActiveTexture(GL_TEXTURE0);
            }
            GlesDraw_Quad(&g_glesBuffer, c->prog, c->mvp, c->pos, c->tc, c->paletted, c->mask,
                          c->add[0], c->add[1], c->add[2], c->mul[0], c->mul[1], c->mul[2],
                          c->invertall, c->colorbal, c->texsize[0], c->texsize[1], c->alphamul);
            i++;
        }
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    g_batch_count = 0;
    g_batch_flushing = 0;
    g_clipX1 = saved_clip[0];
    g_clipY1 = saved_clip[1];
    g_clipX2 = saved_clip[2];
    g_clipY2 = saved_clip[3];
    Gl_ApplyClipRect();
}

static void gles_batch_record(const GlesProgram *prog, int blend_enabled, unsigned int blend_src,
                      unsigned int blend_dst, unsigned int blend_eq, int mask, const GlTex *tex,
                      const GlTex *pal, int paletted, const float *mvp16, const float *positions8,
                      const float *texcoords16, float add_r, float add_g, float add_b, float mul_r,
                      float mul_g, float mul_b, int invertall, float colorbal, float tex_w,
                      float tex_h, float alpha_src, float alpha_dst, const float *atlas_rect)
{
    GlesSpriteCall *c;

    if (g_batch_count == GLES_BATCH_MAX) {
        GlesBatch_Flush();
    }
    c = &g_batch[g_batch_count++];
    c->prog = prog;
    c->limit_pairs = 0;
    c->blend_enabled = blend_enabled;
    c->blend_src = blend_src;
    c->blend_dst = blend_dst;
    c->blend_eq = blend_eq;
    c->mask = mask;
    c->tex = tex;
    c->pal = pal;
    c->paletted = paletted;
    memcpy(c->mvp, mvp16, sizeof(c->mvp));
    memcpy(c->pos, positions8, sizeof(c->pos));
    memcpy(c->tc, texcoords16, sizeof(c->tc));
    c->add[0] = add_r; c->add[1] = add_g; c->add[2] = add_b;
    c->mul[0] = mul_r; c->mul[1] = mul_g; c->mul[2] = mul_b;
    c->colorbal = colorbal;
    c->alphamul = alpha_src;
    c->invertall = invertall;
    c->texsize[0] = tex_w;
    c->texsize[1] = tex_h;
    c->alpha_dst = alpha_dst;
    c->clip[0] = g_clipX1;
    c->clip[1] = g_clipY1;
    c->clip[2] = g_clipX2;
    c->clip[3] = g_clipY2;
    if (atlas_rect != NULL) memcpy(c->atlas_rect, atlas_rect, sizeof(c->atlas_rect));
}

/*
 * Port addition: would this quad draw anything?
 *
 * The draw path used to submit every sprite it was handed; nothing tested whether
 * the quad landed inside the clip rectangle. Measured with the counter below,
 * 15% of the quads in a 2D match and 43% on the title screen were entirely
 * outside the scissor -- a driver call, a vertex upload and a palette upload each,
 * for nothing.
 *
 * This projects the four corners with the same matrix the shader uses and reports
 * whether the transformed box misses the clip rectangle completely, or has no
 * area. Under MUGEN_DEBUG it also counts what it rejects, which is how the figure
 * above was obtained.
 */
static int gles_quad_is_culled(const float *mvp16, const float *pos8)
{
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    int all_behind = 1;
    int i;

    for (i = 0; i < 4; i++) {
        float x = pos8[i * 2];
        float y = pos8[i * 2 + 1];
        float cx = mvp16[0] * x + mvp16[4] * y + mvp16[12];
        float cy = mvp16[1] * x + mvp16[5] * y + mvp16[13];
        float cw = mvp16[3] * x + mvp16[7] * y + mvp16[15];

        if (cw > 0.0f) {
            float nx = cx / cw;
            float ny = cy / cw;

            all_behind = 0;
            if (nx < minx) { minx = nx; }
            if (nx > maxx) { maxx = nx; }
            if (ny < miny) { miny = ny; }
            if (ny > maxy) { maxy = ny; }
        }
    }
    if (all_behind) {
        return 1;
    }
    /* NDC -> pixels, the inverse of what the projection and viewport apply. */
    {
        float px1 = (minx * 0.5f + 0.5f) * (float)g_targetWidth;
        float px2 = (maxx * 0.5f + 0.5f) * (float)g_targetWidth;
        float py1 = (0.5f - maxy * 0.5f) * (float)g_targetHeight;
        float py2 = (0.5f - miny * 0.5f) * (float)g_targetHeight;

        if (px2 < (float)g_clipX1 || px1 > (float)g_clipX2 ||
            py2 < (float)g_clipY1 || py1 > (float)g_clipY2) {
            return 2;               /* outside the clip rectangle */
        }
        if ((px2 - px1) < 0.01f || (py2 - py1) < 0.01f) {
            return 3;               /* no area */
        }
    }
    return 0;
}

void Gl_DrawSprite(int sprite, const DrawParams *dp, SpriteSet *set)
{
    Sprite *spr;
    GlTex *tex = NULL;
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
    int paletted, atlased = 0, instanced = 0, defer;
    float atlas_rect[4];
    int blend_enabled = 0;
    unsigned int blend_src = 0, blend_dst = 0, blend_eq = 0;
    /* MUGEN_DEBUG: the texture uploads are the part of a sprite draw that
     * happens before GlesDraw_Quad, so they need their own timer. */
    unsigned long long bp_upload = BATCHPROF_NOW();

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
    /*
     * Port addition: skip a quad the clip rectangle would reject outright,
     * before its texture or palette is even uploaded.
     */
    {
        int culled = gles_quad_is_culled(mvp.m, positions);

#ifdef MUGEN_DEBUG
        BatchProf_CountQuad(culled == 2, culled == 3);
#endif
        if (culled != 0) {
            return;
        }
    }
    GlesProgram *atlas_program = paletted ? &g_glesProgramAtlas : &g_glesProgramAtlasRgba;
    if (spr->format == SPRITE_FORMAT_ATLAS && !special && atlas_program->program) {
        tex = Sff3_Texture(spr, atlas_rect);
        atlased = tex != NULL;
    } else if (paletted) tex = gles_atlas_texture(set, sprite, spr, !special, atlas_rect, &atlased);
    if (!atlased && spr->format == SPRITE_FORMAT_TEXTURE) tex = spr->pixels;
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
        unsigned long long bp_px = BATCHPROF_NOW();
        void *pixels = Sprite_GetPixels(set, sprite);
        { unsigned long long t = BATCHPROF_NOW(); BatchProf_Split(5, t - bp_px); }

        if (pixels == NULL) {
            return;
        }
        /*
         * One texture per distinct sprite, not one scratch texture per draw:
         * see g_glesStream above for the measurement that made this necessary.
         */
        {
            GlesStreamSlot *slot;
            int hit = 0;

            /* Key on the sprite's own source, not on the pooled decode. */
            slot = gles_stream_slot(spr->pixels, spr->format, spr->w, spr->h, spr->bpp, &hit);
            if (slot == NULL) {
                return;
            }
            tex = &slot->tex;
            /* These GlTex structs are statics too, so register them here or a
             * context loss would leave them with dead names forever. */
            if (!gles_register_tex(tex)) {
                return;
            }
            if (!hit) {
                bp_upload = BATCHPROF_NOW();
                int w = spr->w, h = spr->h;
                void *scaled = NULL;
                if (spr->format == SPRITE_FORMAT_ATLAS &&
                    (w > g_glMaxTextureSize || h > g_glMaxTextureSize)) {
                    int sx = w > g_glMaxTextureSize ? Sprite_TextureStep(w, g_glMaxTextureSize) : 1;
                    int sy = h > g_glMaxTextureSize ? Sprite_TextureStep(h, g_glMaxTextureSize) : 1;
                    scaled = Sprite_Downscale(pixels, w, h, sx, sy, spr->bpp);
                    if (!scaled) return;
                    w /= sx;
                    h /= sy;
                }
                int uploaded = GlTex_Upload2D(h, spr->bpp, tex, scaled ? scaled : pixels, w);
                free(scaled);
                if (!uploaded) {
                    return;
                }
                {
                    unsigned long long now = BATCHPROF_NOW();

                    BatchProf_Split(BATCHPROF_SPLIT_UPLOAD, now - bp_upload);
                    BatchProf_Split(BATCHPROF_SPLIT_UPLOAD_SPRITE, now - bp_upload);
                    bp_upload = now;
                }
                slot->source = spr->pixels;
                slot->format = spr->format;
                slot->w = spr->w;
                slot->h = spr->h;
                slot->bpp = spr->bpp;
                slot->generation = g_glesStreamGeneration;
                slot->valid = 1;
            }
        }
    }

    if (paletted) {
        Palette *pal;

        {
            unsigned long long bp_pal = BATCHPROF_NOW();
            pal = SpriteSet_GetPaletteConverted(set, spr->pal_index);
            { unsigned long long t = BATCHPROF_NOW(); BatchProf_Split(6, t - bp_pal); }
        }

        if (pal == NULL) {
            Log_Printf("GLES: sprite %d has no usable palette; draw skipped\n", sprite);
            return;
        }
        {
            unsigned char padded[0x100 * 4];
            int n = pal->count;
            const void *colors = pal->data;
            const unsigned char *bytes;
            /*
             * Port addition: one GPU texture per distinct palette, uploaded
             * once, instead of re-uploading a single scratch texture on every
             * draw.
             *
             * Measured on an RG351MP: the palette upload was the single largest
             * cost in a frame -- ~3.5 ms of a ~13 ms draw for ~19 sprites,
             * because the driver defers the upload's work to the next draw. The
             * palette for a given (set, pal_index) is stable, so almost every
             * one of those uploads re-sent bytes the GPU already had.
             *
             * The cache key is a hash of the pixels rather than the palette
             * pointer: Pal_Convert writes rgb565 and PalFX is applied through
             * uniforms, so the RGBA data is not expected to change in place --
             * and comparing the cached bytes handles changes and hash
             * collisions. A context loss invalidates the cache.
             */
            if (n < 0 || n > 0x100) {
                return;
            }
            if (n == 0x100) {
                if (colors == NULL) {
                    return;             /* no colour data to upload */
                }
                bytes = (const unsigned char *)colors;
            } else {
                memset(padded, 0, sizeof(padded));
                if (n > 0 && colors != NULL) {
                    memcpy(padded, colors, (size_t)n * 4);
                }
                bytes = padded;
            }
            pal_tex = gles_palette_texture(bytes);
            if (pal_tex == NULL) {
                return;
            }
        }
    }

    /* The instanced shader can also address a whole standalone texture as a
     * rectangle at (0, 0). This lets ordinary sprites share a draw with v3
     * pages and with other standalone sprites while preserving painter order.
     * The special quad path needs its own geometry and projective UVs, and an
     * oversized sprite may have been downscaled for its standalone texture. */
    if (!special && atlas_program->program &&
        (atlased || (tex->w == spr->w && tex->h == spr->h))) {
        instanced = 1;
        if (!atlased) {
            atlas_rect[0] = 0.0f;
            atlas_rect[1] = 0.0f;
            atlas_rect[2] = (float)spr->w;
            atlas_rect[3] = (float)spr->h;
        }
    }
    fx = PalFx_GetApplied();
    prog = instanced ? atlas_program : (paletted ? &g_glesProgramIndexed : &g_glesProgramRgba);
    defer = gles_batch_enabled(instanced);
    if (!defer && !instanced) GlesBatch_Flush();

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

        blend_enabled = blend.enabled;
        blend_src = (unsigned int)gles_gl_factor(blend.src);
        blend_dst = (unsigned int)gles_gl_factor(blend.dst);
        blend_eq = (unsigned int)(blend.eq == GLES_BLEND_EQ_REVERSE_SUBTRACT ? GL_FUNC_REVERSE_SUBTRACT
                                                                           : GL_FUNC_ADD);
    }

    /*
     * Port addition: queue the quad instead of submitting it, so a flush can
     * merge neighbours into one instanced draw. Same values either way; with
     * batching off this is exactly the old straight-through submission.
     */
    if (defer || instanced) {
        /* PalFX scalars exactly as Glr_FxTerms computes them: add is 0..255 and
         * mul / color are fixed point with 256 = 1.0. */
        gles_batch_record(prog, blend_enabled, blend_src, blend_dst, blend_eq, dp->masked,
                         tex, pal_tex, paletted, mvp.m, positions, texcoords,
                         (float)fx->add[0] / 255.0f, (float)fx->add[1] / 255.0f, (float)fx->add[2] / 255.0f,
                         (float)fx->mul[0] * (1.0f / 256.0f), (float)fx->mul[1] * (1.0f / 256.0f),
                         (float)fx->mul[2] * (1.0f / 256.0f), fx->invertall,
                         (float)fx->color * (1.0f / 256.0f), (float)tex->w, (float)tex->h,
                         dp->alpha_src, dp->alpha_dst, instanced ? atlas_rect : NULL);
        g_batch[g_batch_count - 1].limit_pairs =
            g_limit_stage_pairs && g_game && g_game->stage_bg && set == g_game->stage_bg->sprites;
        if (!defer) GlesBatch_Flush();
        return;
    }

    if (!blend_enabled) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
        glBlendColor(dp->alpha_src, dp->alpha_src, dp->alpha_src, dp->alpha_dst);
        glBlendEquation((GLenum)blend_eq);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex->id);
    BatchProf_Texture(0, (unsigned int)tex->id);
    if (pal_tex != NULL) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (GLuint)pal_tex->id);
        BatchProf_Texture(1, (unsigned int)pal_tex->id);
        glActiveTexture(GL_TEXTURE0);
    }

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
    /* GPU-side timing of the present blit; the swap itself is not GL work. */
    GpuTime_BeginPresent();
    if (set != NULL) {
        if (set->sprites == NULL || set->sprites->data == NULL || sprite < 0 || sprite >= set->sprites->capacity) { goto present_out; }
        spr = (Sprite *)(set->sprites->data + set->sprites->elem_size * sprite);
        if (spr->format != SPRITE_FORMAT_TEXTURE || spr->pixels == NULL) { goto present_out; }
        tex = spr->pixels;
        if (tex->id == 0) { goto present_out; }
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
        goto present_out;
    }
    /* Everything queued belongs to the frame being presented. */
    GlesBatch_Flush();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    g_glProjection = 0; /* Overlays and the screen quad use video coordinates. */
    if (!Gfx_SetRenderTarget(NULL, 0)) { g_glProjection = saved_projection; goto present_out; }
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
    GlesBatch_Flush();
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
    /* Everything the present blit issues to the GPU has been submitted. */
    GpuTime_EndPresent();
    if ((g_videoFlags & VIDEO_HWBLIT) && (g_videoSdlFlags & DISPLAY_DOUBLEBUF)) {
        unsigned long long swap_start = Perf_TraceSwapBegin();

        Video_SwapBuffers();
        Perf_TraceSwapEnd(swap_start);
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
present_out:
    /* Early exits land here: end the GPU span and skip the frame entirely. */
    GpuTime_EndPresent();
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
    int read_w = g_glBoundFramebuffer ? g_glesTargetWidth : w;
    int read_h = g_glBoundFramebuffer ? g_glesTargetHeight : h;
    int y;
    size_t row, bytes;

    if (!g_glesReady || read_w <= 0 || read_h <= 0 || read_w > w || read_h > h ||
        !GlImage_Size(w, h, 4, &row, &bytes)) {
        return NULL;
    }
    GlesBatch_Flush();
    rgba = malloc(bytes);
    if (rgba == NULL) {
        return NULL;
    }
    gles_clear_errors();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, read_w, read_h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!gles_ok()) { free(rgba); return NULL; }

    /* Keep captures at logical size. Walk backward so nearest-neighbor
     * expansion cannot overwrite a source pixel still needed below it. */
    if (read_w != w || read_h != h) {
        uint32_t *pixels = (uint32_t *)rgba;
        for (y = h - 1; y >= 0; --y) {
            size_t src_row = (size_t)((2LL * y + 1) * read_h / (2LL * h)) * read_w;
            for (int x = w - 1; x >= 0; --x) {
                size_t src_x = (size_t)((2LL * x + 1) * read_w / (2LL * w));
                pixels[(size_t)y * w + x] = pixels[src_row + src_x];
            }
        }
    }

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
        return Gl_SetupView();
    }
    if (!Gl_AttachFramebuffer(vbuf->id, vbuf->depth)) return 0;
    g_glesTargetWidth = vbuf->w;
    g_glesTargetHeight = vbuf->h;
    return Gl_SetupView();
}

#endif /* MUGEN_GLES */
