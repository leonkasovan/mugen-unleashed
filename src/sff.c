/*
 * Sprite files: SFF v1.01 (PCX images), SFF v2.x and single PCX images
 * (original module 0x467510-0x469A45).
 *
 * File offsets in SFF headers count from the start of the file; the loaders
 * convert them relative to the stream position after the 0x200-byte header,
 * so a sprite file can sit inside a larger stream. The stream position is
 * restored before returning.
 *
 * Sprites, palettes and their containers are created by the sprite module
 * (Sprite_Init, Pal_Init, SpriteSet_*); compressed SFF v2 formats are
 * decoded there too.
 */
#include "mugen.h"
#include <limits.h>
#include <stdlib.h>

#define SFF_LOG_INFO    7
#define SFF_LOG_ERROR   3

/* Offset relative to the start of the stream for a file offset of an SFF. */
static long Sff_StreamPos(unsigned int file_offset, long start)
{
    return (long)(file_offset + start - SFF_HEADER_SIZE);
}

/* ---- headers ---- */

/* 0x00469600: rest of a v1.01 header, after the version. */
int Sff1_ReadHeader(Sff1Header *hdr, Stream *s)
{
    if (Stream_ReadU32(s, (unsigned int *)&hdr->group_count) == -1 ||
        Stream_ReadU32(s, (unsigned int *)&hdr->image_count) == -1 ||
        Stream_ReadU32(s, &hdr->first_offset) == -1 ||
        Stream_ReadU32(s, &hdr->subheader_size) == -1 ||
        Stream_ReadU8(s, &hdr->palette_type) == -1 ||
        Stream_ReadU8(s, &hdr->reserved[0]) == -1 ||
        Stream_ReadU8(s, &hdr->reserved[1]) == -1 ||
        Stream_ReadU8(s, &hdr->reserved[2]) == -1) {
        return 0;
    }
    return Stream_Read(hdr->comments, sizeof(hdr->comments), 1, s) >= 1;
}

/* 0x004696C0: rest of a v2.x header, after the version. */
int Sff2_ReadHeader(Sff2Header *hdr, Stream *s)
{
    if (Stream_ReadU32(s, &hdr->reserved1) == -1 ||
        Stream_ReadU32(s, &hdr->reserved2) == -1 ||
        Stream_ReadU32(s, &hdr->compat_version) == -1 ||
        Stream_ReadU32(s, &hdr->reserved3) == -1 ||
        Stream_ReadU32(s, &hdr->reserved4) == -1 ||
        Stream_ReadU32(s, &hdr->sprite_offset) == -1 ||
        Stream_ReadU32(s, &hdr->sprite_count) == -1 ||
        Stream_ReadU32(s, &hdr->palette_offset) == -1 ||
        Stream_ReadU32(s, &hdr->palette_count) == -1 ||
        Stream_ReadU32(s, &hdr->ldata_offset) == -1 ||
        Stream_ReadU32(s, &hdr->ldata_size) == -1 ||
        Stream_ReadU32(s, &hdr->tdata_offset) == -1 ||
        Stream_ReadU32(s, &hdr->tdata_size) == -1 ||
        Stream_ReadU32(s, &hdr->reserved5) == -1 ||
        Stream_ReadU32(s, &hdr->reserved6) == -1) {
        return 0;
    }
    return Stream_Read(hdr->unused, sizeof(hdr->unused), 1, s) >= 1;
}

/*
 * 0x00469550: reads and checks the file header. The version is stored in
 * *version (if not NULL) as soon as it is read, even when it is not
 * supported (1.01 to 2.01).
 */
int Sff_ReadHeader(Stream *s, SffHeader *hdr, unsigned int *version)
{
    memset(hdr, 0, sizeof(*hdr));
    if (Stream_Read(hdr->v1.signature, sizeof(hdr->v1.signature), 1, s) < 1) {
        return 0;
    }
    /* The signature may lack its terminator: strcmp then runs into the zeroed version. */
    if (strcmp(hdr->v1.signature, "ElecbyteSpr") != 0) {
        return 0;
    }
    if (Stream_ReadU32(s, &hdr->v1.version) == -1) {
        return 0;
    }
    if (version != NULL) {
        *version = hdr->v1.version;
    }
    if (hdr->v1.version < SFF_VERSION_101 || hdr->v1.version > SFF_VERSION_201) {
        return 0;
    }
    if (hdr->v1.version == SFF_VERSION_101) {
        return Sff1_ReadHeader(&hdr->v1, s);
    }
    return Sff2_ReadHeader(&hdr->v2, s);
}

