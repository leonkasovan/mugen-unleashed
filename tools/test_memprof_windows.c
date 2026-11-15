/*
 * Native MinGW/Windows smoke test for the actual src/memprof.c.
 * Build/run from .github/workflows/build.yml; no SDL dependencies.
 */
#define MUGEN_H 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

static void Log_Printf(const char *fmt, ...) { (void)fmt; }
#include "../src/memprof.c"

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

int main(void)
{
    MemprofStats a, b;
    void *p, *q;
    FILE *fp;
    char line[128];

    /* Ordinary builds must not require an opt-in environment variable. */
    Memprof_Stats(&a);
    p = Memprof_Malloc(32);
    check(p != NULL, "unprofiled malloc");
    Memprof_Free(p);
    check(!memprof_site_enabled, "unprofiled path stays off");

    check(_putenv("MUGEN_MEMPROF=1") == 0, "enable profiling");
    check(_putenv("MUGEN_MEMPROF_OUT=win-memprof-test.txt") == 0,
          "set dump path");
    Memprof_Start();
    p = Memprof_Calloc(1, 64);
    check(p != NULL, "profiled calloc");
    q = Memprof_Realloc(p, 128);
    check(q != NULL, "profiled realloc");
    Memprof_Stats(&a);
    check(a.heap_live >= 128 && a.sites_tracked > 0,
          "live bytes and call sites");
    Memprof_Dump();
    Memprof_Dump(); /* Must replace the existing file atomically. */
    fp = fopen("win-memprof-test.txt", "r");
    check(fp != NULL, "second dump preserved report");
    check(fgets(line, sizeof(line), fp) && strstr(line, "memory-by-site"),
          "snapshot header");
    fclose(fp);
    Memprof_Free(q);
    Memprof_Stats(&b);
    check(b.heap_live < a.heap_live, "free reduces tracked bytes");
    Memprof_Stop(NULL);
    remove("win-memprof-test.txt");
    puts("PASS: Windows memprof alloc/realloc and snapshot replacement");
    return 0;
}
