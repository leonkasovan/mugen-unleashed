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
    void *pc;
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

static int Memprof_EntryAddLocked(void *ptr, size_t size, void *pc)
{
    size_t bucket = Memprof_Hash(ptr);
    MemprofEntry *entry = (MemprofEntry *)malloc(sizeof(MemprofEntry));

    if (entry == NULL) {
        return 0;
    }
    entry->ptr = ptr;
    entry->size = size;
    entry->pc = pc;
    entry->next = memprof_buckets[bucket];
    memprof_buckets[bucket] = entry;
    return 1;
}

static int Memprof_EntryRemoveLocked(void *ptr, size_t *size_out, void **pc_out)
{
    MemprofEntry **link = Memprof_FindEntryLocked(ptr);

    if (link == NULL) {
        return 0;
    }
    {
        MemprofEntry *entry = *link;

        *link = entry->next;
        if (size_out != NULL) {
            *size_out = entry->size;
        }
        if (pc_out != NULL) {
            *pc_out = entry->pc;
        }
        free(entry);
        return 1;
    }
}

static int Memprof_EntryRemove(void *ptr, size_t *size_out, void **pc_out)
{
    int found = 0;

    Memprof_Lock();
    found = Memprof_EntryRemoveLocked(ptr, size_out, pc_out);
    Memprof_Unlock();
    return found;
}

static int Memprof_EntryAdd(void *ptr, size_t size, void *pc)
{
    int added;

    Memprof_Lock();
    added = Memprof_EntryAddLocked(ptr, size, pc);
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

/*
 * Call-site counters. Enabled with MUGEN_MEMPROF=1 (same shape as
 * MUGEN_PROF). Each engine allocation stores the caller PC in its entry; a
 * fixed-size table ranks live bytes by site. A symbolizable dump is rewritten
 * on every Memprof_Log call (about once a second), so the dump survives a kill.
 *
 * The static table is read under the heap lock here, so no separate lock is
 * needed; Memprof_Lock is taken by every FindSiteLocked caller.
 */
#define MEMPROF_SITE_BUCKETS 256
typedef struct MemprofSite {
    void *pc;
    uint64_t live, peak;
    unsigned long allocs, frees;
    int inuse;
} MemprofSite;
static MemprofSite memprof_sites[MEMPROF_SITE_BUCKETS];
static int memprof_site_enabled;
static const char *memprof_dump_path;

/*
 * `create` is 0 for a lookup from the free path: a site that was never
 * recorded when its block was allocated (for example because the table was
 * full) must not be invented on the way out, or its live count goes negative.
 */
static MemprofSite *Memprof_FindSiteLocked(void *pc, int create)
{
    size_t slot = ((uintptr_t)pc >> 2) % MEMPROF_SITE_BUCKETS;
    int i;

    for (i = 0; i < MEMPROF_SITE_BUCKETS; i++) {
        MemprofSite *s = &memprof_sites[(slot + i) % MEMPROF_SITE_BUCKETS];

        if (s->inuse && s->pc == pc) {
            return s;
        }
        if (!s->inuse) {
            if (!create) {
                return NULL;
            }
            memset(s, 0, sizeof(*s));
            s->pc = pc;
            s->inuse = 1;
            return s;
        }
    }
    return NULL; /* table full: site not tracked */
}

/*
 * Load base the captured PCs are measured from, so a symbolizer can turn them
 * back into file addresses. Windows relocates a DYNAMICBASE image per run, so
 * the dump records this value; on ELF the addresses are already link-time.
 */
static uintptr_t Memprof_ModuleBase(void)
{
#if defined(_WIN32)
    HMODULE module = GetModuleHandleA(NULL);

    return (uintptr_t)module;
#else
    return 0;
#endif
}

/*
 * Writes the site table to a `path` file. Called by Memprof_Stop and (from
 * Memprof_Log) once a second when MUGEN_MEMPROF=1 is set, so the dump reflects
 * the steady-state mix rather than whatever the caller kills the process at.
 */
static void Memprof_SiteDump(void)
{
    FILE *fp;
    int i, n = 0;
    MemprofSite snapshot[MEMPROF_SITE_BUCKETS];

    if (!memprof_site_enabled || memprof_dump_path == NULL) {
        return;
    }
    Memprof_Lock();
    memcpy(snapshot, memprof_sites, sizeof(snapshot));
    Memprof_Unlock();

    fp = fopen(memprof_dump_path, "w");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "# mugen memory-by-site dump\n");
    /*
     * The captured PCs are absolute runtime addresses. Windows loads this
     * image at a relocated base (DYNAMICBASE), so a symbolizer must subtract
     * the base of *this* run; writing it here keeps the dump self-contained.
     */
    fprintf(fp, "# base 0x%llx\n", (unsigned long long)Memprof_ModuleBase());
    fprintf(fp, "# format: live_bytes peak_bytes allocs frees 0x<pc>\n");
    for (i = 0; i < MEMPROF_SITE_BUCKETS; i++) {
        MemprofSite *s = &snapshot[i];

        if (!s->inuse) {
            continue;
        }
        fprintf(fp, "%llu %llu %lu %lu 0x%llx\n",
                (unsigned long long)s->live, (unsigned long long)s->peak,
                s->allocs, s->frees,
                (unsigned long long)(uintptr_t)s->pc);
        n++;
    }
    fclose(fp);
    (void)n;
}

/*
 * noinline: the frame index below is only correct while this stays a real
 * frame. -O2 would otherwise fold it into Memprof_Malloc and every site would
 * be attributed one level too deep.
 */
