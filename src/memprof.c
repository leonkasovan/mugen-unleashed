/*
 * Port addition: see memprof.h for what this counts and why.
 *
 * MUGEN_MEMPROF_NO_WRAP makes this translation unit exempt from the malloc
 * macros in mugen.h, so the raw CRT calls below stay CRT calls and growing
 * the profiler's own tables cannot re-enter the profiler.
 */
#define MUGEN_MEMPROF_NO_WRAP 1
#include "mugen.h"
#include "memprof.h"

#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/*
 * Side table of allocation sizes, keyed by pointer. A header next to the
 * payload cannot distinguish engine allocations from pointers the CRT gave us
 * before tracking was installed (or pointers passed by accident), so the
 * metadata lives here instead.
 *
 * All counters are 64-bit: the arithmetic must not depend on size_t width.
 */
typedef struct MemprofEntry {
    void *ptr;
    size_t size;
    void *pc;
    unsigned char missing_site; /* attribution failed while profiling */
    struct MemprofEntry *next;
} MemprofEntry;

#define MEMPROF_BUCKETS 1024
static MemprofEntry *memprof_buckets[MEMPROF_BUCKETS];
static volatile int memprof_lock; /* 0 free, 1 held */

static uint64_t memprof_heap_live, memprof_heap_peak;
static uint64_t memprof_heap_allocs, memprof_heap_frees, memprof_heap_fails;
static uint64_t memprof_lua_live, memprof_lua_peak;
static uint64_t memprof_lua_allocs, memprof_lua_frees, memprof_lua_fails;
static uint64_t memprof_tracking_live, memprof_tracking_peak;

