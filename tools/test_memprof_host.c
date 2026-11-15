/*
 * Standalone Linux regression harness for src/memprof.c. It compiles the
 * actual implementation while stubbing the engine's large shared header.
 * Run with tools/test_memprof_host.sh from a Linux checkout.
 */
#define MUGEN_H 1
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <limits.h>

static void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

/* Deterministic fault injection for the 256 -> 512 site-table expansion. */
static int memprof_test_deny_growth;
static void *memprof_test_calloc(size_t count, size_t size)
{
    if (memprof_test_deny_growth && count >= 512 && size >= 32) {
        return NULL;
    }
    return calloc(count, size);
}
#define calloc memprof_test_calloc
#include "../src/memprof.c"
#undef calloc

__attribute__((noinline, used)) static void memprof_test_marker(void)
{
    volatile int x = 1;
    (void)x;
}

static void check(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

int main(void)
{
    unsigned char *p, *q;
    MemprofEntry *record;
    MemprofStats stats;
    FILE *fp;
    char line[128];
    size_t i;
    volatile size_t impossible = SIZE_MAX;

    check(setenv("MUGEN_MEMPROF", "1", 1) == 0, "enable profiler");
    check(setenv("MUGEN_MEMPROF_OUT", "memprof-test-output.txt", 1) == 0,
          "set report path");
    Memprof_Start();

    p = (unsigned char *)Memprof_Malloc(32);
    check(p != NULL, "initial allocation");
    memset(p, 0x5a, 32);
    Memprof_Lock();
    record = *Memprof_FindEntryLocked(p);
    Memprof_Unlock();
    check(record != NULL, "tracking entry");

    q = (unsigned char *)Memprof_Realloc(p, 96);
    check(q != NULL, "grow realloc");
    for (i = 0; i < 32; i++) check(q[i] == 0x5a, "preserved payload");
    Memprof_Lock();
    check(*Memprof_FindEntryLocked(q) == record, "reuse metadata on realloc");
    Memprof_Unlock();

    p = (unsigned char *)Memprof_Realloc(q, impossible);
    check(p == NULL, "realloc failure");
    Memprof_Lock();
    check(*Memprof_FindEntryLocked(q) == record, "failure keeps same metadata");
    Memprof_Unlock();
    Memprof_Free(q);

    Memprof_Lock();
    Memprof_SiteResetLocked();
    /* Hundreds of artificial PCs test table growth, and no linear scan of
     * the whole table is necessary when an existing site is looked up. */
    for (i = 0; i < 400; i++) {
        void *pc = (void *)(uintptr_t)(0x10000 + i * 16);
        check(Memprof_SiteAddLocked(pc, 1, 1), "insert growing table");
    }
    check(memprof_site_capacity >= 512 && memprof_site_count == 400,
          "site table expanded");
    Memprof_SiteResetLocked();
    memprof_test_deny_growth = 1;
    for (i = 0; i < 256; i++) {
        void *pc = (void *)(uintptr_t)(0x10000 + i * 16);
        check(Memprof_SiteAddLocked(pc, 100, 1), "insert without growth");
    }
    check(memprof_site_capacity == 256, "growth failure injection applied");
    check(Memprof_SiteAddLocked((void *)(uintptr_t)0x10000, 20, 1),
          "existing site survives growth failure");
    check(Memprof_FindSiteLocked((void *)(uintptr_t)0x10000, 0)->live == 120,
          "existing site has correct total");
    Memprof_Unlock();

    p = (unsigned char *)Memprof_Malloc(64);
    check(p != NULL, "allocation when attribution table is full");
    Memprof_Stats(&stats);
    check(stats.unattributed_bytes == 64, "missing attribution is tracked");
    check(stats.sites_dropped > 0, "dropped site reported");
    q = (unsigned char *)Memprof_Realloc(p, 128);
    check(q != NULL, "realloc of unattributed allocation");
    Memprof_Stats(&stats);
    check(stats.unattributed_bytes == 128, "missing attribution follows realloc");
    Memprof_Free(q);
    Memprof_Stats(&stats);
    check(stats.unattributed_bytes == 0, "free clears missing live bytes");
    check(stats.tracking_overhead_bytes >= 256 * sizeof(MemprofSite),
          "site table is counted as profiler overhead");
    memprof_test_deny_growth = 0;

    /* Dump must work with Verbose absent, and overwrite an existing report. */
    Memprof_Tick();
    fp = fopen("memprof-test-output.txt", "r");
    check(fp != NULL, "periodic dump exists");
    check(fgets(line, sizeof(line), fp) != NULL && strstr(line, "mugen memory"),
          "dump header");
    fclose(fp);
    Memprof_Dump();
    fp = fopen("memprof-test-output.txt", "r");
    check(fp != NULL, "replaced dump exists");
    fclose(fp);

    memprof_test_marker();
    printf("symbol_addr=0x%" PRIxPTR "\n",
           (uintptr_t)(void *)memprof_test_marker - Memprof_ModuleBase());
    Memprof_Stop(NULL);
    remove("memprof-test-output.txt");
    remove("memprof-test-output.txt.tmp");
    puts("PASS: memprof allocations, site growth, failures and dump");
    return 0;
}
