/*
 * Gate for the rasterized-TrueType-line cache (src/ttfcache.c).
 *
 * The cache skips SDL_ttf work, so the thing that must not go wrong is that a
 * hit returns the pixels a fresh rasterization would have produced -- a stale
 * or colliding key would draw the wrong text, at the wrong colour, or at the
 * wrong size. The render function is stubbed here with deterministic,
 * key-derived bytes, so every assertion is about the cache's own bookkeeping:
 *
 *   - a repeat call returns the *same* buffer and reports a hit;
 *   - every part of the key separates entries: colour, blend mode, font and
 *     text, including strings that differ only in a trailing character;
 *   - the pixels of a hit are byte-identical to a fresh render of the same key;
 *   - a failed render is not cached and cannot be mistaken for a hit;
 *   - a full cache evicts the least recently used entry, so a hot entry
 *     survives a flood of one-shot ones;
 *   - capacity 0 / NULL are refused rather than crashing.
 *
 * Usage: sh tools/test_ttfcache.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ttfcache.h"

static int failures;
static int renders;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

/*
 * A stub rasterizer whose output is a deterministic function of the whole key,
 * so a wrong entry is detectable rather than merely plausible. Width and height
 * also depend on the key, so a size mix-up shows up.
 */
static void *stub_render(const char *text, int r, int g, int b, int blended, int *w, int *h, void *ctx)
{
    int *fail_flag = (int *)ctx;
    unsigned int seed = 2166136261u;
    size_t n = strlen(text);
    int width, height, i;
    unsigned char *pixels;

    renders++;
    if (fail_flag != NULL && *fail_flag) {
        return NULL;
    }
    for (i = 0; i < (int)n; i++) {
        seed ^= (unsigned char)text[i];
        seed *= 16777619u;
    }
    seed ^= (unsigned int)r | ((unsigned int)g << 8) | ((unsigned int)b << 16) |
            ((unsigned int)blended << 24);
    seed *= 16777619u;

    width = (int)(seed % 97) + 1;
    height = (int)((seed >> 8) % 31) + 1;
    pixels = malloc((size_t)width * height * 4);
    if (pixels == NULL) {
        return NULL;
    }
    for (i = 0; i < width * height * 4; i++) {
        pixels[i] = (unsigned char)((seed >> ((i % 4) * 8)) ^ (unsigned char)i);
    }
    *w = width;
    *h = height;
    return pixels;
}

static void test_hit_and_identity(void)
{
    TtfCache *cache = TtfCache_Create(4);
    const void *first;
    const void *second;
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0, hit = -1;
    unsigned char copy[4096];
    size_t bytes;

    check(cache != NULL, "cache is created");
    if (cache == NULL) {
        return;
    }

    renders = 0;
    first = TtfCache_Get(cache, 1, 10, 20, 30, 1, "ROUND 1", &w1, &h1, &hit, stub_render, NULL);
    check(first != NULL, "first call rasterizes");
    check(hit == 0 && renders == 1, "first call is a miss");

    second = TtfCache_Get(cache, 1, 10, 20, 30, 1, "ROUND 1", &w2, &h2, &hit, stub_render, NULL);
    check(second == first, "a repeat returns the same buffer");
    check(hit == 1 && renders == 1, "a repeat does not rasterize again");
    check(w1 == w2 && h1 == h2, "a repeat reports the same size");

    /* The cached bytes are exactly what a fresh render produces. */
    bytes = (size_t)w1 * h1 * 4;
    memcpy(copy, first, bytes);
    renders = 0;
    {
        int fw = 0, fh = 0;
        void *fresh = stub_render("ROUND 1", 10, 20, 30, 1, &fw, &fh, NULL);

        check(fresh != NULL && fw == w1 && fh == h1, "fresh render agrees on size");
        check(fresh != NULL && memcmp(fresh, copy, bytes) == 0,
              "cached pixels are byte-identical to a fresh render");
        free(fresh);
    }
    TtfCache_Free(cache);
}