static void Memprof_Lock(void)
{
    while (__sync_lock_test_and_set(&memprof_lock, 1)) {
        /* Spin. The critical section is a few instructions. */
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
    MemprofEntry **link = &memprof_buckets[Memprof_Hash(ptr)];

    while (*link != NULL) {
        if ((*link)->ptr == ptr) {
            return link;
        }
        link = &(*link)->next;
    }
    return NULL;
}

/*
 * Detach the entry for `ptr` but keep the node itself. realloc reuses it, so
 * tracking a resized block never needs a metadata allocation and therefore
 * cannot fail -- which is what keeps realloc's "original block stays valid
 * and unchanged on failure" contract intact.
 */
static MemprofEntry *Memprof_EntryDetachLocked(void *ptr)
{
    MemprofEntry **link = Memprof_FindEntryLocked(ptr);
    MemprofEntry *entry;

    if (link == NULL) {
        return NULL;
    }
    entry = *link;
    *link = entry->next;
    entry->next = NULL;
    return entry;
}

static void Memprof_EntryAttachLocked(MemprofEntry *entry)
{
    size_t bucket = Memprof_Hash(entry->ptr);

    entry->next = memprof_buckets[bucket];
    memprof_buckets[bucket] = entry;
}

static void Memprof_TrackingAdd(int64_t delta)
{
    uint64_t live = __sync_add_and_fetch(&memprof_tracking_live, (uint64_t)delta);

    if (delta > 0) {
        for (;;) {
            uint64_t peak = __sync_add_and_fetch(&memprof_tracking_peak, 0);

            if (live <= peak || __sync_bool_compare_and_swap(&memprof_tracking_peak, peak, live)) {
                break;
            }
        }
    }
}

/* Caller holds the lock. */
static MemprofEntry *Memprof_EntryNewLocked(void *ptr, size_t size, void *pc)
{
    MemprofEntry *entry = (MemprofEntry *)malloc(sizeof(MemprofEntry));

    if (entry == NULL) {
        return NULL;
    }
    entry->ptr = ptr;
    entry->size = size;
    entry->pc = pc;
    entry->missing_site = 0;
    Memprof_EntryAttachLocked(entry);
    Memprof_TrackingAdd((int64_t)sizeof(*entry));
    return entry;
}

static void Memprof_EntryFreeLocked(MemprofEntry *entry)
{
    free(entry);
    Memprof_TrackingAdd(-(int64_t)sizeof(*entry));
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

/* ------------------------------------------------------------ call sites */

/*
 * The engine's allocation site, captured from inside the Memprof_* entry
 * points.
 *
 * On ELF this is GCC's level-0 return address, which the compiler documents as
 * always safe. A NONZERO level asks for a frame the compiler may not have kept
 * and can fault in an optimised PIE build; that is why the capture has to be
 * expanded inline here rather than walked one frame up out of a helper.
 *
 * On Windows RtlCaptureStackBackTrace unwinds through the image's unwind data,
 * so it is safe at any optimisation level. Its frame 0 is the function that
 * called it, so from Memprof_* the frames are
 *   0 memprof_win_site_pc, 1 Memprof_*, 2 the engine site.
 */
#if defined(_WIN32)
#  define MEMPROF_SITE_PC() memprof_win_site_pc()
__attribute__((noinline)) static void *memprof_win_site_pc(void)
{
    void *trace[2] = { 0, 0 };
    USHORT n = RtlCaptureStackBackTrace(2, 2, trace, NULL);

    return (n >= 1) ? trace[0] : NULL;
}
#else
#  define MEMPROF_SITE_PC() __builtin_return_address(0)
#endif

/* ------------------------------------------------------- per-site table */

/*
 * Grows on demand and reports what it had to drop, so an incomplete ranking is
 * never silent. Open addressing with linear probing; capacity is a power of
 * two and starts small because most runs see well under a hundred sites.
 */
typedef struct MemprofSite {
    void *pc;
    uint64_t live, peak;
    uint64_t allocs, frees;
    int inuse;
} MemprofSite;

static MemprofSite *memprof_sites;
static size_t memprof_site_capacity;
static size_t memprof_site_count;
static uint64_t memprof_sites_dropped;
static uint64_t memprof_dropped_bytes;
static int memprof_site_enabled;
static const char *memprof_dump_path;
static time_t memprof_last_dump;

/* Caller holds the lock. */
static void Memprof_SiteResetLocked(void)
{
    if (memprof_sites != NULL) {
        size_t bytes = memprof_site_capacity * sizeof(*memprof_sites);
        free(memprof_sites);
        Memprof_TrackingAdd(-(int64_t)bytes);
        memprof_sites = NULL;
    }
    memprof_site_capacity = 0;
    memprof_site_count = 0;
    memprof_sites_dropped = 0;
    memprof_dropped_bytes = 0;
}

/* Caller holds the lock. Returns 0 when the table cannot grow. */
static int Memprof_SiteGrowLocked(void)
{
    size_t old_capacity = memprof_site_capacity;
    MemprofSite *old_sites = memprof_sites;
    size_t new_capacity, new_bytes;
    size_t old_bytes = old_capacity * sizeof(*old_sites);
    MemprofSite *fresh;
    size_t i;

    if (old_capacity > SIZE_MAX / 2) {
        return 0;
    }
    new_capacity = old_capacity == 0 ? 256 : old_capacity * 2;
    if (new_capacity > SIZE_MAX / sizeof(MemprofSite)) {
        return 0;
    }
    new_bytes = new_capacity * sizeof(MemprofSite);
    fresh = (MemprofSite *)calloc(new_capacity, sizeof(MemprofSite));
    if (fresh == NULL) {
        return 0;
    }
    /* Account for both tables during resize, including their peak. */
    Memprof_TrackingAdd((int64_t)new_bytes);
    memprof_sites = fresh;
    memprof_site_capacity = new_capacity;
    for (i = 0; i < old_capacity; i++) {
        MemprofSite *old = &old_sites[i];
        size_t slot;
        if (!old->inuse) {
            continue;
        }
        slot = ((uintptr_t)old->pc >> 2) & (new_capacity - 1);
        while (memprof_sites[slot].inuse) {
            slot = (slot + 1) & (new_capacity - 1);
        }
        memprof_sites[slot] = *old;
    }
    free(old_sites);
    if (old_bytes != 0) {
        Memprof_TrackingAdd(-(int64_t)old_bytes);
    }
    return 1;
}

/*
 * `create` is 0 on the free path: a site that was never recorded on the way in
 * must not be invented on the way out, or its live count would go negative.
 */
static MemprofSite *Memprof_FindSiteLocked(void *pc, int create)
{
    size_t slot, probes;
    MemprofSite *empty = NULL;

    if (memprof_site_capacity == 0) {
        if (!create || !Memprof_SiteGrowLocked()) {
            return NULL;
        }
    }
    /* Existing sites are never contingent on successful table growth. */
    slot = ((uintptr_t)pc >> 2) & (memprof_site_capacity - 1);
    for (probes = 0; probes < memprof_site_capacity; probes++) {
        MemprofSite *site = &memprof_sites[slot];
        if (!site->inuse) {
            empty = site;
            break;
        }
        if (site->pc == pc) {
            return site;
        }
        slot = (slot + 1) & (memprof_site_capacity - 1);
    }
    if (!create) {
        return NULL;
    }
    /* Grow only on a NEW insertion, without O(capacity) counting each time.
     * If growth fails, use the remaining free slot if one exists. */
    if (memprof_site_count >= memprof_site_capacity * 7 / 10) {
        if (Memprof_SiteGrowLocked()) {
            slot = ((uintptr_t)pc >> 2) & (memprof_site_capacity - 1);
            while (memprof_sites[slot].inuse) {
                slot = (slot + 1) & (memprof_site_capacity - 1);
            }
            empty = &memprof_sites[slot];
        }
    }
    if (empty == NULL) {
        return NULL;
    }
    memset(empty, 0, sizeof(*empty));
    empty->pc = pc;
    empty->inuse = 1;
    memprof_site_count++;
    return empty;
}

/* Called while holding memprof_lock. Returns whether the site was credited;
 * the owning MemprofEntry records that result for correct free/realloc. */
static int Memprof_SiteAddLocked(void *pc, int64_t delta_size, int allocating)
{
    MemprofSite *site;
    if (pc == NULL) {
        return 0;
    }
    site = Memprof_FindSiteLocked(pc, allocating);
    if (site == NULL) {
        if (allocating) {
            __sync_fetch_and_add(&memprof_sites_dropped, 1);
        }
        return 0;
    }
    if (delta_size >= 0) {
        site->live += (uint64_t)delta_size;
    } else {
        uint64_t bytes = (uint64_t)-delta_size;
        /* A corrupt entry must not wrap the exported unsigned counter. */
        if (site->live < bytes) {
            site->live = 0;
            __sync_fetch_and_add(&memprof_sites_dropped, 1);
        } else {
            site->live -= bytes;
        }
    }
    if (allocating) {
        site->allocs++;
        if (site->live > site->peak) site->peak = site->live;
    } else {
        site->frees++;
    }
    return 1;
}

/* Current LIVE payload without site attribution; not a cumulative total. */
static void Memprof_UnattributedAddLocked(int64_t delta)
{
    __sync_fetch_and_add(&memprof_dropped_bytes, (uint64_t)delta);
}

/* -------------------------------------------------------------- reporting */

/*
 * Load base the captured PCs must be measured from.
 *
 * PE: the image is relocated per run (DYNAMICBASE), so the symbolizer needs the
 * base of *this* run and adds the file's link-time ImageBase back.
 * ELF: the bias is the difference between runtime and link-time addresses,
 * which is 0 for a non-PIE executable and the load bias for a PIE. Subtracting
 * it is therefore the correct conversion in both cases.
 */
static uintptr_t Memprof_ModuleBase(void)
{
#if defined(_WIN32)
    return (uintptr_t)GetModuleHandleA(NULL);
#elif defined(__linux__)
    /*
     * PIC source flags do not determine whether the executable is PIE.
     * Read e_type from its in-memory ELF header, valid on both ELF32 and
     * ELF64 (offset 16). ET_DYN=3 needs a load bias; ET_EXEC=2 does not.
     * __ehdr_start is also available when the engine is linked as an ELF
     * shared library, as it is on Android.
     */
    extern const unsigned char __ehdr_start[];
    const unsigned char *image = __ehdr_start;
    uint16_t elf_type;

    if (image[0] != 0x7f || image[1] != 'E' ||
        image[2] != 'L' || image[3] != 'F') {
        return 0;
    }
    memcpy(&elf_type, image + 16, sizeof(elf_type));
    return elf_type == 3 ? (uintptr_t)image : 0;
#elif defined(__PIE__)
    /* Non-Linux ELF (e.g. Vita): retain the linker-defined PIE base.
     * Actual Vita relocation still needs an on-device verification. */
    extern char __executable_start;
    return (uintptr_t)(void *)&__executable_start;
#else
    return 0;
#endif
}

static const char *Memprof_AbiName(void)
{
#if defined(_WIN32)
    return "pe";
#else
    return "elf";
#endif
}

/* A full-width load: a plain read of a uint64_t can tear on a 32-bit build. */
static uint64_t Memprof_AtomicRead(const uint64_t *value)
{
    return __sync_add_and_fetch((uint64_t *)value, 0);
}

/*
 * Writes the site table through a temporary file and renames it into place, so
 * a process that dies mid-write cannot destroy the last good snapshot.
 */
static void Memprof_SiteDumpTo(const char *path)
{
    char tmp_path[1024];
    FILE *fp;
    size_t i;
    size_t capacity;
    MemprofSite *snapshot;
    uintptr_t base;

    if (!memprof_site_enabled || path == NULL) {
        return;
    }
    Memprof_Lock();
    capacity = memprof_site_capacity;
    snapshot = NULL;
    if (capacity > 0) {
        snapshot = (MemprofSite *)malloc(capacity * sizeof(*snapshot));
        if (snapshot != NULL) {
            memcpy(snapshot, memprof_sites, capacity * sizeof(*snapshot));
        }
    }
    Memprof_Unlock();
    if (capacity != 0 && snapshot == NULL) {
        return;
    }
    if (strlen(path) + 5 >= sizeof(tmp_path)) {
        free(snapshot);
        return;
    }

    sprintf(tmp_path, "%s.tmp", path);
    fp = fopen(tmp_path, "w");
    if (fp == NULL) {
        free(snapshot);
        return;
    }
    base = Memprof_ModuleBase();
    fprintf(fp, "# mugen memory-by-site dump\n");
    fprintf(fp, "# abi %s\n", Memprof_AbiName());
    fprintf(fp, "# base 0x%llx\n", (unsigned long long)base);
    fprintf(fp, "# format: live_bytes peak_bytes allocs frees 0x<pc>\n");
    for (i = 0; i < capacity; i++) {
        MemprofSite *s = &snapshot[i];

        if (!s->inuse) {
            continue;
        }
        fprintf(fp, "%llu %llu %llu %llu 0x%llx\n",
                (unsigned long long)s->live, (unsigned long long)s->peak,
                (unsigned long long)s->allocs, (unsigned long long)s->frees,
                (unsigned long long)(uintptr_t)s->pc);
    }
    fprintf(fp, "# dropped_sites %llu\n",
            (unsigned long long)Memprof_AtomicRead(&memprof_sites_dropped));
    fprintf(fp, "# unattributed_bytes %llu\n",
            (unsigned long long)Memprof_AtomicRead(&memprof_dropped_bytes));
    free(snapshot);

    if (fflush(fp) != 0) {
        fclose(fp);
        remove(tmp_path);
        return;
    }
    if (fclose(fp) != 0) {
        remove(tmp_path);
        return;
    }
#if defined(_WIN32)
    /* CRT rename cannot replace an existing file on Windows. MoveFileExA
     * replaces the old snapshot without exposing a gap where neither
     * snapshot exists. On failure, preserve the previous valid file. */
    if (!MoveFileExA(tmp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp_path);
    }
#else
    /* Both files are in the same directory: POSIX rename is atomic. */
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
    }
#endif
}

void Memprof_Dump(void)
{
    if (memprof_dump_path != NULL) {
        Memprof_SiteDumpTo(memprof_dump_path);
    }
}

/*
 * Called once per frame. Writes the dump at most once a second, independent of
 * whether the mem: log line is enabled, so a run is never left without a
 * snapshot merely because Verbose is off.
 */
void Memprof_Tick(void)
{
    time_t now;

    if (!memprof_site_enabled) {
        return;
    }
    now = time(NULL);
    if (memprof_last_dump != 0 && now - memprof_last_dump < 1) {
        return;
    }
    memprof_last_dump = now;
    Memprof_Dump();
}

void Memprof_Start(void)
{
    const char *enable = getenv("MUGEN_MEMPROF");
    const char *out = getenv("MUGEN_MEMPROF_OUT");

    if (enable == NULL || enable[0] != '1' || memprof_site_enabled) {
        return;
    }
    memprof_dump_path = (out != NULL && out[0] != '\0') ? out : "mugen.memprof";
    Memprof_Lock();
    Memprof_SiteResetLocked();
    /*
     * The aggregate wrappers run before Memprof_Start (command-line/log
     * setup). Their live blocks have no recorded caller; expose them in the
     * live unattributed total instead of silently losing the difference
     * between site totals and heap_live. The same reset handles a restart.
     */
    {
        size_t bucket;
        for (bucket = 0; bucket < MEMPROF_BUCKETS; bucket++) {
            MemprofEntry *entry;
            for (entry = memprof_buckets[bucket]; entry != NULL; entry = entry->next) {
                entry->pc = NULL;
                entry->missing_site = 1;
                Memprof_UnattributedAddLocked((int64_t)entry->size);
            }
        }
    }
    Memprof_Unlock();
    memprof_last_dump = 0;
    memprof_site_enabled = 1;
    Log_Printf("memprof: call-site accounting on, dump -> %s (%s)\n",
               memprof_dump_path, Memprof_AbiName());
}

void Memprof_Stop(const char *path)
{
    if (!memprof_site_enabled) {
        return;
    }
    if (path != NULL && path[0] != '\0') {
        memprof_dump_path = path;
    }
    Memprof_Dump();
    memprof_site_enabled = 0;
    Log_Printf("memprof: dump written to %s\n", memprof_dump_path);
}

/* ------------------------------------------------------------ allocator */

/* These run with memprof_lock held, so entry metadata and site balances
 * change together. A failed site insertion remains marked on the entry
 * until its free/realloc; unattributed_bytes is LIVE, not cumulative. */
static void Memprof_CreditEntryLocked(MemprofEntry *entry, void *pc)
{
    entry->pc = NULL;
    entry->missing_site = 0;
    if (!memprof_site_enabled) return;
    if (Memprof_SiteAddLocked(pc, (int64_t)entry->size, 1)) {
        entry->pc = pc;
    } else {
        entry->missing_site = 1;
        Memprof_UnattributedAddLocked((int64_t)entry->size);
    }
}

static void Memprof_DebitEntryLocked(const MemprofEntry *entry)
{
    if (entry->pc != NULL) {
        Memprof_SiteAddLocked(entry->pc, -(int64_t)entry->size, 0);
    } else if (entry->missing_site) {
        Memprof_UnattributedAddLocked(-(int64_t)entry->size);
    }
}

/* Receive the caller PC explicitly so realloc(NULL,n) retains the engine
 * realloc call site, not this internal helper's address. */
static void *Memprof_MallocWithPC(size_t size, void *pc)
{
    void *p = malloc(size);
    MemprofEntry *entry;

    if (p == NULL) {
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    Memprof_Lock();
    entry = Memprof_EntryNewLocked(p, size, NULL);
    if (entry != NULL) Memprof_CreditEntryLocked(entry, pc);
    Memprof_Unlock();
    if (entry == NULL) {
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)size, 1);
    return p;
}

void *Memprof_Malloc(size_t size)
{
    /* Especially on Windows: avoid RtlCaptureStackBackTrace outside opt-in
     * profiling, rather than paying for a stack walk on every allocation. */
    void *pc = memprof_site_enabled ? MEMPROF_SITE_PC() : NULL;
    return Memprof_MallocWithPC(size, pc);
}

void *Memprof_Calloc(size_t count, size_t size)
{
    void *pc = memprof_site_enabled ? MEMPROF_SITE_PC() : NULL;
    size_t total;
    void *p;
    MemprofEntry *entry;

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
    Memprof_Lock();
    entry = Memprof_EntryNewLocked(p, total, NULL);
    if (entry != NULL) Memprof_CreditEntryLocked(entry, pc);
    Memprof_Unlock();
    if (entry == NULL) {
        free(p);
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }
    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak, (int64_t)total, 1);
    return p;
}

