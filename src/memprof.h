/*
 * Port addition: allocation counters for the engine heap and the Lua heap.
 *
 * The original binary has no memory accounting at all; nothing in the tree
 * reports bytes allocated, resident or peak. The one existing signal is
 * g_outOfMemory, set by Mem_Calloc on failure. On this engine, the big
 * allocators are the C runtime (the 681 malloc/calloc/realloc/free sites in
 * src/) and Lua's internal arena (newstate with the default allocator).
 * Everything else (GL, mixer) is outside the CRT heap and is not counted.
 *
 * Two independent counters are kept:
 *
 *   heap   every allocation routed through the macros in mugen.h (which
 *          redirect malloc/calloc/realloc/free in every engine source file,
 *          including third_party files that include mugen.h) plus anything
 *          that calls the Memprof_* functions directly;
 *   lua    Lua's allocator, replaced in Lua_Init by Lua_MemCounterAlloc.
 *
 * The CRT call sizeof(MemprofHeader) is paid per allocation: the free path
 * must know the payload size, and neither MinGW's nor newlib's private
 * header is stable to read. The size stored is the payload, matching what
 * Lua reports in OSize, so the two counters are comparable.
 *
 * Thread safety: the counters use the GCC atomic builtins, which exist on
 * every compiler this tree builds with (MinGW gcc, Linux gcc, Vita newlib
 * gcc, Android clang). The peak update is a CAS loop so it never under-reports.
 *
 * Not counted: allocations made by the CRT itself (e.g. the block behind
 * strdup) are later freed by Memprof_Free with no header, and fall through
 * to the system free() without touching the counters, so nothing breaks
 * and nothing is miscounted.
 */
#ifndef MUGEN_MEMPROF_H
#define MUGEN_MEMPROF_H

void *Memprof_Malloc(size_t size);
void *Memprof_Calloc(size_t count, size_t size);
void *Memprof_Realloc(void *ptr, size_t size);
void Memprof_Free(void *ptr);

/* lua_Alloc compatible: nsize == 0 frees, osize is the previous payload. */
void *Memprof_LuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize);

typedef struct MemprofStats {
    size_t heap_live, heap_peak;
    unsigned long heap_allocs, heap_frees, heap_fails;
    size_t lua_live, lua_peak;
    unsigned long lua_allocs, lua_frees, lua_fails;
} MemprofStats;

/* Snapshots the two counters into `out` (atomically per counter, not as a
 * single consistent instant -- the engine keeps no global heap lock). */
void Memprof_Stats(MemprofStats *out);

/* Writes one "mem:" line to the log for each heap plus a combined line. */
void Memprof_Log(const char *tag);

#endif /* MUGEN_MEMPROF_H */
