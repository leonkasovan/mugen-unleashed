/*
 * Frame-rate logging for the PS Vita port.
 *
 * Why this exists
 * ---------------
 * The engine can show a frame counter (Game_DrawFps, toggled by the console
 * "fps" command), but reaching it needs the console, and the console needs a
 * backquote key. A Vita has no keyboard, so on this target the frame rate was
 * effectively unmeasurable -- and frame rate is the whole question when the
 * software renderer is doing the work on the ARM core.
 *
 * So the rate is written to mugen.log instead, where it can be read with
 * VitaShell or pulled off the memory card, and where a slow run leaves a record
 * of *when* it was slow rather than a number that has to be caught on screen.
 *
 * What it reports
 * ---------------
 * Three numbers, because they answer different questions:
 *
 *   drawn   frames passed to Screen_Present. The engine drops present when it
 *           is more than a frame late (Frame_UpdateSkip sets skip_draw), so
 *           this can be lower than the tick rate -- that is the engine
 *           reacting to being slow, not the port being wrong.
 *
 *   tick    calls to Screen_EndFrame. The game loop's own rate, i.e. how many
 *           times per second game logic advanced.
 *
 *   fps     Timer_GetFps(), the engine's own measurement. It is sampled every
 *           g_timerFpsInterval+1 frames, so it is read here rather than
 *           computed separately, and it is the number Game_DrawFps would show.
 *
 * The window is long (10 s by default) because a short one is dominated by
 * start-up, loading, and the menu transitions that are not representative of a
 * match.
 */
#include "mugen.h"

/* Seconds between reports. Long enough to survive loading hitches. */
#define VITA_FPS_WINDOW_SEC 10

/* Reports are only useful early; do not let a long session fill the log. */
#define VITA_FPS_MAX_REPORTS 120

static int g_vitaFpsTicks;
static int g_vitaFpsDrawn;
static int g_vitaFpsReports;
static int g_vitaFpsWarmed;      /* skip the first window: it includes loading */
static __int64 g_vitaFpsStart;   /* Timer_GetTicks() when the window opened */

/* Per-frame cost accounting (QueryPerformanceCounter ticks, 100 ns each). */
static __int64 g_vitaPerfScale;
static __int64 g_vitaPerfFlip;

/*
 * Called once per frame end. Cheap: two increments and a tick comparison, and
 * Timer_Count() only when the window has elapsed.
 */
void Vita_PerfAddScale(__int64 ticks)
{
    g_vitaPerfScale += ticks;
}

void Vita_PerfAddFlip(__int64 ticks)
{
    g_vitaPerfFlip += ticks;
}

void Vita_FrameLog(void)
{
    __int64 now;
    __int64 span;

    g_vitaFpsTicks++;
    if (g_game != NULL && g_game->skip_draw == 0) {
        g_vitaFpsDrawn++;
    }
    if (g_vitaFpsReports >= VITA_FPS_MAX_REPORTS) {
        return;
    }

    /*
     * Timer_GetTicks() counts 1/g_timerTicksPerSecond seconds (1/100) since
     * Timer_Init, which is a stable public clock. Timer_Count() itself is
     * static to src/timer.c.
     */
    now = Timer_GetTicks();
    if (g_vitaFpsStart == 0) {
        g_vitaFpsStart = now;
        return;
    }
    span = (__int64)VITA_FPS_WINDOW_SEC * g_timerTicksPerSecond;
    if (now - g_vitaFpsStart < span) {
        return;
    }

    {
        float fps = Timer_GetFps();
        int ticks = g_vitaFpsTicks;
        int drawn = g_vitaFpsDrawn;

        /*
         * The first window covers start-up and the first screen's loading, so
         * it is reported but labelled: seeing "warmup" at 12 fps is expected,
         * seeing it at 55 fps is not.
         */
        Log_Printf("fps: %.1f  (tick %d/s, drawn %d/s%s)\n", (double)fps, ticks / VITA_FPS_WINDOW_SEC,
                   drawn / VITA_FPS_WINDOW_SEC, g_vitaFpsWarmed ? "" : ", warmup window");
        /*
         * Where the frame time goes. Both numbers are averages over the window,
         * in milliseconds per drawn frame -- the scale copy and the driver's
         * present. If "scale" dominates, optimise the copy; if "flip" dominates,
         * the cost is the GPU-mapped surface write and no CPU work will help.
         */
        if (drawn > 0) {
            Log_Printf("  per frame: scale %.1f ms, flip %.1f ms\n",
                       (double)g_vitaPerfScale / 10000.0 / (double)drawn,
                       (double)g_vitaPerfFlip / 10000.0 / (double)drawn);
        }
        g_vitaPerfScale = 0;
        g_vitaPerfFlip = 0;
        g_vitaFpsWarmed = 1;
    }

    g_vitaFpsReports++;
    g_vitaFpsTicks = 0;
    g_vitaFpsDrawn = 0;
    g_vitaFpsStart = now;
}
