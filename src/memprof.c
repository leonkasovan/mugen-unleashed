/*
 * Port addition: see memprof.h for what this counts and why.
 * MUGEN_MEMPROF_NO_WRAP makes this translation unit exempt from the malloc
 * macros in mugen.h, so the raw CRT calls below stay CRT calls.
 */
#define MUGEN_MEMPROF_NO_WRAP 1
#include "mugen.h"
#include "memprof.h"

/*
 * Side table of allocation sizes. Storing a header next to the payload cannot
 * distinguish engine allocations from pointers the CRT gave us before the
 * counters were installed (or pointers passed by accident). Raw malloc plus a
 * hash insert is slightly more expensive, but it is always safe.
 *
 * All counters are 64-bit so the arithmetic does not depend on size_t width.
 */
typedef struct MemprofEntry {
    void *ptr;
    size_t size;
    struct MemprofEntry *next;
} MemprofEntry;

#define MEMPROF_BUCKETS 1024
static MemprofEntry *memprof_buckets[MEMPROF_BUCKETS];
static volatile int memprof_lock; /* 0 free, 1 held */

static uint64_t memprof_heap_live, memprof_heap_peak;
static uint64_t memprof_heap_allocs, memprof_heap_frees, memprof_heap_fails;
static uint64_t memprof_lua_live, memprof_lua_peak;
static uint64_t memprof_lua_allocs, memprof_lua_frees, memprof_lua_fails;

static void Memprof_Lock(void)
{
    while (__sync_lock_test_and_set(&memprof_lock, 1)) {
        /* Spin. The table is only held while inserting/removing and the
         * critical section is a few instructions. */
    }
}

static void Memprof_Unlock(void)
{
    __sync_lock_release(&memprof_lock);
}

static size_t Memprof_Hash(const void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;

    p >>= 4; /* malloc is 16-byte aligned; fold the low bits away */
    p ^= p >> 16;
    return (size_t)p & (MEMPROF_BUCKETS - 1);
}

static MemprofEntry **Memprof_FindEntryLocked(void *ptr)
{
    size_t bucket = Memprof_Hash(ptr);
    MemprofEntry **link = &memprof_buckets[bucket];

    while (*link != NULL) {
        if ((*link)->ptr == ptr) {
            return link;
        }
        link = &(*link)->next;
    }
    return NULL;
}

static int Memprof_EntryAddLocked(void *ptr, size_t size)
{
    size_t bucket = Memprof_Hash(ptr);
    MemprofEntry *entry = (MemprofEntry *)malloc(sizeof(MemprofEntry));

    if (entry == NULL) {
        return 0;
    }
    entry->ptr = ptr;
    entry->size = size;
    entry->next = memprof_buckets[bucket];
    memprof_buckets[bucket] = entry;
    return 1;
}

static int Memprof_EntryRemove(void *ptr, size_t *size_out)
{
    int found = 0;

    Memprof_Lock();
    {
        MemprofEntry **link = Memprof_FindEntryLocked(ptr);

        if (link != NULL) {
            MemprofEntry *entry = *link;

            *link = entry->next;
            if (size_out != NULL) {
                *size_out = entry->size;
            }
            free(entry);
            found = 1;
        }
    }
    Memprof_Unlock();
    return found;
}

static int Memprof_EntryAdd(void *ptr, size_t size)
{
    int added;

    Memprof_Lock();
    added = Memprof_EntryAddLocked(ptr, size);
    Memprof_Unlock();
    return added;
}

/* `delta` is signed; the cast wraps in two's complement, so a negative delta
 * subtracts correctly. */
static void Memprof_Add(uint64_t *live, uint64_t *peak, int64_t delta, int allocating)
{
    uint64_t value = __sync_add_and_fetch(live, (uint64_t)delta);

    if (allocating && delta > 0) {
        for (;;) {
            uint64_t peak_value = __sync_add_and_fetch(peak, 0);

            if (value <= peak_value) {
                break;
            }
            if (__sync_bool_compare_and_swap(peak, peak_value, value)) {
                break;
            }
        }
    }
}

void *Memprof_Malloc(size_t size)
{
    void *p = malloc(size);

    if (p == NULL) {
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    if (!Memprof_EntryAdd(p, size)) {
        /* Cannot track it; refuse the allocation rather than miscount. */
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)size, 1);
    return p;
}

