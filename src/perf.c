/*
 * Port addition: per-frame phase timing and draw counting (perf.h).
 *
 * Not part of the original binary; there is no address to name. The original
 * engine has one clock, 1/100 s, which is enough to count frames and useless for
 * saying where a 16 ms frame went. Everything here is opt-in by the caller:
 * a mark costs one counter read and two subtractions, Perf_CountDraw one add,
 * and the averaging only happens when a log line is actually written.
 */
#include "mugen.h"
#include "perf.h"

#ifdef ANDROID_PORT
/*
 * Port fix: the default Android build deliberately includes no GL loader
 * (mugen.h's ANDROID_PORT branch), but Perf_TriangleCount still compares
 * primitive modes. The NDK's GLES2 header supplies those enums and adds no
 * link dependency; with MUGEN_GLES on, glad_es.h has already defined them.
 */
#ifndef MUGEN_GLES
#include <GLES2/gl2.h>
#endif
#endif

/* A frame contributes a sample only when it reached all four marks. A frame
 * that broke out early (screen change, skipped draw) would otherwise report a
 * fast `draw` on exactly the slow frames. */
#define PERF_MARK_LOGIC   0x01
#define PERF_MARK_DRAW    0x02
#define PERF_MARK_WAIT    0x04
#define PERF_MARK_PRESENT 0x08
#define PERF_MARK_ALL     0x0F

static Uint64 perf_begin;
static Uint64 perf_mark;
static int perf_marks;

/* The second being accumulated. */
static double perf_sum_logic;
static double perf_sum_draw;
static double perf_sum_wait;
static double perf_sum_present;
static double perf_sum_calls;
static double perf_sum_triangles;
static int perf_frames;

/* The frame in progress. */
static double perf_logic_ms;
static double perf_draw_ms;
static double perf_wait_ms;
static double perf_present_ms;
static double perf_calls;
static double perf_triangles;
static unsigned int perf_last_calls;
#ifdef MUGEN_VITA_GL
static double perf_vita_frame[PERF_VITA_PHASE_COUNT];
static double perf_vita_sum[PERF_VITA_PHASE_COUNT];
static double perf_vita_max[PERF_VITA_PHASE_COUNT];
#endif


/* SDL's counter is the only clock here fine enough to separate the phases;
 * the engine's 1/100 s timer is not. Cached, because it cannot change. */
static double Perf_Frequency(void)
{
    static double frequency;

    if (frequency <= 0.0) {
        frequency = (double)SDL_GetPerformanceFrequency();
        if (frequency <= 0.0) {
            frequency = 1.0;        /* a broken clock must not divide by zero */
        }
    }
    return frequency;
}

/* Milliseconds between two counter readings. */
static double Perf_Since(Uint64 then, Uint64 now)
{
    return (double)(now - then) * 1000.0 / Perf_Frequency();
}

/*
 * Opt-in wall-clock capture for frame pacing. Keep I/O outside the frame loop
 * and bound the memory used even when the game is left running.
 */
#define FRAME_TRACE_CAPACITY 18000

typedef struct PerfTraceFrame {
    Uint64 start, present_end, next_start;
    double logic_ms, draw_ms, wait_ms, present_ms, budget_ms;
    Uint64 swap_ticks;
    unsigned swap_calls;
    int screen, drawn, late, next_skip, resync;
} PerfTraceFrame;

static struct {
    const char *path;
    PerfTraceFrame *frames, *previous;
    size_t count;
    Uint64 present_end, swap_ticks;
    unsigned swap_calls;
    int initialized, full;
} frame_trace;

