#ifndef MUGEN_PERF_H
#define MUGEN_PERF_H

/*
 * Port addition: a per-frame time breakdown, so a log can say which phase owns
 * the frame instead of leaving it to inference.
 *
 * The engine's own clock (Timer_* ) counts 1/100 s, which cannot resolve a
 * 16 ms frame's parts. This module reads the platform's high-resolution counter
 * (SDL2, so the same code serves the desktop, GLES and Vita builds) at four
 * points in a frame:
 *
 *     begin -> logic -> draw -> wait -> present -> next begin
 *
 *   logic    input, screen update, tick and the state machines
 *   draw     the sprite/model submission for the frame
 *   wait     the frame limiter in Screen_EndFrame
 *   present  the platform flip/upload, scaling blit and swap/vsync wait
 *
 * A frame capped by vsync shows it in `present`; limiter sleep is in `wait`.
 * Draw calls and triangles are counted alongside: expensive submission with
 * 20 calls and with 2000 calls mean very different things.
 */

/* Vita-only low-frequency CPU timing: one sample per major render phase.
 * These are blocking-call wall times, NOT GPU execution timestamps. */
enum {
    PERF_VITA_GAME_DRAW,
    PERF_VITA_STAGE_BACK,
    PERF_VITA_STAGE_FRONT,
    PERF_VITA_STAGE_BIND,
    PERF_VITA_STAGE_CLEAR,
    PERF_VITA_STAGE_MODEL,
    PERF_VITA_STAGE_RESTORE,
    PERF_VITA_STAGE_COMPOSITE,
    PERF_VITA_STAGE_DIRECT,
    PERF_VITA_PRESENT_BIND,
    PERF_VITA_PRESENT_BLIT,
    PERF_VITA_PRESENT_RESTORE,
    PERF_VITA_SWAP,
    /* Port addition (Esaka stall diagnosis): the three parts of Gl_Present that
     * had no timer. `present` in the perf line is the whole of Screen_Present,
     * and BIND/BLIT/RESTORE/SWAP account for only part of it; a stalled frame
     * could therefore report present=1043ms with every measured part at 0.2ms.
     * These close that gap without changing what is drawn. */
    PERF_VITA_PRESENT_CLEAR,        /* the glClear before the screen-buffer quad */
    PERF_VITA_PRESENT_PREPRESENT,   /* g_gfxPrePresent: PAUSED box, debug, console */
    PERF_VITA_PRESENT_POSTCLEAR,    /* the glClear after the swap */
    PERF_VITA_PHASE_COUNT
};

/* The start value comes from SDL_GetPerformanceCounter(). No-op on non-Vita. */
void Perf_VitaSpan(int phase, unsigned long long start);

typedef struct PerfAverages {
    double logic_ms;    /* mean per frame over the last whole second */
    double draw_ms;
    double wait_ms;
    double present_ms;
    double calls;       /* mean draw calls per frame */
    double triangles;   /* mean triangles *submitted* per frame: multi-pass and
                         * layered drawing submit the same geometry more than
                         * once, which is what the GPU is asked to do, and that
                         * is the number that explains a slow draw */
    double vita_ms[PERF_VITA_PHASE_COUNT]; /* CPU milliseconds per frame */
    double vita_max_ms[PERF_VITA_PHASE_COUNT]; /* worst single-frame phase */
    int frames;         /* frames the means cover; 0 when there is nothing */
} PerfAverages;

void Perf_BeginFrame(void);
void Perf_LogicDone(void);
void Perf_DrawDone(void);
void Perf_WaitDone(void);
void Perf_PresentDone(void);

/* MUGEN_FRAME_TRACE=<csv> captures complete frames and skip decisions.
 * Swap hooks time the synchronous GLES swap call, a subset of presentation. */
unsigned long long Perf_TraceSwapBegin(void);
void Perf_TraceSwapEnd(unsigned long long start);
void Perf_TraceSkip(int drawn, int late, int next_skip, int resync);

/* One GL draw: n triangles, counted for the frame in progress. */
void Perf_CountDraw(unsigned int triangles);
/* Draw submissions in the last completed frame, for the ShowFPS overlay. */
unsigned int Perf_LastDrawCount(void);

/* Port fix: assembled triangles for a GL primitive mode and element count.
 * Lines/points contribute draw calls but no triangles. Degenerate triangles
 * are included: this measures submitted work, not visible pixels. */
#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
unsigned int Perf_TriangleCount(unsigned int mode, unsigned int count);
#endif

/* Means over the last whole second, and starts a new one. 0 when the samples
 * are incomplete or too few to be worth printing. */
int Perf_TakeAverages(PerfAverages *out);

/* Port addition: present-phase microsecond breakdown of the worst frame in the
 * current second, and how many times a present was observed. A stall is a
 * property of ONE frame; a per-second mean hides it, which is how "present=
 * 1043ms with blit=0.2ms" stayed unexplained. Reading these at the same point
 * as Perf_TakeAverages reports the stall rather than the average.
 *
 * Fields are microseconds so a sub-millisecond part is not rounded to 0.0.
 * `slowest_*` describe the single worst present of the second. */
typedef struct PerfVitaWorst {
    unsigned long long frames;      /* >0 once any present was measured; counts
                                     * how often the worst was replaced */
    unsigned long long game_frames; /* engine frame number of the worst */
    double present_ms;              /* whole Screen_Present for that frame */
    unsigned long long total_us;    /* whole Gl_Present for that frame */
    unsigned long long bind_us;
    unsigned long long clear_us;    /* the glClear before the screen-buffer quad */
    unsigned long long blit_us;     /* the screen-buffer quad itself */
    unsigned long long restore_us;
    unsigned long long pre_us;      /* g_gfxPrePresent */
    unsigned long long swap_us;
    unsigned long long post_us;     /* the glClear after the swap */
} PerfVitaWorst;

void Perf_VitaWorstTake(PerfVitaWorst *out);
/* Records one Gl_Present's parts; called from the present path. */
void Perf_VitaPresentParts(unsigned long long clear_us, unsigned long long pre_us,
                           unsigned long long post_us, unsigned long long total_us);
/* Counter ticks to microseconds, for callers outside this module. */
unsigned long long Perf_UsOf(unsigned long long ticks);
#endif /* MUGEN_PERF_H */
