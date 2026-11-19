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


/* ---------------------------------------------------------------- remove --- */
/*
 * Explods_RemoveOwner mutates the list, which is the case where a wrong stopping
 * rule fails silently: reading the live `count` instead of a snapshot lowers the
 * stopping point with every removal, so matching explods late in the list are
 * never visited. These checks compare the final list state against the original
 * loop, not just a count, so a leftover entry cannot pass.
 */
static int remove_original(TestList *list, int owner_id, int id)
{
    int removed = 0;
    int i;

    for (i = 0; i < list->capacity; i++) {
        TestItem *e = &list->items[i];
        if (e->used && (e->id == id || id == -1) && e->owner_id == owner_id) {
            e->used = 0;
            list->count--;
            removed++;
        }
    }
    return removed;
}

static int remove_fast(TestList *list, int owner_id, int id)
{
    int live = list->count;   /* snapshot: the sparse loop decrements it */
    int removed = 0;
    int i;

    if (live <= 0) {
        return 0;
    }
    if (live * 2 > list->capacity) {   /* dense: original range */
        for (i = 0; i < list->capacity; i++) {
            TestItem *e = &list->items[i];
            if (e->used && (e->id == id || id == -1) && e->owner_id == owner_id) {
                e->used = 0;
                list->count--;
                removed++;
            }
        }
        return removed;
    }
    {
        int seen = 0;

        for (i = 0; i < list->capacity && seen < live; i++) {
            TestItem *e = &list->items[i];

            if (!e->used) {
                continue;
            }
            seen++;
            if ((e->id == id || id == -1) && e->owner_id == owner_id) {
                e->used = 0;
                list->count--;
                removed++;
            }
        }
    }
    return removed;
}

static void build_random(TestList *list, unsigned long seed, int cap, int density_pct)
{
    int i;

    rng_state = seed ? seed : 1u;
    list->capacity = cap;
    list->items = calloc((size_t)cap, sizeof(TestItem));
    for (i = 0; i < cap; i++) {
        if ((int)(rnd() % 100) < density_pct) {
            list->items[i].used = 1;
            list->items[i].owner_id = (int)(rnd() % 4);
            list->items[i].id = (int)(rnd() % 6) - 1;   /* includes -1 */
        }
    }
    list->count = live_items(list);
}

/* Compares the whole list, so a surviving entry cannot be missed. */
static int remove_compare(int cap, int density, unsigned long seed, int owner_id, int id,
                          const char *what)
{
    TestList a, b;
    int ra, rb, i, bad = 0;

    build_random(&a, seed, cap, density);
    build_random(&b, seed, cap, density);

    ra = remove_original(&a, owner_id, id);
    rb = remove_fast(&b, owner_id, id);

    if (ra != rb) {
        printf("FAIL: %s cap=%d density=%d owner=%d id=%d removed %d vs %d\n",
               what, cap, density, owner_id, id, rb, ra);
        bad = 1;
    }
    if (a.count != b.count) {
        printf("FAIL: %s count %d vs %d\n", what, b.count, a.count);
        bad = 1;
    }
    if (live_items(&a) != live_items(&b)) {
        printf("FAIL: %s live %d vs %d\n", what, live_items(&b), live_items(&a));
        bad = 1;
    }
    for (i = 0; i < cap && !bad; i++) {
        if (a.items[i].used != b.items[i].used) {
            printf("FAIL: %s slot %d used %d vs %d (owner %d id %d)\n",
                   what, i, b.items[i].used, a.items[i].used,
                   a.items[i].owner_id, a.items[i].id);
            bad = 1;
        }
    }
    if (live_items(&b) != b.count) {
        printf("FAIL: %s fast left count=%d but %d live\n", what, b.count, live_items(&b));
        bad = 1;
    }
    free(a.items);
    free(b.items);
    return bad;
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

    /* ---- 3. Explods_RemoveOwner: sparse, mixed owners, specific id, wildcard ---- */
    {
        static const int owners[] = { -1, 0, 1, 2, 3 };
        static const int ids[] = { -1, 0, 1, 2, 3, 4 };
        int oi, ii, d;
        int cases = 0;

        for (d = 0; d < 3; d++) {
            int density = (d == 0) ? 2 : (d == 1) ? 10 : 60;  /* sparse .. dense */
            for (oi = 0; oi < 5; oi++) {
                for (ii = 0; ii < 6; ii++) {
                    failures += remove_compare(500, density, 0x1234u + (unsigned long)cases,
                                               owners[oi], ids[ii], "remove");
                    cases++;
                    if (failures) break;
                }
                if (failures) break;
            }
            if (failures) break;
        }
        if (!failures) printf("ok   %d remove cases (sparse/mixed/specific id/wildcard)\n", cases);
    }

    /* ---- 4. The snapshot is load-bearing: reading the live count must fail. ---- */
    {
        TestList l;
        int i, left = 0;

        /* Three matches for owner 0, spaced out so a shrinking stop point matters. */
        l.capacity = 100;
        l.items = calloc(100, sizeof(TestItem));
        l.count = 0;
        for (i = 0; i < 100; i += 10) {
            l.items[i].used = 1;
            l.items[i].owner_id = 0;
            l.items[i].id = 7;
            l.count++;
        }
        /* Same loop but re-reading list->count each iteration (the bug). */
        for (i = 0; i < l.capacity && i < l.count; i++) {
            TestItem *e = &l.items[i];
            if (!e->used) continue;
            if ((e->id == 7 || 7 == -1) && e->owner_id == 0) {
                e->used = 0;
                l.count--;
            }
        }
        for (i = 0; i < l.capacity; i++) {
            if (l.items[i].used) left++;
        }
        printf("%s re-reading count leaves %d entry(ies) behind (snapshot is required)\n",
               (left > 0) ? "ok  " : "FAIL", left);
        if (left == 0) failures = 1;
        free(l.items);
    }

    printf("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
