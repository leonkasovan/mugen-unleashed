#ifndef MUGEN_PROFILER_H
#define MUGEN_PROFILER_H

/*
 * Port addition: a sampling CPU profiler, after the design of Go's
 * runtime/pprof CPU profile.
 *
 * Why it exists: batchprof accounts for 4.71 ms of a 7.4 ms 2D draw span
 * (up 0.68, st 0.19, buf 0.87, dr 2.75, px 0.10, pal 0.12) and cannot say where
 * the remaining 2.69 ms goes -- the quad building, the CPU model-view maths, the
 * background and HUD drawing all sit inside that span but outside every bucket.
 *
 * Go's profiler answers exactly this shape of question, and three of its choices
 * are what make it cheap enough to leave running:
 *
 *   1. it SAMPLES (100 Hz here, via ITIMER_PROF/SIGPROF) instead of
 *      instrumenting, so nothing changes codegen and library code is covered;
 *   2. it attributes by STACK, so a sample says who called the hot code, not
 *      just that some function was hot;
 *   3. it does the expensive part OFF THE TARGET: the handler only appends
 *      program counters to a fixed table, and symbolization happens later on a
 *      workstation with addr2line -- the same split as `go tool pprof`.
 *
 * Off by default; MUGEN_PROF=1 enables it and MUGEN_PROF_OUT names the dump
 * (default "mugen.prof"). tools/prof_symbolize.py turns that dump into a
 * self/inclusive ranking. See docs/BUILD-LINUX.md.
 */

/* Arms the timer when MUGEN_PROF=1. Safe to call twice; a no-op otherwise. */
void Prof_Start(void);

/* Writes the collected samples and disarms the timer. Safe when never started. */
void Prof_Stop(const char *path);

/* Number of samples collected so far (0 when the profiler never ran). */
unsigned long Prof_Samples(void);

#endif /* MUGEN_PROFILER_H */