/* 0x00469810 */
int Sff1_ReadSubHeader(Sff1SubHeader *sub, Stream *s)
{
    if (Stream_ReadU32(s, &sub->next_offset) == -1 ||
        Stream_ReadU32(s, (unsigned int *)&sub->size) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&sub->x) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&sub->y) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&sub->group) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&sub->number) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&sub->link) == -1 ||
        Stream_ReadU8(s, &sub->same_palette) == -1) {
        return 0;
    }
    return Stream_Read(sub->comment, sizeof(sub->comment), 1, s) >= 1;
}

/* 0x004698C0 */
int Sff2_ReadSpriteNode(Sff2SpriteNode *node, Stream *s)
{
    if (Stream_ReadU16(s, &node->group) == -1 ||
        Stream_ReadU16(s, &node->number) == -1 ||
        Stream_ReadU16(s, &node->w) == -1 ||
        Stream_ReadU16(s, &node->h) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&node->x) == -1 ||
        Stream_ReadU16(s, (unsigned short *)&node->y) == -1 ||
        Stream_ReadU16(s, &node->link) == -1 ||
        Stream_ReadU8(s, &node->format) == -1 ||
        Stream_ReadU8(s, &node->depth) == -1 ||
        Stream_ReadU32(s, &node->data_offset) == -1 ||
        Stream_ReadU32(s, &node->data_size) == -1 ||
        Stream_ReadU16(s, &node->palette) == -1) {
        return 0;
    }
    return Stream_ReadU16(s, &node->flags) != -1;
}

/* 0x004699D0 */
int Sff2_ReadPaletteNode(Sff2PaletteNode *node, Stream *s)
{
    if (Stream_ReadU16(s, &node->group) == -1 ||
        Stream_ReadU16(s, &node->number) == -1 ||
        Stream_ReadU16(s, &node->ncolors) == -1 ||
        Stream_ReadU16(s, &node->link) == -1 ||
        Stream_ReadU32(s, &node->data_offset) == -1) {
        return 0;
    }
    return Stream_ReadU32(s, &node->data_size) != -1;
}

/* ---- PCX ---- */

/* Port fix: bounded PCX byte reader; negative reads include both EOF and I/O errors. */
static int Pcx_Byte(Stream *s, size_t *left)
{
    if (*left == 0) return -1;
    --*left;
    return Stream_GetByte(s);
}
static unsigned int Pcx_U16(const unsigned char *p)
{
    return p[0] | (unsigned int)p[1] << 8;
}

/* Port fix: decode within the declared PCX record, never adjacent SFF/FNT data.
 * All failures release pixels, zero outputs and restore the input position.
 * Missing palette markers retain the legacy header-palette fallback; a present
 * marker must have a complete 768-byte palette. RGB output remains B,G,R. */
void *Pcx_DecodeSized(Palette *pal, int *w_out, Stream *s, int *h_out,
                      int *bpp_out, size_t *size_out, int flags, size_t length)
{
    unsigned char hdr[128], colors[1024] = {0};
    unsigned char *pixels = NULL;
    long start = Stream_Tell(s), end = Stream_GetSize(s);
    unsigned int xmin, ymin, xmax, ymax;
    size_t left, size;
    int w, h, planes, bpl, y, i;
    *w_out = *h_out = *bpp_out = 0; *size_out = 0;
    if (start < 0 || end < start || length > (size_t)(end - start) || length < sizeof(hdr)) goto fail;
    if (Stream_Read(hdr, 1, sizeof(hdr), s) != sizeof(hdr) ||
        hdr[0] != 10 || hdr[2] != 1 || hdr[3] != 8) goto fail;
    left = length - sizeof(hdr);
    xmin = Pcx_U16(hdr + 4); ymin = Pcx_U16(hdr + 6);
    xmax = Pcx_U16(hdr + 8); ymax = Pcx_U16(hdr + 10);
    if (xmax < xmin || ymax < ymin) goto fail;
    w = (int)(xmax - xmin + 1); h = (int)(ymax - ymin + 1);
    planes = hdr[65]; bpl = Pcx_U16(hdr + 66);
    if ((planes != 1 && planes != 3) || bpl < w || bpl > 32767 ||
        (size_t)w > (size_t)INT_MAX / planes / h) goto fail;
    size = (size_t)w * h * planes;
    /* Even maximal RLE runs encode at most 63 bytes in two input bytes.
     * Reject impossible tiny records before reserving a huge output buffer. */
    if (((uint64_t)bpl * planes * h + 62) / 63 * 2 > left + 1) goto fail;
    pixels = calloc(1, size);
    if (pixels == NULL) { g_outOfMemory = 1; goto fail; }
    for (y = 0; y < h; ++y) {
        int x = 0, line = bpl * planes;
        while (x < line) {
            int c = Pcx_Byte(s, &left), run = 1;
            if (c < 0) goto fail;
            if ((c & 0xc0) == 0xc0) {
                run = c & 63;
                c = Pcx_Byte(s, &left);
                if (c < 0 || run == 0) goto fail;
            }
            if (run > line - x) goto fail;
            while (run--) {
                int plane = x / bpl, px = x % bpl;
                if (px < w) pixels[((size_t)y * w + px) * planes + planes - 1 - plane] = (unsigned char)c;
                ++x;
            }
        }
    }
    if (planes == 1 && pal != NULL) {
        int c;
        for (i = 0; i < 16; ++i) {
            memcpy(colors + i * 4, hdr + 16 + i * 3, 3); colors[i * 4 + 3] = 255;
        }
        while (left) {
            c = Pcx_Byte(s, &left);
            if (c < 0) goto fail;
            if (c != 12) continue;
            if (left < 768) goto fail;
            for (i = 0; i < 256; ++i) {
                int j;
                for (j = 0; j < 3; ++j) {
                    c = Pcx_Byte(s, &left);
                    if (c < 0) goto fail;
                    colors[i * 4 + j] = (unsigned char)c;
                }
                colors[i * 4 + 3] = 255;
            }
            break;
        }
        colors[3] = 0;
        if (!Pal_Init(pal, 256, colors, PAL_COPY | (flags ? PAL_NO_TEXTURE : 0))) goto fail;
    }
    *w_out = w; *h_out = h; *bpp_out = planes * 8; *size_out = size;
    Stream_Seek(s, start, SEEK_SET);
    return pixels;
fail:
    free(pixels);
    if (start >= 0) Stream_Seek(s, start, SEEK_SET);
    return NULL;
}

