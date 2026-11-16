/*
 * Small helpers between the player input buffer and the Lua utilities
 * (original block 0x477EC0-0x478040, no strings): the decoded sprite cache
 * glue (compressed SFF v2 sprites are decoded into g_spriteDecompBuffer and
 * kept there until the space is needed), growable arrays and an empty
 * callback.
 */
#include "mugen.h"
#include <limits.h>

/* DecompBlock and DecompRequest are defined in spritecodec.h. */

/*
 * 0x00477EC0: drops the decoded copy of the compressed sprite data `data`
 * from the sprite cache (blocks are chained by their size field).
 */
void SpriteCache_Remove(void *data)
{
    DecompBuffer *buf = g_spriteDecompBuffer;
    DecompBlock *block, *prev = NULL;

    if (buf == NULL) {
        return;
    }
    block = (DecompBlock *)buf->data;
    while (block->size != 0) {
        if (block->key == data) {
            DecompBuffer_FreeBlock(block, buf, prev);
            return;
        }
        prev = block;
        block = (DecompBlock *)((char *)block + block->size);
    }
}

/* Little-endian decoded-size prefix of compressed sprite data. */
static unsigned int SpriteCache_DeclaredSize(const void *data)
{
    const unsigned char *p = data;

    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* Whether the decompression pool already holds the decoded block of `data`. */
static int SpriteCache_HasBlock(const void *data)
{
    DecompBuffer *buf = g_spriteDecompBuffer;
    DecompBlock *block;

    if (buf == NULL) {
        return 0;
    }
    block = (DecompBlock *)buf->data;
    while (block->size != 0) {
        if (block->key == data) {
            return 1;
        }
        block = (DecompBlock *)((char *)block + block->size);
    }
    return 0;
}

/*
 * Port addition (SFF memory plan, P0/C2): the lazy decoder path with the
 * checks the eager one in Sprite_Init already has.
 *
 * `expected` is the decoded extent the caller computed from the sprite's own
 * width, height and bpp (0: trust the input's prefix). The whole encoded span,
 * the codec and the declared output length are checked before any cache space
 * is taken, so a malformed image never publishes a half-decoded block that the
 * renderer would then upload. A failed PNG decode frees its block again for the
 * same reason (Png_Decode reports the image read result).
 */
int SpriteCache_Acquire(void *data, int format, size_t size, size_t expected, void **out)
{
    DecompRequest key;
    unsigned int declared;
    int hit;
    void *pixels;

    if (g_spriteDecompBuffer == NULL || data == NULL || out == NULL || size < SPRITE_CODEC_PREFIX) {
        return 0;
    }
    declared = SpriteCache_DeclaredSize(data);
    if (expected == 0) {
        expected = declared;
    }
    /* Walk the encoded span only when it is not already decoded: a pool hit is
     * an image that validated and decoded once, and re-walking every span on
     * every draw would put the check in the frame budget. */
    if (!SpriteCache_HasBlock(data) && !Sprite_ValidateCompressed(data, size, format, expected)) {
        return 0;
    }
    key.pool = g_spriteDecompBuffer;
    key.size = declared;
    key.key = data;
    key.cost = (int)(size > (size_t)INT_MAX ? INT_MAX : size);
    /* The pointer comes back at full width; "hit" says whether it is already
     * decoded. (It used to be packed into the pointer's upper 32 bits -- see
     * DecompBuffer_Get.) */
    pixels = DecompBuffer_Get(&key, &hit);
    if (pixels == NULL) {
        return 0;
    }
    if (hit != 0) {
        *out = pixels;
        return 1;
    }
    if ((unsigned int)(format - SPRITE_FORMAT_DECODE_MIN) >
        (unsigned int)(SPRITE_FORMAT_DECODE_MAX - SPRITE_FORMAT_DECODE_MIN) ||
        g_spriteDecoders[format] == NULL) {
        SpriteCache_Remove(data);
        return 0;
    }
    if (format >= 10 && format <= 12) {
        if (g_spriteDecoders[format](data, size, pixels) == 0) {
            SpriteCache_Remove(data);
            return 0;
        }
    } else {
        /* The legacy codecs' return value is not a status (Rle8_Decode returns
         * 0, the LZ5/RLE5 ones return the bytes written); the validator above
         * walked the whole span, which is what establishes that they succeed. */
        g_spriteDecoders[format](data, size, pixels);
    }
    *out = pixels;
    return 1;
}

/*
 * 0x00477EF0: the decoded pixels of the compressed sprite data `data`
 * (SFF v2 compression `format`, size bytes) in *out: the cached copy, or a
 * new block decoded with g_spriteDecoders. Returns 0 without a cache, when
 * no block can be had or for an unknown format.
 *
 * Fix (port change): the checks of SpriteCache_Acquire now apply here too, so
 * corrupt input is rejected instead of being decoded into the pool.
 */
int SpriteCache_Get(void *data, int format, int size, void **out)
{
    if (data == NULL || size < SPRITE_CODEC_PREFIX) {
        return 0;
    }
    return SpriteCache_Acquire(data, format, (size_t)size, 0, out);
}

/*
 * 0x00477F80: an empty array of capacity elements (8 when capacity < 1).
 * Quirk: the parts already allocated leak when an allocation fails.
 */
DynArray *DynArray_Create(int capacity, int elem_size)
{
    DynArray *a = calloc(1, sizeof(DynArray));
    void *data;

    if (a == NULL) {
        g_outOfMemory = 1;
    }
    if (capacity < 1) {
        capacity = 8;
    }
    data = calloc(capacity, elem_size);
    if (data == NULL) {
        g_outOfMemory = 1;
    }
    if (a == NULL || data == NULL) {
        return NULL;
    }
    a->capacity = capacity;
    a->data = data;
    a->elem_size = elem_size;
    return a;
}

/* 0x00477FF0: appends a copy of elem (doubling the capacity when full); elem, or NULL. */
void *DynArray_Push(DynArray *a, const void *elem)
{
    if (a->count >= a->capacity) {
        void *data = realloc(a->data, a->elem_size * a->capacity * 2);
        if (data == NULL) {
            return NULL;
        }
        a->data = data;
        a->capacity *= 2;
    }
    memcpy((char *)a->data + a->count * a->elem_size, elem, a->elem_size);
    a->count++;
    return (void *)elem;
}

/* 0x00478040: does nothing (a default callback). */
void Stub_Nop(void)
{
}
