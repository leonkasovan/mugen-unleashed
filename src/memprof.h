/*
 * Port addition: a tracked-engine-heap profiler.
 *
 * This is NOT process-memory accounting. It reports what the engine and Lua
 * asked the C runtime for, and nothing else. Resident working set also
 * includes resident code, shared pages, heap metadata, thread stacks and the
 * graphics/audio drivers, none of which are visible here. Do not subtract
 * these figures from a working-set measurement and call the remainder "other".
 *
 * Two independent counters are kept:
 *
 *   heap   allocations routed through the macros in mugen.h (every source
 *          file in src/ includes that header) plus direct Memprof_* calls;
 *   lua    Lua's arena, via lua_newstate(Memprof_LuaAlloc, ...) in Lua_Init.
 *
 * Known coverage gaps, deliberate and documented rather than hidden:
 *   - vendored libraries that do not include mugen.h (libresample, minizip)
 *     allocate through the bare CRT and are not counted;
 *   - libpng IS counted, through its explicit allocator hooks;
 *   - allocations the CRT makes internally (strdup and friends) never pass
 *     through the wrappers.
 *
 * Thread safety: the live/peak/allocation counters are updated with GCC
 * atomic builtins and read back with an atomic load, so a 32-bit build cannot
 * observe a torn 64-bit value. The per-site table has its own spin lock.
 */
#ifndef MUGEN_MEMPROF_H
#define MUGEN_MEMPROF_H

#include <stdint.h>

void *Memprof_Malloc(size_t size);
void *Memprof_Calloc(size_t count, size_t size);
void *Memprof_Realloc(void *ptr, size_t size);
void Memprof_Free(void *ptr);

/* lua_Alloc compatible: nsize == 0 frees, osize is the previous payload. */
void *Memprof_LuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize);

/*
 * Full-width on every target: `unsigned long` is 32 bits on 64-bit Windows,
 * which silently truncated large allocation counts.
 */
typedef struct MemprofStats {
    uint64_t heap_live, heap_peak;
    uint64_t heap_allocs, heap_frees, heap_fails;
    uint64_t lua_live, lua_peak;
    uint64_t lua_allocs, lua_frees, lua_fails;
    /* sites_dropped: failed attribution attempts since Memprof_Start.
     * unattributed_bytes: CURRENT live payload without a recorded site.
     * tracking_overhead_bytes: live pointer-record and site-table heap,
     * excluding static buckets and transient report snapshots. */
    uint64_t sites_tracked, sites_dropped, unattributed_bytes;
    uint64_t tracking_overhead_bytes;
} MemprofStats;

/* Snapshots the counters with atomic loads (each counter individually
 * consistent; not a single instant, since the engine keeps no global lock). */
void Memprof_Stats(MemprofStats *out);

/* Writes one "mem:" line to the log. */
void Memprof_Log(const char *tag);

/*
 * Writes the site dump if the reporting interval has elapsed, independent of
 * whether the mem: line is enabled. Call once per frame; it does nothing
 * between intervals and nothing at all when MUGEN_MEMPROF is unset.
 */
void Memprof_Tick(void);

/*
 * Per-call-site counters, collected when MUGEN_MEMPROF=1. Each engine
 * allocation records its caller PC; the table grows as needed and reports
 * how many sites it had to drop if it ever runs out. Off by default.
 */
void Memprof_Start(void);

/*
 * Writes the dump now (default path "mugen.memprof"). Also called by
 * Memprof_Stop at exit; a clean exit is not required, because the dump is
 * rewritten periodically during the run.
 */
void Memprof_Dump(void);

void Memprof_Stop(const char *path);

#endif /* MUGEN_MEMPROF_H */
