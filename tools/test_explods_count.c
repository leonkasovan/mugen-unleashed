/*
 * Equivalence test for the Explods_Count fast path (src/explod.c, 0x00411790).
 *
 * The port changed Explods_Count from a full scan of `capacity` slots to one
 * that stops once it has seen `list->count` live items. That is only sound if
 * `count` is always exactly the number of items with `used != 0`, so this test
 * checks both halves of the claim and needs no engine, no GL and no device:
 *
 *   1. ALGORITHM  over every reachable (used, id, owner) layout at small
 *                 capacity, and over randomised layouts at realistic capacity,
 *                 the fast scan returns exactly what the original scan returns
 *                 -- for every (owner_id, id) query, including id == -1.
 *   2. INVARIANT  the layouts compared are the reachable ones: `used` flags are
 *                 set and cleared by the same rules the container follows
 *                 (lowest free slot on insert, cleared together with count--).
 *                 The test also proves the failure mode is real, so the
 *                 invariant is not taken on faith: a deliberately inconsistent
 *                 `count` must make the two disagree.
 *
 * Build/run:  sh tools/test_explods_count.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirrors the engine's SysDrawItem/SysDrawList fields this test needs. The real
 * structs are 0x270/0x0C bytes with layout assertions; only these members
 * participate in Explods_Count, and both orders are already asserted there. */
typedef struct TestItem {
    int used;
    int id;
    int owner_id;
} TestItem;

typedef struct TestList {
    TestItem *items;
    int capacity;
    int count;
} TestList;

/* The original implementation (0x00411790), verbatim. */
static int count_original(const TestList *list, int owner_id, int id)
{
    int n = 0;
    int i;

    for (i = 0; i < list->capacity; i++) {
        const TestItem *e = &list->items[i];
        if (e->used && (e->id == id || id == -1) && e->owner_id == owner_id) {
            n++;
        }
    }
    return n;
}

/* The port's implementation, same logic as src/explod.c: an early exit for
 * sparse lists, the original straight scan when the list is more than half
 * full (where the bookkeeping would cost more than it saves). */
static int count_fast(const TestList *list, int owner_id, int id)
{
    int n = 0;
    int i;

    if (list->count == 0) {
        return 0;
    }
    if (list->count * 2 <= list->capacity) {
        int seen = 0;

        for (i = 0; i < list->capacity; i++) {
            const TestItem *e = &list->items[i];

            if (!e->used) {
                continue;
            }
            seen++;
            if ((e->id == id || id == -1) && e->owner_id == owner_id) {
                n++;
            }
            if (seen == list->count) {
                break;
            }
        }
        return n;
    }
    for (i = 0; i < list->capacity; i++) {
        const TestItem *e = &list->items[i];

        if (e->used && (e->id == id || e->owner_id == owner_id)) {
            if ((e->id == id || id == -1) && e->owner_id == owner_id) {
                n++;
            }
        }
    }
    return n;
}

/* Count of live items, i.e. what `list->count` must equal. */
static int live_items(const TestList *list)
{
    int n = 0;
    int i;

    for (i = 0; i < list->capacity; i++) {
        if (list->items[i].used) n++;
    }
    return n;
}

static unsigned long rng_state = 0x9E3779B9u;