void *Memprof_Calloc(size_t count, size_t size)
{
    size_t total;
    void *p;

    if (count != 0 && size > SIZE_MAX / count) {
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    total = count * size;
    p = calloc(count, size);
    if (p == NULL) {
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    if (!Memprof_EntryAdd(p, total)) {
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)total, 1);
    return p;
}

void *Memprof_Realloc(void *ptr, size_t size)
{
    size_t old_size;
    void *fresh;

    if (ptr == NULL) {
        return Memprof_Malloc(size);
    }
    if (!Memprof_EntryRemove(ptr, &old_size)) {
        /* Not ours: pass through without disturbing the counters. */
        if (size == 0) {
            free(ptr);
            return NULL;
        }
        return realloc(ptr, size);
    }
    if (size == 0) {
        free(ptr);
        __sync_fetch_and_add(&memprof_heap_frees, 1);
        Memprof_Add(&memprof_heap_live, &memprof_heap_peak, -(int64_t)old_size, 0);
        return NULL;
    }
    fresh = realloc(ptr, size);
    if (fresh == NULL) {
        /* Put the old entry back; the CRT block is unchanged and still ours. */
        Memprof_EntryAdd(ptr, old_size);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    if (!Memprof_EntryAdd(fresh, size)) {
        free(fresh);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak,
                (int64_t)size - (int64_t)old_size, size > old_size);
    return fresh;
}

void Memprof_Free(void *ptr)
{
    size_t size;

    if (ptr == NULL) {
        return;
    }
    if (!Memprof_EntryRemove(ptr, &size)) {
        free(ptr);
        return;
    }
    free(ptr);
    __sync_fetch_and_add(&memprof_heap_frees, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, -(int64_t)size, 0);
}

void *Memprof_LuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    void *result;

    (void)ud;
    (void)osize;

    if (nsize == 0) {
        if (ptr != NULL) {
            free(ptr);
            __sync_fetch_and_add(&memprof_lua_frees, 1);
            if (osize > 0) {
                Memprof_Add(&memprof_lua_live, &memprof_lua_peak, -(int64_t)osize, 0);
            }
        }
        return NULL;
    }
    if (ptr == NULL) {
        result = malloc(nsize);
        if (result == NULL) {
            __sync_fetch_and_add(&memprof_lua_fails, 1);
            return NULL;
        }
        __sync_fetch_and_add(&memprof_lua_allocs, 1);
        Memprof_Add(&memprof_lua_live, &memprof_lua_peak, (int64_t)nsize, 1);
        return result;
    }
    /*
     * Lua passes osize = the previous block's payload, so realloc works
     * without a header of our own. A failure leaves ptr untouched, which is
     * what Lua expects (the old block stays valid).
     */
    result = realloc(ptr, nsize);
    if (result == NULL) {
        __sync_fetch_and_add(&memprof_lua_fails, 1);
        return NULL;
    }
    if (nsize > osize) {
        Memprof_Add(&memprof_lua_live, &memprof_lua_peak, (int64_t)(nsize - osize), 1);
    } else if (osize > nsize) {
        Memprof_Add(&memprof_lua_live, &memprof_lua_peak, -(int64_t)(osize - nsize), 0);
    }
    __sync_fetch_and_add(&memprof_lua_allocs, 1);
    return result;
}

void Memprof_Stats(MemprofStats *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->heap_live = (size_t)memprof_heap_live;
    out->heap_peak = (size_t)memprof_heap_peak;
    out->heap_allocs = (unsigned long)memprof_heap_allocs;
    out->heap_frees = (unsigned long)memprof_heap_frees;
    out->heap_fails = (unsigned long)memprof_heap_fails;
    out->lua_live = (size_t)memprof_lua_live;
    out->lua_peak = (size_t)memprof_lua_peak;
    out->lua_allocs = (unsigned long)memprof_lua_allocs;
    out->lua_frees = (unsigned long)memprof_lua_frees;
    out->lua_fails = (unsigned long)memprof_lua_fails;
}

void Memprof_Log(const char *tag)
{
    MemprofStats s;

    Memprof_Stats(&s);
    Log_Printf("mem: %s heap_live=%lu heap_peak=%lu heap_allocs=%lu heap_frees=%lu heap_fails=%lu lua_live=%lu lua_peak=%lu lua_allocs=%lu lua_frees=%lu lua_fails=%lu\n",
               tag != NULL ? tag : "",
               (unsigned long)s.heap_live, (unsigned long)s.heap_peak,
               s.heap_allocs, s.heap_frees, s.heap_fails,
               (unsigned long)s.lua_live, (unsigned long)s.lua_peak,
               s.lua_allocs, s.lua_frees, s.lua_fails);
}
