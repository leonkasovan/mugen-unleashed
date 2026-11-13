/*
 * Batch-rendering profiler gate (MUGEN_DEBUG only).
 *
 * src/batchprof.c is a release no-op and a debug-only counter set, so nothing in
 * the normal suite touches it: the GLES harnesses do not compile it and the
 * engine tests run without MUGEN_DEBUG. That is how its whole class of bug --
 * a counter that reports the wrong thing and is believed -- would reach a device
 * unchallenged. This test asserts the arithmetic and the de-duplication
 * directly, with the platform counter stubbed so the timings are exact.
 *
 * What it pins:
 *   - one Submit is one draw call, whatever the quad/vertex/byte counts are;
 *   - the per-frame counters start clean on FrameBegin and one begin/end pair
 *     is exactly one sample, so the averages divide by the frame count;
 *   - Program/Texture count only *changes*, which is the number that says what
 *     a batch would amortize: re-binding the same program is not a state change;
 *   - texture de-duplication is per unit, so the palette binding on unit 1 is
 *     counted even when the sprite texture on unit 0 is unchanged;
 *   - TakeAverages converts ticks to microseconds, and starts a new second (a
 *     second call with no frames reports nothing);
 *   - an End with no Begin, and a repeated End, are both ignored.
 */
#include <stdio.h>
#include <string.h>

#include "batchprof.h"

/* The SDL counter the profiler reads. Fixed so a tick is exactly 1 microsecond:
 * 1 GHz means ticks/1000 == microseconds, which keeps the expected values
 * readable instead of scaled. */
static unsigned long long fake_counter;

unsigned long long SDL_GetPerformanceCounter(void)
{
    return fake_counter;
}

unsigned long long SDL_GetPerformanceFrequency(void)
{
    return 1000000000ULL;       /* 1 GHz */
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static int near(double got, double want)
{
    double d = got - want;

    if (d < 0) {
        d = -d;
    }
    return d < 1e-9;
}

/* One frame: a batch of `quads` quads submitted in one call, `ticks` of CPU. */
static void one_frame(int quads, unsigned long long ticks)
{
    BatchProf_FrameBegin();
    BatchProf_Submit(quads, quads * 4, (unsigned long)(quads * 4 * 8 * sizeof(float)), quads * 2, ticks);
    BatchProf_FrameEnd();
}

static void test_counts(void)
{
    BatchProfAverages avg;

    /* Nothing accumulated yet: the first FrameBegin only opens a frame. */
    check(!BatchProf_TakeAverages(&avg), "no frames yet reports nothing");

    /* Ten frames of 100 quads each, 250 us of CPU per frame. */
    fake_counter = 0;
    {
        int i;

        for (i = 0; i < 10; i++) {
            one_frame(100, 250000ULL);
        }
    }
    check(BatchProf_TakeAverages(&avg), "ten frames report an average");
    check(avg.frames == 10, "one begin/end pair is one sample");
    check(near(avg.calls, 1.0), "one Submit is one draw call");
    check(near(avg.quads, 100.0), "quads per frame");
    check(near(avg.vertices, 400.0), "vertices per frame");
    check(near(avg.stream_kib, 100.0 * 4 * 8 * 4 / 1024.0), "streamed KiB per frame");
    check(near(avg.cpu_us, 250.0), "ticks converted to microseconds");

    /* The batching ratio the log line prints: 100 quads in 1 call. */
    check(near(avg.quads / avg.calls, 100.0), "quads per call is the batch size");

    /* A second read with no frames since reports nothing and does not repeat
     * the previous second. */
    check(!BatchProf_TakeAverages(&avg), "a second with no frames reports nothing");
}

static void test_state_changes(void)
{
    BatchProfAverages avg;

    /* Fresh: the first bind of each program is a change, repeats are not. */
    BatchProf_FrameBegin();
    BatchProf_Program(7);
    BatchProf_Program(7);
    BatchProf_Program(7);
    BatchProf_Program(9);
    BatchProf_Program(7);

    BatchProf_Texture(0, 11);
    BatchProf_Texture(0, 11);
    BatchProf_Texture(0, 12);
    BatchProf_Texture(1, 50);
    BatchProf_Texture(1, 50);
    /* unit 1 changing while unit 0 does not must still be counted */
    BatchProf_Texture(1, 51);
    /* an out-of-range unit is ignored rather than indexing the array */
    BatchProf_Texture(2, 99);
    BatchProf_FrameEnd();

    check(BatchProf_TakeAverages(&avg), "state-change frame reported");
    check(near(avg.programs, 3.0), "program binds counted on change only (7, 9, 7)");
    check(near(avg.textures, 4.0), "texture binds counted per unit (11, 12, 50, 51)");
}

static void test_zero_call_ratio(void)
{
    BatchProfAverages avg;

    /* A frame that submitted nothing must not divide by zero in the log line;
     * the caller guards it, and this asserts the value it guards on. */
    BatchProf_FrameBegin();
    BatchProf_FrameEnd();
    check(BatchProf_TakeAverages(&avg), "an idle frame is still a frame");
    check(near(avg.calls, 0.0), "no calls in an idle frame");
    check(near(avg.quads, 0.0), "no quads in an idle frame");
}

static void test_end_without_begin(void)
{
    BatchProfAverages avg;

    /* A End with no Begin must not invent a sample, and must not carry the
     * previous frame's counters into the next second. */
    BatchProf_FrameEnd();
    BatchProf_FrameEnd();
    check(!BatchProf_TakeAverages(&avg), "End without Begin counts no frame");

    /* And a frame that is opened but never closed contributes nothing. */
    BatchProf_FrameBegin();
    BatchProf_Submit(5, 20, 640, 10, 0);
    check(!BatchProf_TakeAverages(&avg), "an unclosed frame is not counted");
    BatchProf_FrameEnd();
    check(BatchProf_TakeAverages(&avg), "closing it makes it count");
    check(near(avg.quads, 5.0), "and the counters survived until the close");
}

int main(void)
{
    test_counts();
    test_state_changes();
    test_zero_call_ratio();
    test_end_without_begin();

    if (failures != 0) {
        printf("test_batchprof: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_batchprof: OK\n");
    return 0;
}