/* 0x00468C60: standalone PCX; embedded callers supply their record size. */
void *Pcx_Decode(Palette *pal, int *w, Stream *s, int *h, int *bpp, size_t *size, int flags)
{
    long start = Stream_Tell(s), end = Stream_GetSize(s);
    return Pcx_DecodeSized(pal, w, s, h, bpp, size, flags,
                          start >= 0 && end >= start ? (size_t)(end - start) : 0);
}

/*
 * Port addition: bounded variant of the 0x00469410 PCX sprite-set loader, with
 * palette (0, 0) for 8-bit images. Flags keeps the image and the palette out
 * of textures.
 */
SpriteSet *Pcx_LoadSized(Stream *s, int flags, size_t length)
{
    Palette pal;
    Sprite spr;
    int w, h, bpp;
    size_t size;
    void *pixels;
    SpriteSet *set = calloc(1, sizeof(SpriteSet));

    if (set == NULL) {
        g_outOfMemory = 1;
        return NULL;
    }
    /* The set leaks when these fail. */
    set->sprites = KeyedArray_Create(1, sizeof(Sprite));
    if (set->sprites == NULL) {
        return NULL;
    }
    set->palettes = KeyedArray_Create(1, sizeof(Palette));
    if (set->palettes == NULL) {
        return NULL;
    }
    pixels = Pcx_DecodeSized(&pal, &w, s, &h, &bpp, &size, flags, length);
    if (pixels == NULL) {
        SpriteSet_Free(set);
        return NULL;
    }
    if (bpp <= 8) {
        SpriteSet_AddPalette(set, &pal, 0, 0);
    }
    if (!Sprite_Init(flags ? SPRITE_INIT_OWN | SPRITE_INIT_DECODE | SPRITE_INIT_NO_TEXTURE
                           : SPRITE_INIT_OWN | SPRITE_INIT_DECODE,
                     &spr, SPRITE_FORMAT_RAW, pixels, size, w, h, bpp, 0, 0)) {
        SpriteSet_Free(set);
        return NULL;
    }
    if (bpp <= 8) {
        SpriteSet_LinkPalette(set, &spr, 0, 0);
    }
    SpriteSet_AddSprite(set, &spr, 0, 0);
    return set;
}

/* 0x00469410: standalone PCX loader. */
SpriteSet *Pcx_Load(Stream *s, int flags)
{
    long start = Stream_Tell(s), end = Stream_GetSize(s);
    return Pcx_LoadSized(s, flags, start >= 0 && end >= start ? (size_t)(end - start) : 0);
}

/* ---- SFF v1.01 ---- */

/*
 * Port fix: legacy SFF v1 writers can count an omitted shared palette in
 * sub.size. Bound each PCX by the next subheader (or EOF for the last one),
 * as well as its declared size, so it cannot consume the following record.
 * start is the stream position after the SFF header, including any prefix.
 */