/*
 * The CRT block and its original metadata survive a failed realloc.
 * Successful realloc reuses the SAME metadata node with no allocations.
 */
void *Memprof_Realloc(void *ptr, size_t size)
{
    void *pc = memprof_site_enabled ? MEMPROF_SITE_PC() : NULL;
    MemprofEntry *entry;
    size_t old_size;
    void *fresh;

    if (ptr == NULL) {
        return Memprof_MallocWithPC(size, pc);
    }
    Memprof_Lock();
    entry = Memprof_EntryDetachLocked(ptr);
    Memprof_Unlock();
    if (entry == NULL) {
        /* CRT allocations not created by the wrappers remain untracked. */
        if (size == 0) {
            free(ptr);
            return NULL;
        }
        return realloc(ptr, size);
    }
    old_size = entry->size;
    if (size == 0) {
        free(ptr);
        Memprof_Lock();
        Memprof_DebitEntryLocked(entry);
        Memprof_EntryFreeLocked(entry);
        Memprof_Unlock();
        __sync_fetch_and_add(&memprof_heap_frees, 1);
        Memprof_Add(&memprof_heap_live, &memprof_heap_peak, -(int64_t)old_size, 0);
        return NULL;
    }

    fresh = realloc(ptr, size);
    if (fresh == NULL) {
        /* The caller still owns ptr, which must remain tracked. */
        Memprof_Lock();
        Memprof_EntryAttachLocked(entry);
        Memprof_Unlock();
        __sync_fetch_and_add(&memprof_heap_fails, 1);
        return NULL;
    }

    Memprof_Lock();
    Memprof_DebitEntryLocked(entry);
    entry->ptr = fresh;
    entry->size = size;
    Memprof_CreditEntryLocked(entry, pc);
    Memprof_EntryAttachLocked(entry);
    Memprof_Unlock();

    __sync_fetch_and_add(&memprof_heap_allocs, 1);
    Memprof_Add(&memprof_heap_live, &memprof_heap_peak,
                (int64_t)size - (int64_t)old_size, size > old_size);
    return fresh;
}