static void Perf_WriteFrameTrace(void)
{
    FILE *out = fopen(frame_trace.path, "w");
    size_t i;

    if (out == NULL) {
        fprintf(stderr, "Cannot open frame trace %s\n", frame_trace.path);
    } else {
        fputs("frame,screen,drawn,logic_ms,draw_ms,wait_ms,present_ms,gl_swap_ms,gl_swaps,"
              "period_ms,after_present_ms,budget_ms,late_frames,next_skip,resync\n", out);
        for (i = 0; i < frame_trace.count; i++) {
            const PerfTraceFrame *f = &frame_trace.frames[i];

            fprintf(out, "%u,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%u,",
                    (unsigned)i + 1, f->screen, f->drawn, f->logic_ms, f->draw_ms, f->wait_ms,
                    f->present_ms, Perf_Since(0, f->swap_ticks), f->swap_calls);
            if (f->next_start != 0) {
                fprintf(out, "%.4f,%.4f", Perf_Since(f->start, f->next_start),
                        Perf_Since(f->present_end, f->next_start));
            } else {
                fputc(',', out);
            }
            fprintf(out, ",%.4f,%d,%d,%d\n", f->budget_ms, f->late, f->next_skip, f->resync);
        }
        {
            int failed = ferror(out);

            if (fclose(out) != 0 || failed) {
                fprintf(stderr, "Could not finish writing frame trace %s\n", frame_trace.path);
            }
        }
    }
    if (frame_trace.full) {
        fprintf(stderr, "Frame trace limited to the first %d complete frames\n", FRAME_TRACE_CAPACITY);
    }
    SDL_free(frame_trace.frames);
}

static void Perf_InitFrameTrace(void)
{
    frame_trace.initialized = 1;
    frame_trace.path = getenv("MUGEN_FRAME_TRACE");
    if (frame_trace.path == NULL || frame_trace.path[0] == '\0') return;

    frame_trace.frames = SDL_calloc(FRAME_TRACE_CAPACITY, sizeof(*frame_trace.frames));
    if (frame_trace.frames == NULL || atexit(Perf_WriteFrameTrace) != 0) {
        fprintf(stderr, "Cannot initialize frame trace %s\n", frame_trace.path);
        SDL_free(frame_trace.frames);
        frame_trace.frames = NULL;
    }
}

unsigned long long Perf_TraceSwapBegin(void)
{
    return frame_trace.frames != NULL && !frame_trace.full ? SDL_GetPerformanceCounter() : 0;
}

void Perf_TraceSwapEnd(unsigned long long start)
{
    if (start != 0) {
        frame_trace.swap_ticks += SDL_GetPerformanceCounter() - start;
        frame_trace.swap_calls++;
    }
}

void Perf_TraceSkip(int drawn, int late, int next_skip, int resync)
{
    PerfTraceFrame *f;

    if (frame_trace.frames == NULL || frame_trace.present_end == 0) return;
    if (frame_trace.count == FRAME_TRACE_CAPACITY) {
        frame_trace.full = 1;
        return;
    }
    f = &frame_trace.frames[frame_trace.count++];
    f->start = perf_begin;
    f->present_end = frame_trace.present_end;
    f->logic_ms = perf_logic_ms;
    f->draw_ms = perf_draw_ms;
    f->wait_ms = perf_wait_ms;
    f->present_ms = perf_present_ms;
    f->budget_ms = 1000.0 * (double)g_timerFrameTicks / (double)g_timerFrequency;
    f->swap_ticks = frame_trace.swap_ticks;
    f->swap_calls = frame_trace.swap_calls;
    f->screen = g_game->screen_kind;
    f->drawn = drawn;
    f->late = late;
    f->next_skip = next_skip;
    f->resync = resync;
    frame_trace.previous = f;
    frame_trace.present_end = 0;
}

void Perf_VitaSpan(int phase, unsigned long long start)
{
#ifdef MUGEN_VITA_GL
    /* Only time normal frame drawing; startup shaders, menus before the loop
     * and the end-of-frame log itself are deliberately excluded. */
    if ((unsigned)phase < PERF_VITA_PHASE_COUNT &&
        (perf_marks & PERF_MARK_LOGIC) && !(perf_marks & PERF_MARK_PRESENT)) {
        perf_vita_frame[phase] += Perf_Since((Uint64)start, SDL_GetPerformanceCounter());
    }
#else
    (void)phase; (void)start;
#endif
}

