#ifndef MUGEN_TTFCACHE_H
#define MUGEN_TTFCACHE_H

/*
 * Port addition: a small cache of rasterized TrueType lines (ttfcache.c).
 *
 * Font_DrawLineTTF (font.c, 0x0046C6F0) rasterizes its text with SDL_ttf and
 * pushes the result through a temporary SDL surface -- and, on the OpenGL
 * renderers, through a temporary SpriteSet -- on *every* call. HUD text draws
 * the same strings at the same size and colour every frame ("ROUND 1", a
 * character name, the life numbers, the "PAUSE" box), so those rasterizations
 * repeat identically frame after frame.
 *
 * This caches the rasterized pixels, keyed on everything that affects them
 * (font, colour, blend mode and the exact bytes of the line). A hit skips only
 * the SDL_ttf work: the caller still submits the pixels the same way it did
 * before, so what reaches the screen is unchanged. The cache owns its pixel
 * buffers and drops the least recently used entry when it is full.
 *
 * No SDL and no engine dependency: the caller passes the render function in,
 * which is what makes this testable against a stub (tools/test_ttfcache.sh).
 */

typedef struct TtfCache TtfCache;

/* Rasterizes one line. Returns a malloc'd RGBA32 buffer of *w x *h, or NULL. */
typedef void *(*TtfCache_RenderFn)(const char *text, int r, int g, int b, int blended,
                                   int *w, int *h, void *ctx);

typedef struct TtfCacheEntry {
    int used;                   /* entry holds a rasterization */
    unsigned long tick;         /* for least-recently-used replacement */
    unsigned long font_id;      /* identity of the font, caller-defined */
    int r, g, b;
    int blended;
    char *text;
    void *pixels;               /* RGBA32, owned by the cache */
    int w, h;
} TtfCacheEntry;

struct TtfCache {
    TtfCacheEntry *entries;
    int capacity;
    int count;
    unsigned long clock;
    /* Statistics, so a caller (and the tests) can tell a hit from a miss. */
    unsigned long hits;
    unsigned long misses;
    unsigned long evictions;
};

/* Creates a cache of `capacity` entries. NULL when capacity <= 0 or out of
 * memory. */
TtfCache *TtfCache_Create(int capacity);

/* Frees every cached rasterization and the cache itself. NULL is accepted. */
void TtfCache_Free(TtfCache *cache);

/*
 * The pixels for one line, rasterizing through `render` only on a miss.
 * Returns the RGBA32 buffer, its size in *w / *h, and 1 in *hit when it came
 * from the cache. NULL when the line cannot be rasterized (or out of memory),
 * in which case the caller draws nothing, exactly as before.
 *
 * The returned buffer belongs to the cache and stays valid until the next call
 * that can evict it; callers copy or submit it immediately.
 */
const void *TtfCache_Get(TtfCache *cache, unsigned long font_id, int r, int g, int b, int blended,
                         const char *text, int *w, int *h, int *hit,
                         TtfCache_RenderFn render, void *ctx);

/* Drops every entry, e.g. when a GL context is lost and any texture made from
 * these pixels is gone. */
void TtfCache_Clear(TtfCache *cache);

#endif /* MUGEN_TTFCACHE_H */
