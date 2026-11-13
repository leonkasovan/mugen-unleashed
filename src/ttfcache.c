/*
 * Port addition: the rasterized-line cache for TrueType text (ttfcache.h).
 * Not part of the original binary; there is no address to name.
 *
 * Deliberately dumb: a fixed array, linear scan on lookup, least-recently-used
 * replacement. Capacity is a handful of entries because that is all a screen
 * needs (a HUD draws a few dozen distinct lines at most, and the hot ones --
 * round text, names, numbers -- repeat every frame), and a linear scan over
 * eight pointers costs nothing next to an SDL_ttf rasterization.
 */
#include <stdlib.h>
#include <string.h>

#include "ttfcache.h"

/* A bound on the stored key, so one pathological line cannot pin a megabyte. */
#define TTFCACHE_MAX_KEY 4096

TtfCache *TtfCache_Create(int capacity)
{
    TtfCache *cache;

    if (capacity <= 0) {
        return NULL;
    }
    cache = calloc(1, sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }
    cache->entries = calloc((size_t)capacity, sizeof(*cache->entries));
    if (cache->entries == NULL) {
        free(cache);
        return NULL;
    }
    cache->capacity = capacity;
    return cache;
}

void TtfCache_Clear(TtfCache *cache)
{
    int i;

    if (cache == NULL) {
        return;
    }
    for (i = 0; i < cache->capacity; i++) {
        free(cache->entries[i].text);
        free(cache->entries[i].pixels);
        memset(&cache->entries[i], 0, sizeof(cache->entries[i]));
    }
    cache->count = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
}

void TtfCache_Free(TtfCache *cache)
{
    if (cache == NULL) {
        return;
    }
    TtfCache_Clear(cache);
    free(cache->entries);
    free(cache);
}

/* The entry holding this key, or NULL. */
static TtfCacheEntry *TtfCache_Find(TtfCache *cache, unsigned long font_id, int r, int g, int b,
                                   int blended, const char *text)
{
    int i;

    for (i = 0; i < cache->capacity; i++) {
        TtfCacheEntry *e = &cache->entries[i];

        if (!e->used || e->font_id != font_id || e->r != r || e->g != g || e->b != b ||
            e->blended != blended) {
            continue;
        }
        if (strcmp(e->text, text) == 0) {
            return e;
        }
    }
    return NULL;
}

/* A free slot, or the least recently used one (whose contents are released). */
static TtfCacheEntry *TtfCache_Slot(TtfCache *cache)
{
    TtfCacheEntry *lru = NULL;
    int i;

    for (i = 0; i < cache->capacity; i++) {
        TtfCacheEntry *e = &cache->entries[i];

        if (!e->used) {
            return e;
        }
        if (lru == NULL || e->tick < lru->tick) {
            lru = e;
        }
    }
    if (lru != NULL) {
        free(lru->text);
        free(lru->pixels);
        memset(lru, 0, sizeof(*lru));
        cache->count--;
        cache->evictions++;
    }
    return lru;
}

const void *TtfCache_Get(TtfCache *cache, unsigned long font_id, int r, int g, int b, int blended,
                         const char *text, int *w, int *h, int *hit,
                         TtfCache_RenderFn render, void *ctx)
{
    TtfCacheEntry *e;
    void *pixels;
    int width = 0;
    int height = 0;
    size_t key_len;

    if (w != NULL) {
        *w = 0;
    }
    if (h != NULL) {
        *h = 0;
    }
    if (hit != NULL) {
        *hit = 0;
    }
    if (cache == NULL || text == NULL || render == NULL) {
        return NULL;
    }

    e = TtfCache_Find(cache, font_id, r, g, b, blended, text);
    if (e != NULL) {
        e->tick = ++cache->clock;
        cache->hits++;
        if (w != NULL) {
            *w = e->w;
        }
        if (h != NULL) {
            *h = e->h;
        }
        if (hit != NULL) {
            *hit = 1;
        }
        return e->pixels;
    }

    key_len = strlen(text);
    pixels = render(text, r, g, b, blended, &width, &height, ctx);
    if (pixels == NULL || width <= 0 || height <= 0) {
        free(pixels);
        return NULL;
    }
    if (key_len >= TTFCACHE_MAX_KEY) {
        /*
         * Longer than any text this engine produces (Font_Printf caps its
         * buffer at 0x1000). Refuse rather than cache a truncated key that
         * could collide with a different line: the caller draws nothing, which
         * is what it did before there was a cache for such input.
         */
        free(pixels);
        return NULL;
    }

    cache->misses++;
    e = TtfCache_Slot(cache);
    if (e == NULL) {
        free(pixels);
        return NULL;
    }
    e->text = malloc(key_len + 1);
    if (e->text == NULL) {
        free(pixels);
        return NULL;
    }
    memcpy(e->text, text, key_len + 1);
    e->used = 1;
    e->tick = ++cache->clock;
    e->font_id = font_id;
    e->r = r;
    e->g = g;
    e->b = b;
    e->blended = blended;
    e->pixels = pixels;
    e->w = width;
    e->h = height;
    cache->count++;

    if (w != NULL) {
        *w = width;
    }
    if (h != NULL) {
        *h = height;
    }
    return e->pixels;
}