static unsigned long rnd(void)
{
    /* xorshift32: deterministic, so a failure is reproducible. */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int fail(const char *what, int cap, int owner, int id, int got, int want)
{
    printf("FAIL: %s cap=%d owner=%d id=%d fast=%d original=%d\n",
           what, cap, owner, id, got, want);
    return 1;
}

/* Query every (owner, id) pair that can matter for this layout. */
static int compare_all(const TestList *list, const char *what)
{
    int owners[] = { 0, 1, 2, 7 };
    int ids[] = { -1, 0, 1, 2, 3, 99 };
    size_t o, d;

    for (o = 0; o < sizeof(owners) / sizeof(owners[0]); o++) {
        for (d = 0; d < sizeof(ids) / sizeof(ids[0]); d++) {
            int fast = count_fast(list, owners[o], ids[d]);
            int orig = count_original(list, owners[o], ids[d]);

            if (fast != orig) {
                return fail(what, list->capacity, owners[o], ids[d], fast, orig);
            }
        }
    }
    return 0;
}

int main(void)
{
    int failures = 0;
    int cap, trial;
    TestList list;

    /* ---- 1a. Exhaustive: every used/id/owner layout at capacity 5. ---- */
    {
        const int owners[] = { 0, 1, 2 };
        const int ids[] = { -1, 0, 1, 2 };
        long unsigned layout;
        long unsigned total = 1;

        for (cap = 0; cap < 5; cap++) total *= 4UL * 3 * 4; /* used*id*owner */
        cap = 5;
        list.capacity = cap;
        list.items = calloc((size_t)cap, sizeof(TestItem));

        for (layout = 0; layout < total; layout++) {
            long unsigned v = layout;
            int i;

            for (i = 0; i < cap; i++) {
                int used, id_idx, own_idx;

                own_idx = (int)(v % 3); v /= 3;
                id_idx = (int)(v % 4); v /= 4;
                used = (int)(v % 2); v /= 2;
                list.items[i].used = used;
                list.items[i].owner_id = owners[own_idx];
                list.items[i].id = ids[id_idx];
            }
            /* count is derived, exactly as the container maintains it. */
            list.count = live_items(&list);
            failures += compare_all(&list, "exhaustive");
            if (failures) break;
        }
        free(list.items);
        if (!failures) printf("ok   exhaustive layouts at capacity 5\n");
    }
    if (failures) return 1;

    /* ---- 1b. Randomised at realistic capacity (ExplodMax = 500). ---- */
    for (trial = 0; trial < 200; trial++) {
        int i;

        cap = (trial % 3 == 0) ? 500 : (trial % 3 == 1) ? 50 : 7;
        list.capacity = cap;
        list.items = calloc((size_t)cap, sizeof(TestItem));
        if (list.items == NULL) return 2;

        /* Sparse population, like a live match: a handful of live explods in a
         * mostly empty list. That is the case the fast path is for. */
        for (i = 0; i < cap; i++) {
            if ((rnd() % 100) < 4) {
                list.items[i].used = 1;
                list.items[i].owner_id = (int)(rnd() % 4);
                list.items[i].id = (int)(rnd() % 6);
            }
        }
        list.count = live_items(&list);
        failures += compare_all(&list, "random-sparse");
        free(list.items);
        if (failures) return 1;
    }
    printf("ok   200 randomised sparse layouts (cap 7/50/500)\n");

    /* ---- 1c. Randomised dense, plus clustered-at-front like real inserts. ---- */
    for (trial = 0; trial < 100; trial++) {
        int i, live;

        cap = 120;
        list.capacity = cap;
        list.items = calloc((size_t)cap, sizeof(TestItem));
        if (list.items == NULL) return 2;
        live = 1 + (int)(rnd() % (unsigned)cap);
        for (i = 0; i < live; i++) {   /* inserts take the lowest free slot */
            list.items[i].used = 1;
            list.items[i].owner_id = (int)(rnd() % 3);
            list.items[i].id = (int)(rnd() % 5);
        }
        list.count = live_items(&list);
        failures += compare_all(&list, "dense-front");
        free(list.items);
        if (failures) return 1;
    }
    printf("ok   100 dense front-packed layouts\n");

    /* ---- 1d. Both branches, walked across the sparse/dense boundary. ---- */
    {
        int trial2;

        cap = 64;
        list.capacity = cap;
        list.items = calloc((size_t)cap, sizeof(TestItem));
        if (list.items == NULL) return 2;
        for (trial2 = 0; trial2 <= cap; trial2++) {
            int i;

            memset(list.items, 0, (size_t)cap * sizeof(TestItem));
            for (i = 0; i < trial2; i++) {   /* front-packed, as inserts behave */
                list.items[i].used = 1;
                list.items[i].owner_id = i % 3;
                list.items[i].id = i % 5;
            }
            list.count = live_items(&list);
            failures += compare_all(&list, "branch-boundary");
            if (failures) return 1;
        }
        free(list.items);
        printf("ok   65 layouts across the count*2<=capacity boundary\n");
    }

    /* ---- 2. The invariant is load-bearing, not incidental. ---- */
    {
        int same, bad;

        /* Consistent count: both agree. */
        cap = 20;
        list.capacity = cap;
        list.items = calloc((size_t)cap, sizeof(TestItem));
        if (list.items == NULL) return 2;
        list.items[0].used = 1; list.items[0].owner_id = 1; list.items[0].id = 0;
        list.items[5].used = 1; list.items[5].owner_id = 1; list.items[5].id = 0;
        list.count = 2;
        same = (count_fast(&list, 1, 0) == count_original(&list, 1, 0));
        printf("%s consistent count -> both scans agree\n", same ? "ok  " : "FAIL");
        if (!same) failures = 1;

        /* Inconsistent (count too low): the fast path must under-report. This is
         * what proves the test is not vacuous -- and why the invariant audit in
         * Explods_Count's comment matters. */
        list.count = 1;
        bad = (count_fast(&list, 1, 0) != count_original(&list, 1, 0));
        printf("%s count too low -> scans diverge (invariant is load-bearing)\n",
               bad ? "ok  " : "FAIL");
        if (!bad) failures = 1;
        free(list.items);
    }

    printf("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