unsigned long long Perf_UsOf(unsigned long long ticks)
{
    return (unsigned long long)((double)ticks * 1000000.0 / Perf_Frequency());
}

#ifdef MUGEN_VITA_GL
/*
 * Port addition (Esaka stall diagnosis): the worst present of the second.
 *
 * `present` in the perf line is the whole of Screen_Present, but BIND, BLIT,
 * RESTORE and SWAP cover only parts of Gl_Present, and the pre-swap callback
 * and the two clears had no timer at all. A stalled frame therefore reported
 * present=1043ms with every measured part near 0.2ms. A stall is a property of
 * ONE frame, so a per-second mean cannot show it; these record the single worst
 * present and its full breakdown, which separates time inside Gl_Present from
 * time in the rest of Screen_Present.
 */
static PerfVitaWorst perf_vita_worst;
static struct {
    unsigned long long clear_us, pre_us, post_us, total_us;
} perf_vita_current;

void Perf_VitaPresentParts(unsigned long long clear_us, unsigned long long pre_us,
                           unsigned long long post_us, unsigned long long total_us)
{
    perf_vita_current.clear_us = clear_us;
    perf_vita_current.pre_us = pre_us;
    perf_vita_current.post_us = post_us;
    perf_vita_current.total_us = total_us;
}

void Perf_VitaWorstTake(PerfVitaWorst *out)
{
    if (out == NULL) {
        return;
    }
    *out = perf_vita_worst;
    memset(&perf_vita_worst, 0, sizeof(perf_vita_worst));
}
#endif

void Perf_BeginFrame(void)
{
    Uint64 now;

    if (!frame_trace.initialized) Perf_InitFrameTrace();
    now = SDL_GetPerformanceCounter();
    if (frame_trace.frames != NULL && !frame_trace.full) {
        /* Link only adjacent loop iterations on the same kind of screen. */
        if (frame_trace.previous != NULL && frame_trace.previous->screen == g_game->screen_kind) {
            frame_trace.previous->next_start = now;
        }
        frame_trace.previous = NULL;
        frame_trace.present_end = 0;
        frame_trace.swap_ticks = 0;
        frame_trace.swap_calls = 0;
    }
    perf_begin = now;
    perf_marks = 0;
    perf_calls = 0.0;
    perf_triangles = 0.0;
#ifdef MUGEN_VITA_GL
    memset(perf_vita_frame, 0, sizeof(perf_vita_frame));
#endif
}

void Perf_LogicDone(void)
{
    perf_mark = SDL_GetPerformanceCounter();
    perf_marks |= PERF_MARK_LOGIC;
}

void Perf_DrawDone(void)
{
    Uint64 now;

    if ((perf_marks & PERF_MARK_LOGIC) == 0) {
        return;
    }
    now = SDL_GetPerformanceCounter();
    perf_logic_ms = Perf_Since(perf_begin, perf_mark);
    perf_draw_ms = Perf_Since(perf_mark, now);
    perf_marks |= PERF_MARK_DRAW;
    perf_mark = now;
}

void Perf_WaitDone(void)
{
    Uint64 now;

    if ((perf_marks & PERF_MARK_DRAW) == 0) {
        return;
    }
    now = SDL_GetPerformanceCounter();
    perf_wait_ms = Perf_Since(perf_mark, now);
    perf_marks |= PERF_MARK_WAIT;
    perf_mark = now;
}