static size_t Sff1_PcxLength(const Sff1SubHeader *sub, Stream *s, long start)
{
    long pos = Stream_Tell(s), end = Stream_GetSize(s);
    uint64_t limit;
    size_t available;

    if (start < SFF_HEADER_SIZE || pos < start || end < pos || sub->size <= 0) {
        return 0;
    }
    limit = sub->next_offset != 0
                ? (uint64_t)(start - SFF_HEADER_SIZE) + sub->next_offset
                : (uint64_t)end;
    if (limit < (uint64_t)pos || limit > (uint64_t)end) {
        return 0;
    }
    available = (size_t)(limit - (uint64_t)pos);
    return (size_t)sub->size < available ? (size_t)sub->size : available;
}

/*
 * 0x004677D0: every sprite of a v1.01 file. Palettes are numbered (0, n) in
 * load order. Linked sprites copy the linked sprite by slot index without a
 * range check.
 */
SpriteSet *Sff1_Load(const Sff1Header *hdr, Stream *s)
{
    Sff1SubHeader sub;
    Palette pal;
    Sprite spr;
    int w, h, bpp;
    size_t size;
    int pal_count = 0;
    int last_pal = -1;
    long start = Stream_Tell(s);
    int image_count = hdr->image_count;
    unsigned int offset;
    int i;
    SpriteSet *set = calloc(1, sizeof(SpriteSet));

    if (set == NULL) {
        g_outOfMemory = 1;
        return NULL;
    }
    /* The set leaks when these fail. */
    if (image_count > 0) {
        set->sprites = KeyedArray_Create(image_count, sizeof(Sprite));
        if (set->sprites == NULL) {
            return NULL;
        }
    }
    set->palettes = KeyedArray_Create(1, sizeof(Palette));
    if (set->palettes == NULL) {
        return NULL;
    }
    offset = hdr->first_offset;
    for (i = 0; i < hdr->image_count; i++) {
        Stream_Seek(s, Sff_StreamPos(offset, start), SEEK_SET);
        if (!Sff1_ReadSubHeader(&sub, s)) {
            goto fail;
        }
        if (sub.size > 0) {
            Palette *new_pal = NULL;
            void *pixels;
            if (last_pal == -1 || !sub.same_palette) {
                new_pal = &pal;
            }
            pixels = Pcx_DecodeSized(new_pal, &w, s, &h, &bpp, &size, 0, Sff1_PcxLength(&sub, s, start));
            if (pixels == NULL) {
                goto fail;
            }
            if (bpp > 8) {
                new_pal = NULL;
            } else if (new_pal != NULL && !SpriteSet_AddPalette(set, new_pal, 0, pal_count)) {
                goto fail;
            }
            if (!Sprite_Init(SPRITE_INIT_OWN | SPRITE_INIT_DECODE, &spr, SPRITE_FORMAT_RAW, pixels, size,
                             w, h, bpp, sub.x, sub.y)) {
                goto fail;
            }
            if (new_pal != NULL) {
                SpriteSet_LinkPalette(set, &spr, 0, pal_count);
                last_pal = pal_count++;
            } else if (sub.same_palette) {
                SpriteSet_LinkPalette(set, &spr, 0, last_pal);
            }
        } else {
            KeyedArray *sprites = set->sprites;
            spr = *(const Sprite *)(sprites->data + sub.link * sprites->elem_size);
            spr.owns_rows = 0;
            spr.owns_pixels = 0;
            spr.x = sub.x;
            spr.y = sub.y;
        }
        SpriteSet_AddSprite(set, &spr, sub.group, sub.number);
        offset = sub.next_offset;
    }
    Stream_Seek(s, start, SEEK_SET);
    return set;

fail:
    SpriteSet_Free(set);
    return NULL;
}

/*
 * 0x00467C70: only the sprites in select (and the palettes they need) of a
 * v1.01 file. Stops once as many sprites as select pairs were loaded; linked
 * sprites are skipped.
 */
