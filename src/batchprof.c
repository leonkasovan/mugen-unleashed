/*
 * Port addition: batch-rendering profiler (see batchprof.h for what it is for).
 *
 * Not part of the original binary; there is no address to name. It exists
 * because both GL backends submit one quad per draw call, and the per-frame
 * `perf:` line (src/perf.c) reports draw calls and triangles without saying how
 * fragmented that submission is -- 2000 calls of one quad and 20 calls of 100
 * quads draw the same triangles and cost very different amounts.
 *
 * Everything here is compiled only under MUGEN_DEBUG, so a release build pays
 * nothing at all: the header's macros expand to empty statements.
 *
 * The counters are per frame. BatchProf_FrameBegin zeroes the frame and
 * BatchProf_FrameEnd closes it into the second being accumulated, which is the
 * same begin/end pairing src/perf.c uses; the loop in gameloop.c calls them
 * where Perf_BeginFrame and Perf_DrawDone are called, so a sample covers
 * exactly the submission of one frame.
 */
#include "mugen.h"

#ifdef MUGEN_DEBUG

#include "batchprof.h"

/* The frame in progress. */
static double bp_calls;
static double bp_quads;
static double bp_vertices;
static double bp_stream_bytes;
static double bp_programs;
static double bp_textures;
static double bp_cpu_ticks;
static int bp_have_frame;

/* Per-region submission time for the frame in progress and the second being
 * accumulated (BATCHPROF_SPLIT_*). */
static double bp_split[BATCHPROF_SPLIT_COUNT];
static double bp_sum_split[BATCHPROF_SPLIT_COUNT];
/* Last bound state, for counting only the binds that actually changed. */
static unsigned int bp_last_program;
static int bp_program_valid;
static unsigned int bp_last_texture[2];   /* unit 0 (sprite) and unit 1 (palette) */
static int bp_texture_valid[2];

/* The second being accumulated. */
static double bp_sum_calls;
static double bp_sum_quads;
static double bp_sum_vertices;
static double bp_sum_stream_bytes;
static double bp_sum_programs;
static double bp_sum_textures;
static double bp_sum_cpu_ticks;
static int bp_frames;

/* Cached, because it cannot change; 1.0 keeps a broken clock from dividing by
 * zero (the same guard perf.c uses). */
static double BatchProf_Frequency(void)
{
    static double frequency;

    if (frequency <= 0.0) {
        frequency = (double)SDL_GetPerformanceFrequency();
        if (frequency <= 0.0) {
            frequency = 1.0;
        }
    }
    return frequency;
}

unsigned long long BatchProf_Now(void)
{
    return (unsigned long long)SDL_GetPerformanceCounter();
}

void BatchProf_FrameBegin(void)
{
    int i;

    bp_calls = bp_quads = bp_vertices = bp_stream_bytes = 0.0;
    bp_programs = bp_textures = bp_cpu_ticks = 0.0;
    for (i = 0; i < BATCHPROF_SPLIT_COUNT; i++) {
        bp_split[i] = 0.0;
    }
    bp_have_frame = 1;
}

void BatchProf_FrameEnd(void)
{
    int i;

    if (!bp_have_frame) {
        return;         /* no Begin: nothing to close, and a double End is a no-op */
    }
    bp_sum_calls += bp_calls;
    bp_sum_quads += bp_quads;
    bp_sum_vertices += bp_vertices;
    bp_sum_stream_bytes += bp_stream_bytes;
    bp_sum_programs += bp_programs;
    bp_sum_textures += bp_textures;
    bp_sum_cpu_ticks += bp_cpu_ticks;
    for (i = 0; i < BATCHPROF_SPLIT_COUNT; i++) {
        bp_sum_split[i] += bp_split[i];
    }
    bp_frames++;
    bp_have_frame = 0;
}

void BatchProf_Submit(int quads, int vertices, unsigned long bytes, unsigned int triangles,
                      unsigned long long ticks)
{
    (void)triangles;    /* triangles are perf.c's job; kept in the signature so
                         * a call site reads the same in both profilers */
    bp_calls += 1.0;
    if (quads > 0) {
        bp_quads += (double)quads;
    }
    if (vertices > 0) {
        bp_vertices += (double)vertices;
    }
    if (bytes > 0) {
        bp_stream_bytes += (double)bytes;
    }
    bp_cpu_ticks += (double)ticks;
}

void BatchProf_Program(unsigned int program)
{
    if (bp_program_valid && bp_last_program == program) {
        return;         /* re-binding the same program is not a state change */
    }
    bp_program_valid = 1;
    bp_last_program = program;
    bp_programs += 1.0;
}

void BatchProf_Texture(int unit, unsigned int texture)
{
    if (unit < 0 || unit > 1) {
        return;
    }
    if (bp_texture_valid[unit] && bp_last_texture[unit] == texture) {
        return;
    }
    bp_texture_valid[unit] = 1;
    bp_last_texture[unit] = texture;
    bp_textures += 1.0;
}

void BatchProf_Split(int which, unsigned long long ticks)
{
    if (which < 0 || which >= BATCHPROF_SPLIT_COUNT) {
        return;
    }
    bp_split[which] += (double)ticks;
}

int BatchProf_TakeAverages(BatchProfAverages *out)
{
    int have;
    int i;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->frames = bp_frames;
    have = bp_frames > 0;
    if (have) {
        double frequency = BatchProf_Frequency();

        out->calls = bp_sum_calls / bp_frames;
        out->quads = bp_sum_quads / bp_frames;
        out->vertices = bp_sum_vertices / bp_frames;
        out->stream_kib = bp_sum_stream_bytes / bp_frames / 1024.0;
        out->programs = bp_sum_programs / bp_frames;
        out->textures = bp_sum_textures / bp_frames;
        /* ticks are in the platform counter's units, not milliseconds */
        out->cpu_us = bp_sum_cpu_ticks / bp_frames * 1000000.0 / frequency;
        for (i = 0; i < BATCHPROF_SPLIT_COUNT; i++) {
            out->split_us[i] = bp_sum_split[i] / bp_frames * 1000000.0 / frequency;
        }
    }
    bp_sum_calls = bp_sum_quads = bp_sum_vertices = bp_sum_stream_bytes = 0.0;
    bp_sum_programs = bp_sum_textures = bp_sum_cpu_ticks = 0.0;
    for (i = 0; i < BATCHPROF_SPLIT_COUNT; i++) {
        bp_sum_split[i] = 0.0;
    }
    bp_frames = 0;
    return have;
}

#endif /* MUGEN_DEBUG */
