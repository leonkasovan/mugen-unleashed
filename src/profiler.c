/*
 * Port addition: sampling CPU profiler (profiler.h). Not part of the original
 * binary; there is no address to name.
 *
 * The handler does the least possible work: unwind, hash the stack, bump a
 * counter. Everything expensive -- symbolization, aggregation, ranking -- happens
 * on the host afterwards, which is Go's split and the reason this is cheap enough
 * to leave on during a real run.
 *
 * Two constraints shaped the code:
 *
 *  - the signal handler may not allocate. glibc's backtrace() uses the unwinder
 *    and may allocate on first use, so Prof_Start() calls it once up front
 *    (warm-up) and the handler only uses the pre-heated path;
 *  - the table is fixed-size and never grows, so the handler cannot fail.
 *
 * ITIMER_PROF is used rather than ITIMER_REAL: it ticks on CPU time (user +
 * system), so a thread blocked in a swap contributes no samples -- which is the
 * behaviour we want, since the open question is CPU work.
 */
#include "mugen.h"

#include <signal.h>
#include <sys/time.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profiler.h"

#define PROF_MAX_DEPTH 24
#define PROF_SLOTS 4096
#define PROF_HZ 100

typedef struct ProfSlot {
    unsigned long count;
    int depth;
    void *pc[PROF_MAX_DEPTH];
} ProfSlot;

static ProfSlot g_prof[PROF_SLOTS];
static unsigned long g_prof_samples;
static volatile int g_prof_active;
static void *g_prof_buffer[PROF_MAX_DEPTH];

/* FNV-1a over the stack, so equal stacks land in the same slot. */
static unsigned long prof_hash(void *const *pc, int depth)
{
    unsigned long h = 2166136261u;
    int i;

    for (i = 0; i < depth; i++) {
        unsigned long v = (unsigned long)(size_t)pc[i];

        h ^= v;
        h *= 16777619u;
    }
    h ^= (unsigned long)depth;
    return h;
}

static void prof_handler(int sig)
{
    int depth;
    unsigned long h;
    int seen = 0;
    int i;

    (void)sig;
    if (!g_prof_active) {
        return;
    }
    depth = backtrace(g_prof_buffer, PROF_MAX_DEPTH);
    /*
     * Drop this handler's own frames: glibc reports [0] = prof_handler,
     * [1] = the signal trampoline, [2] = the interrupted program counter. Without
     * this every sample's leaf is the handler itself and "self" time is 100%
     * profiler, which is what the first run reported.
     */
    if (depth > 2) {
        memmove(g_prof_buffer, g_prof_buffer + 2, (size_t)(depth - 2) * sizeof(void *));
        depth -= 2;
    } else {
        return;
    }
    h = prof_hash(g_prof_buffer, depth);
    for (i = 0; i < PROF_SLOTS; i++) {
        ProfSlot *slot = &g_prof[(h + (unsigned long)i) % PROF_SLOTS];

        if (slot->count == 0) {
            int k;

            /* Claim it. Two threads racing here at worst lose one sample. */
            slot->depth = depth;
            for (k = 0; k < depth; k++) {
                slot->pc[k] = g_prof_buffer[k];
            }
            slot->count = 1;
            seen = 1;
            g_prof_samples++;
            return;
        }
        if (slot->depth == depth && memcmp(slot->pc, g_prof_buffer, (size_t)depth * sizeof(void *)) == 0) {
            slot->count++;
            seen = 1;
            g_prof_samples++;
            return;
        }
    }
    if (!seen) {
        /* Table full: drop the sample rather than allocate in a handler. */
        return;
    }
}

void Prof_Start(void)
{
    const char *enable = getenv("MUGEN_PROF");
    struct sigaction sa;
    struct itimerval timer;
    void *warm[PROF_MAX_DEPTH];

    if (enable == NULL || enable[0] != '1' || g_prof_active) {
        return;
    }
    /* Warm the unwinder up outside the handler, so no allocation happens there. */
    (void)backtrace(warm, PROF_MAX_DEPTH);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = prof_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0) {
        Log_Printf("prof: cannot install the SIGPROF handler; profiler off\n");
        return;
    }
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_usec = 1000000 / PROF_HZ;
    timer.it_value = timer.it_interval;
    if (setitimer(ITIMER_PROF, &timer, NULL) != 0) {
        Log_Printf("prof: cannot arm ITIMER_PROF; profiler off\n");
        return;
    }
    g_prof_active = 1;
    Log_Printf("prof: sampling at %d Hz into %d slots (ITIMER_PROF, CPU time)\n", PROF_HZ, PROF_SLOTS);
}

unsigned long Prof_Samples(void)
{
    return g_prof_samples;
}

void Prof_Stop(const char *path)
{
    struct itimerval timer;
    struct sigaction sa;
    FILE *fp;
    int i;

    if (!g_prof_active) {
        return;
    }
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_PROF, &timer, NULL);
    g_prof_active = 0;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);

    if (path == NULL || path[0] == '\0') {
        path = "mugen.prof";
    }
    fp = fopen(path, "w");
    if (fp == NULL) {
        Log_Printf("prof: cannot write %s\n", path);
        return;
    }
    /*
     * Plain text, one stack per line: "<count> <pc> <pc> ...". Deliberately not
     * a binary format -- the analysis runs on the host with addr2line, and a
     * text dump can be read, diffed and re-analysed without this code.
     */
    fprintf(fp, "# mugen sampling profile: %d Hz, %lu samples\n", PROF_HZ, g_prof_samples);
    fprintf(fp, "# format: count pc1 pc2 ... (leaf first)\n");
    for (i = 0; i < PROF_SLOTS; i++) {
        int k;

        if (g_prof[i].count == 0) {
            continue;
        }
        fprintf(fp, "%lu", g_prof[i].count);
        for (k = 0; k < g_prof[i].depth; k++) {
            fprintf(fp, " %llx", (unsigned long long)(size_t)g_prof[i].pc[k]);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    Log_Printf("prof: %lu samples written to %s\n", g_prof_samples, path);
}