SpriteSet *Sff1_LoadSelected(Stream *s, const Sff1Header *hdr, const int *select)
{
    Sff1SubHeader sub;
    Palette pal;
    Sprite spr;
    int w, h, bpp;
    size_t size;
    int pal_count = 0;
    int last_pal = -1;
    int loaded = 0;
    int wanted_count = 0;
    const int *p;
    long start;
    unsigned int offset;
    int i;
    SpriteSet *set;

    for (p = select; p[0] != SFF_SELECT_END; p += 2) {
        wanted_count++;
    }
    start = Stream_Tell(s);
    set = calloc(1, sizeof(SpriteSet));
    if (set == NULL) {
        g_outOfMemory = 1;
        return NULL;
    }
    /* The set leaks when these fail. */
    if (wanted_count > 0) {
        set->sprites = KeyedArray_Create(wanted_count, sizeof(Sprite));
        if (set->sprites == NULL) {
            return NULL;
        }
    }
    set->palettes = KeyedArray_Create(1, sizeof(Palette));
    if (set->palettes == NULL) {
        return NULL;
    }
    offset = hdr->first_offset;
    if (hdr->image_count > 0) {
        i = 0;
        for (;;) {
            int wanted = 0;

            Stream_Seek(s, Sff_StreamPos(offset, start), SEEK_SET);
            if (!Sff1_ReadSubHeader(&sub, s)) {
                goto fail;
            }
            /* Selected and not loaded yet. */
            for (p = select; p[0] != SFF_SELECT_END; p += 2) {
                if (sub.group == p[0] && sub.number == p[1]) {
                    KeyedArray *sprites = set->sprites;
                    int index = KeyedArray_Find(sprites, p[0], p[1]);
                    if (index == -1 || sprites->data == NULL) {
                        wanted = 1;
                        break;
                    }
                }
            }
            if (sub.size > 0) {
                Palette *new_pal = NULL;
                if (last_pal == -1 || !sub.same_palette) {
                    new_pal = &pal;
                }
                if (wanted) {
                    void *pixels = Pcx_DecodeSized(new_pal, &w, s, &h, &bpp, &size, 0, Sff1_PcxLength(&sub, s, start));
                    if (pixels == NULL) {
                        goto fail;
                    }
                    if (bpp > 8) {
                        new_pal = NULL;
                    } else if (new_pal != NULL && !SpriteSet_AddPalette(set, new_pal, 0, pal_count)) {
                        goto fail;
                    }
                    if (!Sprite_Init(SPRITE_INIT_OWN | SPRITE_INIT_DECODE, &spr, SPRITE_FORMAT_RAW, pixels, size,
                                     w, h, bpp, sub.x, sub.y)) {
                        goto fail;
                    }
                    if (new_pal != NULL) {
                        SpriteSet_LinkPalette(set, &spr, 0, pal_count);
                        last_pal = pal_count++;
                    } else if (sub.same_palette) {
                        SpriteSet_LinkPalette(set, &spr, 0, last_pal);
                    }
                    SpriteSet_AddSprite(set, &spr, sub.group, sub.number);
                    loaded++;
                }
            }
            if (loaded >= wanted_count) {
                break;
            }
            offset = sub.next_offset;
            if (++i >= hdr->image_count) {
                break;
            }
        }
    }
    Stream_Seek(s, start, SEEK_SET);
    return set;

fail:
    SpriteSet_Free(set);
    return NULL;
}

/* ---- SFF v2.x ---- */

/*
 * Port addition (docs/SFF_MEMORY_OPTIMIZATION_PLAN.md, phase P3): whether SFF v2
 * images are decoded (and, with a GL renderer, uploaded as textures) at load
 * time, as the original did, instead of staying encoded until first use.
 *
 * MEMORY: the original decoded every ldata image when g_gfxFlags had bit 1 set
 * (desktop GL does), which allocates a full decoded buffer and a GL texture per
 * sprite of the file -- 25 to 90 MB of textures for one Hyper DBZ character
 * whose encoded SFF is 2 to 23 MB -- whether or not the animation ever shows
 * it. Lazy keeps the encoded span (already resident in SpriteSet.data) and
 * resolves an image on demand into the bounded decompression pool, so the
 * residency follows what the fight actually draws.
 *
 * MUGEN_SFF_EAGER=1 restores the original policy, which is the rollback switch
 * the plan requires until every platform has native evidence.
 *
 * The trigger is still g_gfxFlags bit 1, so builds that never set it (a plain
 * software renderer, and GLES, whose Gl_Init does not set it) keep their old
 * lazy behaviour, while OpenGLScreen and OpenGL change over. In OpenGLScreen the
 * software rasterizer draws from Sprite_GetPixels, which is the bounded pool
 * this policy feeds.
 */
static int Sff_DecodeOnLoad(const Sff2SpriteNode *node)
{
    static int eager = -1;

    if (eager < 0) {
        const char *env = getenv("MUGEN_SFF_EAGER");

        eager = (env != NULL && env[0] == '1' && env[1] == '\0') ? 1 : 0;
        MatchLog_Printf(SFF_LOG_INFO, "SFF v2 decode policy: %s\n",
                        eager ? "eager (original)" : "lazy (encoded source, textures on demand)");
    }
    /* Port fix: retain eager decoding for images that cannot fit the pool,
     * including alignment, the block header and the end marker. This avoids
     * silently dropping valid large images under the default lazy policy. */
    if (!eager) {
        size_t stride = ((size_t)node->depth + 7) / 8;
        size_t bytes, capacity;
        if (!g_spriteDecompBuffer || !node->w || !node->h || !stride ||
            (size_t)node->w > (size_t)-1 / node->h / stride) return 1;
        bytes = (size_t)node->w * node->h * stride;
        capacity = g_spriteDecompBuffer->size;
        if (capacity < 2 * sizeof(DecompBlock) + sizeof(void *) - 1 ||
            bytes > capacity - 2 * sizeof(DecompBlock) - (sizeof(void *) - 1)) return 1;
    }
    return eager;
}