#if defined(_WIN32)
#include <windows.h>
__attribute__((noinline)) static void *Memprof_CallerPC(void)
{
    /*
     * RtlCaptureStackBackTrace counts its own caller as frame 0:
     *   0 Memprof_CallerPC, 1 Memprof_Malloc, 2 the allocation site.
     */
    void *trace[3] = { 0, 0, 0 };
    USHORT n = RtlCaptureStackBackTrace(2, 3, trace, NULL);

    return (n >= 1) ? trace[0] : (void *)0;
}
#else
__attribute__((noinline)) static void *Memprof_CallerPC(void)
{
    /* Level 0 is this function's return into Memprof_*; level 1 is the site. */
    return __builtin_return_address(1);
}
#endif

void Memprof_Start(void)
{
    const char *enable = getenv("MUGEN_MEMPROF");
    const char *out = getenv("MUGEN_MEMPROF_OUT");

    if (enable == NULL || enable[0] != '1' || memprof_site_enabled) {
        return;
    }
    memprof_dump_path = out != NULL ? out : "mugen.memprof";
    memprof_site_enabled = 1;
    Log_Printf("memprof: call-site accounting on, dump -> %s\n", memprof_dump_path);
}

void Memprof_Stop(const char *path)
{
    if (!memprof_site_enabled) {
        return;
    }
    if (path != NULL && path[0] != '\0') {
        memprof_dump_path = path;
    }
    Memprof_SiteDump();
    memprof_site_enabled = 0;
    Log_Printf("memprof: dump written to %s\n", memprof_dump_path);
}

/* Add site accounting for pc. Acquires the memprof lock. */
static void Memprof_SiteAddLocked(void *pc, int64_t delta_size, int allocating)
{
    MemprofSite *s;

    if (!memprof_site_enabled || pc == NULL) {
        return;
    }
    s = Memprof_FindSiteLocked(pc, allocating);
    if (s == NULL) {
        return;
    }
    if (delta_size >= 0) {
        s->live += (uint64_t)delta_size;
    } else {
        s->live -= (uint64_t)-delta_size;
    }
    if (allocating) {
        s->allocs++;
        if (delta_size > 0 && s->live > s->peak) {
            s->peak = s->live;
        }
    } else {
        s->frees++;
    }
}

static void Memprof_SiteAdd(void *pc, int64_t delta_size, int allocating)
{
    if (!memprof_site_enabled || pc == NULL) {
        return;
    }
    Memprof_Lock();
    Memprof_SiteAddLocked(pc, delta_size, allocating);
    Memprof_Unlock();
}

void *Memprof_Malloc(size_t size)
{
    void *p = malloc(size);
    void *pc = memprof_site_enabled ? Memprof_CallerPC() : NULL;

    if (p == NULL) {
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    if (!Memprof_EntryAdd(p, size, pc)) {
        /* Cannot track it; refuse the allocation rather than miscount. */
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)size, 1);
    Memprof_SiteAdd(pc, (int64_t)size, 1);
    return p;
}

void *Memprof_Calloc(size_t count, size_t size)
{
    size_t total;
    void *p;
    void *pc;

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
    pc = memprof_site_enabled ? Memprof_CallerPC() : NULL;
    if (!Memprof_EntryAdd(p, total, pc)) {
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)total, 1);
    Memprof_SiteAdd(pc, (int64_t)total, 1);
    return p;
}

void *Memprof_Realloc(void *ptr, size_t size)
{
    size_t old_size;
    void *fresh;
    void *pc;
    void *old_pc = NULL;

    if (ptr == NULL) {
        return Memprof_Malloc(size);
    }
    if (!Memprof_EntryRemove(ptr, &old_size, &old_pc)) {
        /* Not ours: pass through without disturbing the counters. */
        if (size == 0) {
            free(ptr);
            return NULL;
        }
        return realloc(ptr, size);
    }
    /*
     * The old entry is gone but the counters have not moved yet: every path
     * below applies exactly one delta, so live never double-counts old_size.
     */
    if (size == 0) {
        free(ptr);
        __sync_fetch_and_add(&memprof_heap_frees, 1);
        Memprof_Add(&memprof_heap_live, &memprof_heap_peak, -(int64_t)old_size, 0);
        Memprof_SiteAdd(old_pc, -(int64_t)old_size, 0);
        return NULL;
    }
    pc = memprof_site_enabled ? Memprof_CallerPC() : NULL;
    fresh = realloc(ptr, size);
    if (fresh == NULL) {
        /* The CRT block is unchanged and still ours; re-track it, unchanged. */
        Memprof_EntryAdd(ptr, old_size, old_pc);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    if (!Memprof_EntryAdd(fresh, size, pc)) {
        free(fresh);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak,
                (int64_t)size - (int64_t)old_size, size > old_size);
    /*
     * Per site the move is two steps: the old site loses the whole old block,
     * the new site gains the whole new one. The sum is the same delta the
     * global counter applies, and no site keeps bytes that moved away from it.
     */
    Memprof_SiteAdd(old_pc, -(int64_t)old_size, 0);
    Memprof_SiteAdd(pc, (int64_t)size, 1);
    return fresh;
}

void Memprof_Free(void *ptr)
{
    size_t size;
    void *pc = NULL;

    if (ptr == NULL) {
        return;
    }
    if (!Memprof_EntryRemove(ptr, &size, &pc)) {
        free(ptr);
        return;
    }
    free(ptr);
    __sync_fetch_and_add(&memprof_heap_frees, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, -(int64_t)size, 0);
    Memprof_SiteAdd(pc, -(int64_t)size, 0);
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
    /* Refresh the site dump about once a second. */
    Memprof_SiteDump();
}
