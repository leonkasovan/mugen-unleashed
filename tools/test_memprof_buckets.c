/*
 * Host microbenchmark for the Memprof pointer side table (src/memprof.c).
 *
 * Why: an on-device profile of the HyperDBZ pack showed Memprof_FindEntryLocked
 * at 11.7% of all samples, and the run reported ~99,000 live allocations. The
 * side table has a fixed MEMPROF_BUCKETS of 1024 and hashes on pointer bits, so
 * ~99k live pointers over 1024 buckets means an average chain of ~97 entries
 * and every malloc/free walks it under a spin lock.
 *
 * This measures that directly and deterministically, with no driver, no vsync
 * and no frame-skip noise: allocate N pointers, then time lookups. If the cost
 * is chain length, raising the bucket count must show it.
 *
 * Build/run (from the repository root):
 *   sh tools/test_memprof_buckets.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The engine's only external from memprof.c. */
void Log_Printf(const char *fmt, ...) { (void)fmt; }

#include "../src/memprof.h"

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int live = (argc > 1) ? atoi(argv[1]) : 99000;   /* live pointers */
    int churn = (argc > 2) ? atoi(argv[2]) : 200000; /* alloc/free pairs */
    void **ptrs;
    double t0, t1;
    int i;
    MemprofStats st;

    ptrs = (void **)calloc((size_t)live, sizeof(*ptrs));
    if (ptrs == NULL) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }

    /* 1. Fill: hold `live` allocations so the table has realistic occupancy. */
    t0 = now_seconds();
    for (i = 0; i < live; i++) {
        ptrs[i] = Memprof_Malloc((size_t)(16 + (i % 96)));
        if (ptrs[i] == NULL) {
            fprintf(stderr, "allocation %d failed\n", i);
            return 2;
        }
    }
    t1 = now_seconds();
    printf("fill      %6d allocs  %8.2f ms  (%6.2f us each)\n",
           live, (t1 - t0) * 1e3, (t1 - t0) * 1e6 / live);

    /* heap_live is a BYTE count; the allocation count is allocs - frees. */
    Memprof_Stats(&st);
    printf("live      %llu allocations, %llu bytes\n",
           (unsigned long long)(st.heap_allocs - st.heap_frees),
           (unsigned long long)st.heap_live);

    /*
     * 2. Churn: alloc+free at full occupancy. Every free walks the chain for
     *    its own pointer, and every insert walks it too -- this is the work a
     *    frame's short-lived allocations actually pay.
     */
    t0 = now_seconds();
    for (i = 0; i < churn; i++) {
        void *p = Memprof_Malloc((size_t)(24 + (i % 64)));
        if (p == NULL) {
            fprintf(stderr, "churn allocation failed at %d\n", i);
            return 2;
        }
        Memprof_Free(p);
    }
    t1 = now_seconds();
    printf("churn     %6d pairs   %8.2f ms  (%6.2f us per pair)  <== metric\n",
           churn, (t1 - t0) * 1e3, (t1 - t0) * 1e6 / churn);

    /* 3. Free the live set. */
    t0 = now_seconds();
    for (i = 0; i < live; i++) {
        Memprof_Free(ptrs[i]);
    }
    t1 = now_seconds();
    printf("drain     %6d frees   %8.2f ms  (%6.2f us each)\n",
           live, (t1 - t0) * 1e3, (t1 - t0) * 1e6 / live);

    Memprof_Stats(&st);
    if (st.heap_live != 0 || st.heap_allocs != st.heap_frees) {
        printf("WARNING: %llu bytes / %llu allocations still live after drain\n",
               (unsigned long long)st.heap_live,
               (unsigned long long)(st.heap_allocs - st.heap_frees));
        return 1;
    }
    printf("heap_leak ok (0 live)\n");

    free(ptrs);
    return 0;
}