/*
 * Links spr to the palette of node index pal_index: the palette that went
 * into slot pal_index of the palette table (palettes fill it in file order).
 */
static void Sff2_LinkPalette(SpriteSet *set, Sprite *spr, const Sff2SpriteNode *node, const Sff2Header *hdr)
{
    if (node->depth <= 8 && node->palette < hdr->palette_count) {
        const KeyedArraySlot *slot = &set->palettes->slots[node->palette];
        SpriteSet_LinkPalette(set, spr, slot->key1, slot->key2);
    }
}

/*
 * 0x004680F0: sprites of a v2.x file: all of them when select is empty, else
 * the selected ones (palettes are always loaded). With everything selected
 * the ldata block is read at once into SpriteSet.data and sprites point
 * into it; otherwise only the data of selected sprites is read into it.
 * Sprites in the tdata block get their own decoded copy.
 *
 * Quirks kept: the tdata offset is used as a stream position without the
 * header adjustment, the tdata buffer leaks when Sprite_Init fails, and the
 * alpha of v2.00 palettes is patched in the file data (color 0 transparent).
 */
SpriteSet *Sff2_Load(Stream *s, const Sff2Header *hdr, const int *select)
{
    Sff2SpriteNode *sprite_nodes = NULL;
    Sff2PaletteNode *pal_nodes = NULL;
    unsigned int data_pos = 0;
    int load_all = select[0] == SFF_SELECT_END;
    int v200 = hdr->version == SFF_VERSION_200;
    long start = Stream_Tell(s);
    unsigned char *data;
    size_t data_size;
    unsigned int i;
    long pos;
    SpriteSet *set;
    Palette pal;
    Sprite spr;

    set = SpriteSet_Create(hdr->sprite_count, hdr->palette_count);
    if (set == NULL) {
        return NULL;
    }
    if (hdr->palette_count != 0) {
        pal_nodes = calloc(hdr->palette_count, sizeof(Sff2PaletteNode));
        if (pal_nodes == NULL) {
            g_outOfMemory = 1;
            SpriteSet_Free(set);
            return NULL;
        }
        pos = Sff_StreamPos(hdr->palette_offset, start);
        if (pos != Stream_Tell(s)) {
            Stream_Seek(s, pos, SEEK_SET);
        }
        for (i = 0; i < hdr->palette_count; i++) {
            if (!Sff2_ReadPaletteNode(&pal_nodes[i], s)) {
                free(pal_nodes);
                SpriteSet_Free(set);
                return NULL;
            }
        }
    }
    if (hdr->sprite_count != 0) {
        sprite_nodes = calloc(hdr->sprite_count, sizeof(Sff2SpriteNode));
        if (sprite_nodes == NULL) {
            g_outOfMemory = 1;
            if (pal_nodes != NULL) {
                free(pal_nodes);
            }
            SpriteSet_Free(set);
            return NULL;
        }
        pos = Sff_StreamPos(hdr->sprite_offset, start);
        if (pos != Stream_Tell(s)) {
            Stream_Seek(s, pos, SEEK_SET);
        }
        for (i = 0; i < hdr->sprite_count; i++) {
            if (!Sff2_ReadSpriteNode(&sprite_nodes[i], s)) {
                free(sprite_nodes);
                if (pal_nodes != NULL) {
                    free(pal_nodes);
                }
                SpriteSet_Free(set);
                return NULL;
            }
        }
    }

    /* Size of the data block: the whole ldata, or the palettes and selected ldata sprites. */
    if (load_all) {
        data_size = hdr->ldata_size;
    } else {
        data_size = 0;
        for (i = 0; i < hdr->palette_count; i++) {
            data_size += pal_nodes[i].data_size;
        }
        for (i = 0; i < hdr->sprite_count; i++) {
            const Sff2SpriteNode *node = &sprite_nodes[i];
            const int *p;
            for (p = select; p[0] != SFF_SELECT_END; p += 2) {
                if (node->group == p[0] && node->number == p[1]) {
                    if (!(node->flags & SFF2_SPRITE_TDATA)) {
                        data_size += node->data_size;
                    }
                    break;
                }
            }
        }
    }
    data = malloc(data_size);
    set->data = data;
    /* Port addition (SFF memory plan, P1): let SpriteSet_MemUse report it. */
    SpriteSet_SetDataSourceSize(set, data_size);
    if (data == NULL) {
        goto fail_nodes;
    }
    if (load_all) {
        pos = Sff_StreamPos(hdr->ldata_offset, start);
        if (pos != Stream_Tell(s)) {
            Stream_Seek(s, pos, SEEK_SET);
        }
        if (Stream_Read(data, hdr->ldata_size, 1, s) < 1) {
            goto fail_nodes;
        }
    }

    for (i = 0; i < hdr->palette_count; i++) {
        const Sff2PaletteNode *node = &pal_nodes[i];
        if (node->data_size != 0) {
            unsigned int at;
            if (!load_all) {
                Stream_Seek(s, Sff_StreamPos(hdr->ldata_offset + node->data_offset, start), SEEK_SET);
                if (Stream_Read(data + data_pos, node->data_size, 1, s) < 1) {
                    goto fail_nodes;
                }
            } else {
                data_pos = node->data_offset;
            }
            at = data_pos;
            if (v200) {
                int n = node->ncolors;
                int k;
                if (n > 0) {
                    data[at + 3] = 0;
                }
                for (k = 1; k < n; k++) {
                    data[at + k * 4 + 3] = 0xFF;
                }
            }
            if (!Pal_Init(&pal, node->ncolors, data + at, 0)) {
                goto fail_nodes;
            }
            if (!load_all) {
                data_pos = at + node->data_size;
            }
        } else {
            KeyedArray *palettes = set->palettes;
            const Palette *src = (const Palette *)(palettes->data + node->link * palettes->elem_size);
            if (src == NULL) {
                goto fail_nodes;
            }
            pal = *src;
            pal.owns = 0;
        }
        if (!SpriteSet_AddPalette(set, &pal, node->group, node->number)) {
            goto fail_nodes;
        }
    }
    if (pal_nodes != NULL) {
        free(pal_nodes);
    }

    for (i = 0; i < hdr->sprite_count; i++) {
        const Sff2SpriteNode *node = &sprite_nodes[i];
        if (!load_all) {
            const int *p;
            for (p = select;; p += 2) {
                if (p[0] == SFF_SELECT_END) {
                    goto next_sprite;
                }
                if (node->group == p[0] && node->number == p[1]) {
                    break;
                }
            }
        }
        if (node->data_size != 0) {
            if (node->flags & SFF2_SPRITE_TDATA) {
                void *buf = malloc(node->data_size);
                if (buf == NULL) {
                    goto fail_sprites;
                }
                Stream_Seek(s, (long)(node->data_offset + hdr->tdata_offset), SEEK_SET);
                if (Stream_Read(buf, node->data_size, 1, s) < 1) {
                    free(buf);
                    goto fail_sprites;
                }
                /* Port addition (SFF memory plan P3): on a GL build the tdata
                 * image stays encoded and is decoded on demand, so loading a
                 * character does not allocate a texture per unused sprite. */
                if (!Sprite_Init(SPRITE_INIT_COPY |
                                 ((Sff_DecodeOnLoad(node) || (g_gfxFlags & 1) == 0) ? SPRITE_INIT_DECODE : 0),
                                 &spr, node->format, buf, node->data_size,
                                 (short)node->w, (short)node->h, node->depth, node->x, node->y)) {
                    free(NULL);
                    SpriteSet_Free(set);
                    free(sprite_nodes);
                    return NULL;
                }
                free(buf);
            } else {
                unsigned int flags = (Sff_DecodeOnLoad(node) && (g_gfxFlags & 1)) ? SPRITE_INIT_DECODE : 0;
                unsigned int at;
                if (!load_all) {
                    Stream_Seek(s, Sff_StreamPos(node->data_offset + hdr->ldata_offset, start), SEEK_SET);
                    if (Stream_Read(data + data_pos, node->data_size, 1, s) < 1) {
                        goto fail_sprites;
                    }
                    at = data_pos;
                } else {
                    at = node->data_offset;
                    data_pos = at;
                }
                if (!Sprite_Init(flags, &spr, node->format, data + at, node->data_size,
                                 (short)node->w, (short)node->h, node->depth, node->x, node->y)) {
                    goto fail_sprites;
                }
                if (!load_all) {
                    data_pos = at + node->data_size;
                }
            }
            Sff2_LinkPalette(set, &spr, node, hdr);
        } else {
            KeyedArray *sprites = set->sprites;
            const Sprite *src = (const Sprite *)(sprites->data + node->link * sprites->elem_size);
            if (src == NULL) {
                goto fail_sprites;
            }
            spr = *src;
            spr.owns_rows = 0;
            spr.owns_pixels = 0;
            spr.x = node->x;
            spr.y = node->y;
            Sff2_LinkPalette(set, &spr, node, hdr);
        }
        if (!SpriteSet_AddSprite(set, &spr, node->group, node->number)) {
            goto fail_sprites;
        }
    next_sprite:;
    }
    if (sprite_nodes != NULL) {
        free(sprite_nodes);
    }
    Stream_Seek(s, start, SEEK_SET);
    return set;

fail_nodes:
    if (sprite_nodes != NULL) {
        free(sprite_nodes);
    }
    if (pal_nodes != NULL) {
        free(pal_nodes);
    }
    SpriteSet_Free(set);
    return NULL;

fail_sprites:
    free(sprite_nodes);
    SpriteSet_Free(set);
    return NULL;
}