void Memprof_Free(void *ptr)
{
    MemprofEntry *entry;
    size_t size;

    if (ptr == NULL) return;
    Memprof_Lock();
    entry = Memprof_EntryDetachLocked(ptr);
    if (entry != NULL) {
        size = entry->size;
        Memprof_DebitEntryLocked(entry);
        Memprof_EntryFreeLocked(entry);
    }
    Memprof_Unlock();
    if (entry == NULL) {
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
    size_t tracked = 0;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->heap_live = Memprof_AtomicRead(&memprof_heap_live);
    out->heap_peak = Memprof_AtomicRead(&memprof_heap_peak);
    out->heap_allocs = Memprof_AtomicRead(&memprof_heap_allocs);
    out->heap_frees = Memprof_AtomicRead(&memprof_heap_frees);
    out->heap_fails = Memprof_AtomicRead(&memprof_heap_fails);
    out->lua_live = Memprof_AtomicRead(&memprof_lua_live);
    out->lua_peak = Memprof_AtomicRead(&memprof_lua_peak);
    out->lua_allocs = Memprof_AtomicRead(&memprof_lua_allocs);
    out->lua_frees = Memprof_AtomicRead(&memprof_lua_frees);
    out->lua_fails = Memprof_AtomicRead(&memprof_lua_fails);
    out->sites_dropped = Memprof_AtomicRead(&memprof_sites_dropped);
    out->unattributed_bytes = Memprof_AtomicRead(&memprof_dropped_bytes);
    out->tracking_overhead_bytes = Memprof_AtomicRead(&memprof_tracking_live);

    Memprof_Lock();
    tracked = memprof_site_count;
    Memprof_Unlock();
    out->sites_tracked = tracked;
}

void Memprof_Log(const char *tag)
{
    MemprofStats s;

    Memprof_Stats(&s);
    /*
     * %llu on uint64_t: `unsigned long` is 32 bits on 64-bit Windows, which
     * truncated large totals in the previous version of this line.
     */
    Log_Printf("mem: %s heap_live=%llu heap_peak=%llu heap_allocs=%llu heap_frees=%llu heap_fails=%llu "
               "lua_live=%llu lua_peak=%llu lua_allocs=%llu lua_frees=%llu lua_fails=%llu "
               "sites=%llu sites_dropped=%llu unattributed=%llu tracking=%llu\n",
               tag != NULL ? tag : "",
               (unsigned long long)s.heap_live, (unsigned long long)s.heap_peak,
               (unsigned long long)s.heap_allocs, (unsigned long long)s.heap_frees,
               (unsigned long long)s.heap_fails,
               (unsigned long long)s.lua_live, (unsigned long long)s.lua_peak,
               (unsigned long long)s.lua_allocs, (unsigned long long)s.lua_frees,
               (unsigned long long)s.lua_fails,
               (unsigned long long)s.sites_tracked, (unsigned long long)s.sites_dropped,
               (unsigned long long)s.unattributed_bytes,
               (unsigned long long)s.tracking_overhead_bytes);
}
