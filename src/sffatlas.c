/* SFF 3.00, a mugen-unleashed extension. See docs/SFF3.md. */
#include "mugen.h"
#include "sffatlas.h"
#ifdef MUGEN_GLES
#include "gles_render.h"
#endif
#include <limits.h>
#include <stdlib.h>

typedef struct AtlasPage {
    unsigned w, h, depth, codec, offset, stored, decoded;
    unsigned char *pixels;
    GlTex *texture;
    struct AtlasPage *resident_next;
    uint64_t last_draw;
} AtlasPage;

typedef struct AtlasRegion {
    AtlasPage *page;
    unsigned left, top, w, h, depth;
    unsigned char *crop; /* allocated only for consumers requiring contiguous rows */
} AtlasRegion;

typedef struct SpriteAtlas {
    unsigned source, page_count, sprite_count;
    AtlasPage *pages;
    AtlasRegion *regions;
    struct SpriteAtlas *next;
} SpriteAtlas;

static SpriteAtlas *atlases;
static AtlasPage *resident_pages;
static uint64_t draw_serial;

static unsigned word(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t dword(const unsigned char *p)
{
    return word(p) | (uint32_t)word(p+2) << 16;
}

static int span(unsigned offset, uint64_t size, unsigned limit)
{
    return offset <= limit && size <= limit - offset;
}

static int read_at(Stream *stream, long start, unsigned offset, void *data, size_t length)
{
    return !Stream_Seek(stream, start + (long)offset, SEEK_SET) &&
           Stream_Read(data, 1, length, stream) == length;
}

unsigned Sff_SourceVersion(const SpriteSet *set)
{
    while (set && set->shared) set = set->shared;
    if (!set) return 0;
    if (set->version == SFF_VERSION_300 && set->data)
        return ((SpriteAtlas *)set->data)->source;
    return set->version;
}

static void release_texture(AtlasPage *page)
{
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
    if (page->texture) {
        AtlasPage **link = &resident_pages;
        while (*link && *link != page) link = &(*link)->resident_next;
        if (*link) *link = page->resident_next;
#ifdef MUGEN_GLES
        GlesBatch_Flush();
#else
        GlBatch_Flush();
#endif
        GlTex_Free(page->texture);
    }
#endif
    page->texture = NULL;
}

void Sff3_ForgetTextures(void)
{
    SpriteAtlas *a;
    for (a = atlases; a; a = a->next)
        for (unsigned i = 0; i < a->page_count; ++i) release_texture(&a->pages[i]);
}

void Sff3_Release(SpriteSet *set)
{
    if (set->version != SFF_VERSION_300 || set->shared || !set->data) return;
    SpriteAtlas *a = set->data, **link = &atlases;
    while (*link && *link != a) link = &(*link)->next;
    if (*link) *link = a->next;
    if (a->pages) {
        for (unsigned i = 0; i < a->page_count; i++) {
            release_texture(&a->pages[i]);
            free(a->pages[i].pixels);
        }
    }
    if (a->regions)
        for (unsigned i = 0; i < a->sprite_count; i++) free(a->regions[i].crop);
    free(a->pages);
    free(a->regions);
    free(a);
    set->data = NULL;
}

void Sff3_MemUse(const SpriteSet *set, unsigned *sys, unsigned *tex)
{
    const SpriteAtlas *a = set->data;
    if (set->version != SFF_VERSION_300 || set->shared || !a) return;
    *sys += sizeof(*a) + a->page_count * sizeof(AtlasPage) + a->sprite_count * sizeof(AtlasRegion);
    for (unsigned i = 0; i < a->page_count; ++i) {
        const AtlasPage *p = &a->pages[i];
        if (p->pixels) *sys += p->decoded;
        if (p->texture) *tex += p->texture->size;
    }
    for (unsigned i = 0; i < a->sprite_count; ++i) {
        const AtlasRegion *r = &a->regions[i];
        if (r->crop) *sys += r->w * r->h * (r->page->depth / 8);
    }
}

void *Sff3_Pixels(const Sprite *sprite)
{
    AtlasRegion *r = sprite->pixels;
    if (!r->crop) {
        size_t step = r->page->depth / 8, stride = r->page->w * step, bytes = r->w * step;
        const unsigned char *src = r->page->pixels + stride * r->top + step * r->left;
        r->crop = malloc(bytes * r->h);
        if (!r->crop) return NULL;
        for (unsigned y = 0; y < r->h; ++y)
            memcpy(r->crop + y * bytes, src + y * stride, bytes);
    }
    return r->crop;
}

GlTex *Sff3_Texture(const Sprite *sprite, float rect[4])
{
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
    if (sprite->format != SPRITE_FORMAT_ATLAS) return NULL;
    AtlasRegion *r = sprite->pixels;
    AtlasPage *p = r->page;
    if ((int)p->w > g_glMaxTextureSize || (int)p->h > g_glMaxTextureSize) return NULL;
    if (!p->texture || !p->texture->id) {
        /* Bound GPU residency independently of the archive's page count.
         * CPU pages survive eviction and context loss. */
        if (!p->texture) {
            uint64_t bytes = p->decoded;
            unsigned count = 0;
            for (AtlasPage *q = resident_pages; q; q = q->resident_next) {
                bytes += q->decoded;
                count++;
            }
            while (resident_pages && (count >= 16 || bytes > 16u*1024*1024)) {
                AtlasPage *oldest = resident_pages;
                for (AtlasPage *q = oldest->resident_next; q; q = q->resident_next)
                    if (q->last_draw < oldest->last_draw) oldest = q;
                bytes -= oldest->decoded;
                --count;
                release_texture(oldest);
            }
            p->texture = GlTex_Create();
            if (p->texture) {
                p->resident_next = resident_pages;
                resident_pages = p;
            }
        }
        if (!p->texture || !GlTex_Upload2D(p->h, p->depth, p->texture, p->pixels, p->w)) {
            release_texture(p);
            return NULL;
        }
    }
    p->last_draw = ++draw_serial;
    rect[0] = (float)r->left; rect[1] = (float)r->top;
    rect[2] = (float)r->w; rect[3] = (float)r->h;
    return p->texture;
#else
    return NULL;
#endif
}

static int decode_page(AtlasPage *p, Stream *s, long start, unsigned source)
{
    if (p->pixels) return 1;
    p->pixels = malloc(p->decoded);
    if (!p->pixels) return 0;
    if (!p->codec) {
        if (!read_at(s, start, p->offset, p->pixels, p->stored)) return 0;
    } else {
        unsigned char *data = malloc(p->stored);
        int ok = data && read_at(s, start, p->offset, data, p->stored) && dword(data) == p->decoded;
        unsigned pos = 4, out = 0;
        while (ok && pos < p->stored) {
            unsigned value = data[pos++], count = 1;
            if ((value & 0xc0) == 0x40) {
                count = value & 63;
                if (!count || pos == p->stored) { ok = 0; break; }
                value = data[pos++];
            }
            if (count > p->decoded - out) { ok = 0; break; }
            memset(p->pixels + out, value, count);
            out += count;
        }
        free(data);
        if (!ok || out != p->decoded) return 0;
    }
    /* The v1 PCX loader reverses its three planes on every backend. */
    if (source == SFF_VERSION_101 && p->depth == 24) {
        for (unsigned i = 0; i < p->decoded; i += 3) {
            unsigned char tmp = p->pixels[i];
            p->pixels[i] = p->pixels[i+2]; p->pixels[i+2] = tmp;
        }
    }
    return 1;
}

static int load_palettes(SpriteSet *set, Stream *s, long start, const Sff2Header *h, unsigned limit)
{
    for (unsigned i = 0; i < h->palette_count; i++) {
        unsigned char n[16], colors[1024];
        Palette pal;
        if (!read_at(s, start, h->palette_offset + 16*i, n, sizeof(n))) return 0;
        unsigned size = dword(n+12), count = word(n+4);
        if (size) {
            unsigned offset = dword(n+8);
            if (!count || count > 256 || size < count*4 ||
                !span(offset, size, limit - h->tdata_offset) ||
                !read_at(s, start, h->tdata_offset + offset, colors, count*4) ||
                !Pal_Init(&pal, count, colors, PAL_COPY)) return 0;
        } else {
            unsigned link = word(n+6);
            if (link >= i) return 0;
            pal = ((Palette *)set->palettes->data)[link];
            pal.owns = 0;
        }
        if (!SpriteSet_AddPalette(set, &pal, word(n), word(n+2))) {
            Pal_Free(&pal);
            return 0;
        }
    }
    return 1;
}

static int selected(const int *selection, int group, int number)
{
    if (!selection || selection[0] == SFF_SELECT_END) return 1;
    for (; selection[0] != SFF_SELECT_END; selection += 2)
        if (selection[0] == group && selection[1] == number) return 1;
    return 0;
}

SpriteSet *Sff3_Load(Stream *s, long start, const Sff2Header *h, const int *selection)
{
    long end = Stream_GetSize(s);
    if (start < 0 || end < start || (uint64_t)(end-start) > UINT32_MAX) return NULL;
    unsigned limit = (unsigned)(end-start), pages = h->ldata_size, data = h->tdata_offset;
    if (h->compat_version != SFF_VERSION_300 ||
        (h->reserved1 != SFF_VERSION_101 && h->reserved1 != SFF_VERSION_200 && h->reserved1 != SFF_VERSION_201) ||
        h->sprite_offset < SFF_HEADER_SIZE || !span(h->sprite_offset, (uint64_t)h->sprite_count*40, h->palette_offset) ||
        !span(h->palette_offset, (uint64_t)h->palette_count*16, h->ldata_offset) ||
        !span(h->ldata_offset, (uint64_t)pages*28, data) || data > limit)
        return NULL;

    SpriteSet *set = calloc(1, sizeof(*set));
    SpriteAtlas *a = calloc(1, sizeof(*a));
    unsigned *roots = calloc(h->sprite_count, sizeof(*roots));
    if (!set || !a) { free(set); free(a); free(roots); return NULL; }
    set->version = SFF_VERSION_300;
    set->data = a;
    a->page_count = pages; a->sprite_count = h->sprite_count; a->source = h->reserved1;
    a->pages = calloc(pages, sizeof(*a->pages));
    a->regions = calloc(a->sprite_count, sizeof(*a->regions));
    if ((pages && !a->pages) || (a->sprite_count && (!a->regions || !roots))) goto fail;
    if (h->palette_count && !(set->palettes = KeyedArray_Create(h->palette_count, sizeof(Palette)))) goto fail;
    if (!load_palettes(set, s, start, h, limit)) goto fail;
    for (unsigned i = 0; i < pages; i++) {
        unsigned char n[28];
        AtlasPage *p = &a->pages[i];
        if (!read_at(s, start, h->ldata_offset + 28*i, n, sizeof(n))) goto fail;
        p->w = dword(n); p->h = dword(n+4); p->depth = dword(n+8); p->codec = dword(n+12);
        p->offset = dword(n+16); p->stored = dword(n+20); p->decoded = dword(n+24);
        if (!p->w || !p->h || p->w > 32769 || p->h > 32769 ||
            (p->depth != 8 && p->depth != 24 && p->depth != 32) ||
            (uint64_t)p->w*p->h*(p->depth/8) != p->decoded || p->decoded > INT_MAX ||
            p->offset < data || !span(p->offset, p->stored, limit) ||
            (p->codec == 0 ? p->stored != p->decoded :
             p->codec != 2 || p->stored < 6 || (uint64_t)(p->stored-4)*63 < p->decoded)) goto fail;
        if ((!selection || selection[0] == SFF_SELECT_END) && !decode_page(p,s,start,a->source)) goto fail;
    }
    if (h->sprite_count && (!selection || selection[0] == SFF_SELECT_END) &&
        !(set->sprites = KeyedArray_Create(h->sprite_count, sizeof(Sprite)))) goto fail;
    for (unsigned i = 0; i < a->sprite_count; i++) {
        unsigned char n[40];
        if (!read_at(s, start, h->sprite_offset + 40*i, n, sizeof(n))) goto fail;
        int group = (int32_t)dword(n), number = (int32_t)dword(n+4);
        unsigned w = word(n+8), ht = word(n+10), depth = dword(n+16);
        unsigned request = dword(n+20), effective = dword(n+24), link = dword(n+28);
        unsigned page = dword(n+32), left = word(n+36), top = word(n+38);
        if (!w || !ht || w > INT16_MAX || ht > INT16_MAX || page >= pages ||
            (request != UINT32_MAX && request >= h->palette_count) ||
            (effective != UINT32_MAX && effective >= h->palette_count)) goto fail;
        AtlasPage *p = &a->pages[page];
        if (effective != UINT32_MAX && (depth == 0 || depth > 8 ||
            ((Palette *)set->palettes->data)[effective].count != (int)(1u << depth))) goto fail;
        if (!depth || (depth <= 8 ? 8u : depth) != p->depth ||
            !left || !top || left+w >= p->w || top+ht >= p->h) goto fail;
        if (link != UINT32_MAX && link >= i) goto fail;
        roots[i] = link == UINT32_MAX ? i : roots[link];
        AtlasRegion *r = &a->regions[roots[i]];
        if (link == UINT32_MAX) {
            r->page = p; r->left = left; r->top = top; r->w = w; r->h = ht; r->depth = depth;
        } else if (r->page != p || r->left != left || r->top != top ||
                   r->w != w || r->h != ht || r->depth != depth) goto fail;
        if (selected(selection, group, number)) {
            Sprite spr;
            if (!decode_page(p,s,start,a->source) ||
                !Sprite_Init(SPRITE_INIT_NO_TEXTURE, &spr, SPRITE_FORMAT_ATLAS, r, 0, w, ht,
                             depth, (int16_t)word(n+12), (int16_t)word(n+14))) goto fail;
            spr.pal_index = effective == UINT32_MAX ? -1 : (int)effective;
            if (!SpriteSet_AddSprite(set, &spr, group, number)) { Sprite_Free(&spr); goto fail; }
        }
    }
    free(roots);
    a->next = atlases; atlases = a;
    return set;
fail:
    free(roots);
    SpriteSet_Free(set);
    return NULL;
}