/* ---- entry points ---- */

/*
 * 0x00467680: every sprite of the SFF file at the stream position (the
 * position is restored).
 */
SpriteSet *Sff_LoadStream(Stream *s)
{
    SffHeader hdr;
    unsigned int version = 0;
    SpriteSet *set = NULL;
    long start = Stream_Tell(s);

    if (Sff_ReadHeader(s, &hdr, &version)) {
        MatchLog_Printf(SFF_LOG_INFO, "SFF version %08X (compatver %08X)\n", version, hdr.v2.compat_version);
        if (version == SFF_VERSION_101) {
            set = Sff1_Load(&hdr.v1, s);
        } else if (hdr.v2.compat_version > SFF_VERSION_201) {
            MatchLog_Printf(SFF_LOG_ERROR, "SFF version not supported: %08X (compatver %08X)\n", version,
                            hdr.v2.compat_version);
        } else {
            int select_all = SFF_SELECT_END;
            set = Sff2_Load(s, &hdr.v2, &select_all);
        }
        if (set != NULL) {
            set->version = hdr.v1.version;
        }
    }
    Stream_Seek(s, start, SEEK_SET);
    return set;
}

/*
 * 0x00467B30: the sprites in select ((group, number) pairs ended by -1) of
 * the SFF file at the stream position (the position is restored).
 */