static void test_key_separation(void)
{
    TtfCache *cache = TtfCache_Create(8);
    const void *base;
    int w = 0, h = 0, hit = 0;

    check(cache != NULL, "cache is created");
    if (cache == NULL) {
        return;
    }
    base = TtfCache_Get(cache, 1, 10, 20, 30, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(base != NULL && hit == 0, "base entry rasterized");

    /* Each key component must produce its own entry (a miss), not a hit. */
    hit = -1;
    (void)TtfCache_Get(cache, 1, 11, 20, 30, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a different red is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 21, 30, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a different green is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 20, 31, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a different blue is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 20, 30, 0, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a different blend mode is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 2, 10, 20, 30, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a different font is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 20, 30, 1, "abd", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "text differing in the last character is a different entry");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 20, 30, 1, "ab", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "a prefix of the text is a different entry");

    /* And the original is still there. */
    hit = -1;
    (void)TtfCache_Get(cache, 1, 10, 20, 30, 1, "abc", &w, &h, &hit, stub_render, NULL);
    check(hit == 1, "the original entry survived the others");
    TtfCache_Free(cache);
}

static void test_failed_render_not_cached(void)
{
    TtfCache *cache = TtfCache_Create(4);
    int fail = 1;
    int w = 0, h = 0, hit = -1;

    check(cache != NULL, "cache is created");
    if (cache == NULL) {
        return;
    }
    check(TtfCache_Get(cache, 1, 0, 0, 0, 0, "x", &w, &h, &hit, stub_render, &fail) == NULL,
          "a failed render returns NULL");
    fail = 0;
    hit = -1;
    check(TtfCache_Get(cache, 1, 0, 0, 0, 0, "x", &w, &h, &hit, stub_render, &fail) != NULL,
          "the same key rasterizes once the stub succeeds");
    check(hit == 0, "a failed render did not leave a cache entry");
    TtfCache_Free(cache);
}

static void test_lru_eviction(void)
{
    TtfCache *cache = TtfCache_Create(2);
    int w = 0, h = 0, hit = -1;

    check(cache != NULL, "cache is created");
    if (cache == NULL) {
        return;
    }
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "hot", &w, &h, &hit, stub_render, NULL);
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "cold", &w, &h, &hit, stub_render, NULL);
    /* Touch "hot" so "cold" is the least recently used. */
    hit = -1;
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "hot", &w, &h, &hit, stub_render, NULL);
    check(hit == 1, "hot entry hits");

    /* Filling with a third entry must evict "cold", not "hot". */
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "third", &w, &h, &hit, stub_render, NULL);
    hit = -1;
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "hot", &w, &h, &hit, stub_render, NULL);
    check(hit == 1, "the hot entry survived eviction");
    hit = -1;
    (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "cold", &w, &h, &hit, stub_render, NULL);
    check(hit == 0, "the least recently used entry was evicted");
    TtfCache_Free(cache);
}

static void test_degenerate(void)
{
    int w = 0, h = 0, hit = 0;
    TtfCache *cache = TtfCache_Create(2);

    check(TtfCache_Create(0) == NULL, "capacity 0 is refused");
    check(TtfCache_Get(NULL, 1, 0, 0, 0, 0, "x", &w, &h, &hit, stub_render, NULL) == NULL,
          "a NULL cache returns NULL");
    check(cache != NULL, "cache is created");
    if (cache != NULL) {
        check(TtfCache_Get(cache, 1, 0, 0, 0, 0, NULL, &w, &h, &hit, stub_render, NULL) == NULL,
              "NULL text returns NULL");
        check(TtfCache_Get(cache, 1, 0, 0, 0, 0, "x", &w, &h, &hit, NULL, NULL) == NULL,
              "a NULL render returns NULL");

        /* Clear drops the entries, so the next call rasterizes again. */
        (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "x", &w, &h, &hit, stub_render, NULL);
        TtfCache_Clear(cache);
        renders = 0;
        hit = -1;
        (void)TtfCache_Get(cache, 1, 0, 0, 0, 0, "x", &w, &h, &hit, stub_render, NULL);
        check(hit == 0 && renders == 1, "Clear() drops the entries");
        TtfCache_Free(cache);
    }
    TtfCache_Free(NULL);    /* must not crash */
}

int main(void)
{
    test_hit_and_identity();
    test_key_separation();
    test_failed_render_not_cached();
    test_lru_eviction();
    test_degenerate();

    if (failures != 0) {
        printf("test_ttfcache: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ttfcache: OK\n");
    return 0;
}