void Perf_PresentDone(void)
{
    if ((perf_marks & PERF_MARK_WAIT) != 0) {
        Uint64 now = SDL_GetPerformanceCounter();

        perf_present_ms = Perf_Since(perf_mark, now);
        perf_marks |= PERF_MARK_PRESENT;
        if (perf_marks == PERF_MARK_ALL && frame_trace.frames != NULL && !frame_trace.full) {
            frame_trace.present_end = now;
        }
    }
    if (perf_marks == PERF_MARK_ALL) {
        perf_last_calls = (unsigned int)perf_calls;
        perf_sum_logic += perf_logic_ms;
        perf_sum_draw += perf_draw_ms;
        perf_sum_wait += perf_wait_ms;
        perf_sum_present += perf_present_ms;
        perf_sum_calls += perf_calls;
        perf_sum_triangles += perf_triangles;
#ifdef MUGEN_VITA_GL
        {
            int i;
            for (i = 0; i < PERF_VITA_PHASE_COUNT; ++i) {
                perf_vita_sum[i] += perf_vita_frame[i];
                if (perf_vita_frame[i] > perf_vita_max[i]) {
                    perf_vita_max[i] = perf_vita_frame[i];
                }
            }
        }
        /* Keep the worst present of the second with its full breakdown. A
         * zero total means Gl_Present returned before presenting, so it is not
         * a candidate. */
        if (perf_vita_current.total_us > 0 &&
            perf_vita_current.total_us >= perf_vita_worst.total_us) {
            perf_vita_worst.frames++;
            perf_vita_worst.game_frames =
                g_game != NULL ? (unsigned long long)g_game->frames : 0;
            perf_vita_worst.present_ms = perf_present_ms;
            perf_vita_worst.total_us = perf_vita_current.total_us;
            perf_vita_worst.clear_us = perf_vita_current.clear_us;
            perf_vita_worst.pre_us = perf_vita_current.pre_us;
            perf_vita_worst.post_us = perf_vita_current.post_us;
            perf_vita_worst.bind_us = (unsigned long long)(perf_vita_frame[PERF_VITA_PRESENT_BIND] * 1000.0);
            perf_vita_worst.blit_us = (unsigned long long)(perf_vita_frame[PERF_VITA_PRESENT_BLIT] * 1000.0);
            perf_vita_worst.restore_us = (unsigned long long)(perf_vita_frame[PERF_VITA_PRESENT_RESTORE] * 1000.0);
            perf_vita_worst.swap_us = (unsigned long long)(perf_vita_frame[PERF_VITA_SWAP] * 1000.0);
        }
        perf_vita_current.clear_us = perf_vita_current.pre_us = 0;
        perf_vita_current.post_us = perf_vita_current.total_us = 0;
#endif
        perf_frames++;
    }
    perf_marks = 0;
}

void Perf_CountDraw(unsigned int triangles)
{
    perf_calls += 1.0;
    if (triangles > 0) {
        perf_triangles += (double)triangles;
    }
}

unsigned int Perf_LastDrawCount(void)
{
    return perf_last_calls;
}

/* Port fix: index_count/3 only describes independent triangles. */
unsigned int Perf_TriangleCount(unsigned int mode, unsigned int count)
{
    switch (mode) {
    case GL_TRIANGLES:
        return count / 3u;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return count >= 3u ? count - 2u : 0u;
    default:
        return 0u;
    }
}

int Perf_TakeAverages(PerfAverages *out)
{
    int have;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->frames = perf_frames;
    have = perf_frames > 0;
    if (have) {
        out->logic_ms = perf_sum_logic / perf_frames;
        out->draw_ms = perf_sum_draw / perf_frames;
        out->wait_ms = perf_sum_wait / perf_frames;
        out->present_ms = perf_sum_present / perf_frames;
        out->calls = perf_sum_calls / perf_frames;
        out->triangles = perf_sum_triangles / perf_frames;
#ifdef MUGEN_VITA_GL
        {
            int i;
            for (i = 0; i < PERF_VITA_PHASE_COUNT; ++i) {
                out->vita_ms[i] = perf_vita_sum[i] / perf_frames;
                out->vita_max_ms[i] = perf_vita_max[i];
            }
        }
#endif
    }
    perf_sum_logic = perf_sum_draw = perf_sum_wait = perf_sum_present = 0.0;
    perf_sum_calls = perf_sum_triangles = 0.0;
#ifdef MUGEN_VITA_GL
    memset(perf_vita_sum, 0, sizeof(perf_vita_sum));
    memset(perf_vita_max, 0, sizeof(perf_vita_max));
#endif
    perf_frames = 0;
    return have;
}
