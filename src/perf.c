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

void Perf_BeginFrame(void)
{
    perf_begin = SDL_GetPerformanceCounter();
    perf_marks = 0;
    perf_calls = 0.0;
    perf_triangles = 0.0;
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
        perf_present_ms = Perf_Since(perf_mark, SDL_GetPerformanceCounter());
        perf_marks |= PERF_MARK_PRESENT;
    }
    if (perf_marks == PERF_MARK_ALL) {
        perf_sum_logic += perf_logic_ms;
        perf_sum_draw += perf_draw_ms;
        perf_sum_wait += perf_wait_ms;
        perf_sum_present += perf_present_ms;
        perf_sum_calls += perf_calls;
        perf_sum_triangles += perf_triangles;
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
    }
    perf_sum_logic = perf_sum_draw = perf_sum_wait = perf_sum_present = 0.0;
    perf_sum_calls = perf_sum_triangles = 0.0;
    perf_frames = 0;
    return have;
}