SpriteSet *Sff_LoadSelected(Stream *s, const int *select)
{
    SffHeader hdr;
    unsigned int version = 0;
    SpriteSet *set = NULL;
    long start = Stream_Tell(s);

    if (Sff_ReadHeader(s, &hdr, &version)) {
        if (version == SFF_VERSION_101) {
            set = Sff1_LoadSelected(s, &hdr.v1, select);
        } else if (hdr.v2.compat_version > SFF_VERSION_201) {
            MatchLog_Printf(SFF_LOG_ERROR, "SFF version not supported: %08X (compatver %08X)\n", version,
                            hdr.v2.compat_version);
        } else {
            set = Sff2_Load(s, &hdr.v2, select);
        }
        if (set != NULL) {
            set->version = hdr.v1.version;
        }
    }
    Stream_Seek(s, start, SEEK_SET);
    return set;
}

/*
 * 0x00467510: loads a sprite file (.sff, or a .pcx image) and logs its
 * estimated memory use. With BufferedRead the file is read into memory first.
 */
SpriteSet *Sff_Load(const char *path)
{
    unsigned int sys_bytes = 0;
    unsigned int tex_bytes = 0;
    SpriteSet *set;
    Stream *s;

    if (!g_bufferedRead) {
        s = Stream_OpenFile(path, "rb");
        if (s == NULL) {
            return NULL;
        }
    } else {
        FILE *fp = fopen(path, "rb");
        long pos, size;
        void *buf;

        if (fp == NULL) {
            return NULL;
        }
        pos = ftell(fp);
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, pos, SEEK_SET);
        buf = malloc(size);
        if (buf == NULL) {
            fclose(fp);
            return NULL;
        }
        if (fread(buf, size, 1, fp) < 1) {
            fclose(fp);
            free(buf);
            return NULL;
        }
        fclose(fp);
        s = Stream_OpenMemory(buf, size);
        if (s == NULL) {
            free(buf);
            return NULL;
        }
        s->owns_buf = 1;
    }
    Stream_Seek(s, 0, SEEK_SET);
    if (File_GetType(path) == FILETYPE_PCX) {
        set = Pcx_Load(s, 0);
    } else {
        set = Sff_LoadStream(s);
    }
    Stream_Close(s);
    if (set == NULL) {
        return NULL;
    }
    SpriteSet_MemUse(set, &sys_bytes, &tex_bytes);
    MatchLog_Printf(SFF_LOG_INFO, "SFF %s estimated mem use: %uk sys %uk tex\n", path, sys_bytes >> 10,
                    tex_bytes >> 10);
    return set;
}
