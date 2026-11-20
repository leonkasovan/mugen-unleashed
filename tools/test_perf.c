/* Deterministic profiler regression: production frame boundary and counters.
 * SDL/GPU presentation and the clock are injected; no device timing claim. */
#include "mugen.h"
#include <assert.h>
#include <limits.h>

#undef main
#if TEST_ANDROID
/* Keep host declarations, then select the production Android frame boundary. */
#define ANDROID_PORT 1
#if TEST_ANDROID == 2
#define MUGEN_GLES 1
#endif
#include "android_platform.h"
#endif

static Uint64 clock_ticks;
static Uint64 Test_Counter(void) { return clock_ticks; }
static Uint64 Test_Frequency(void) { return 1000; }
#define SDL_GetPerformanceCounter Test_Counter
#define SDL_GetPerformanceFrequency Test_Frequency
#include "../src/perf.c"
#include "../src/frame.c"
/* Standalone frame test does not link the optional memory-profiler module. */
void Memprof_Tick(void) {}
void SoundCache_Tick(void) {}

static int swaps, uploads, desktop_presents, fail_upload;
static GameVars game;
static int timer_late;

int Timer_WaitFrame(void) { clock_ticks += 3; return 1; }
int64_t Timer_GetFrame(void) { return g_timerFrame + timer_late; }
void Timer_ResetFrame(void) {}
void Sdl2_PresentFrame(void) { desktop_presents++; clock_ticks += 7; }
#if TEST_ANDROID
int Android_GLPresent(void) { swaps++; clock_ticks += 7; return 0; }
int Android_PresentFrame(void)
{
    uploads++; clock_ticks += 7;
    if (fail_upload) {
        SDL_SetError("injected upload failure (expected by test)");
        return -1;
    }
    return 0;
}
int Android_GLForceContextLoss(void) { assert(0 && "unexpected loss rehearsal"); return 0; }
#endif
SDL_Surface *Gfx_GetScreenSurface(void) { assert(0); return NULL; }
unsigned int Video_MapRGB(int r, int g, int b) { assert(0); return 0; }
void Font_DrawText(const char *s, Font *f, int x, int y, int a, int bank, int r, int g, int b) { assert(0); }
int Png_WriteSurface(SDL_Surface *s, FILE *f) { assert(0); return 0; }
void Log_Printf(const char *fmt, ...) { assert(0); }
void MatchLog_Printf(int level, const char *fmt, ...) { assert(0); }
void Gfx_SoftBlendRect(int x1, int y1, int x2, int y2, unsigned int c, int a, int b) { assert(0); }
static void present(SpriteSet *s, int sprite, float x, float y, float w, float h) {}

static void check_frame(int mode, int skipped, int speedup, int failure)
{
    PerfAverages a;
    Uint64 started = clock_ticks;
    int expected_present = skipped ? 0 : 7;
    int expected_wait = speedup ? 0 : 3;

    memset(&game, 0, sizeof(game));
    g_game = &game;
    game.skip_draw = skipped;
    game.speedup = speedup;
    game.frame_skip = -1;
    g_renderMode = mode;
    g_gfxPresent = present;
    swaps = uploads = desktop_presents = 0;
    fail_upload = failure;
    Perf_BeginFrame(); clock_ticks += 1;
    Perf_LogicDone(); clock_ticks += 2;
    Perf_DrawDone();
    Screen_EndFrame();
    assert(Perf_TakeAverages(&a));
    assert(a.frames == 1 && a.logic_ms == 1 && a.draw_ms == 2);
    assert(a.wait_ms == expected_wait && a.present_ms == expected_present);
    assert(a.logic_ms + a.draw_ms + a.wait_ms + a.present_ms == (double)(clock_ticks - started));
#if TEST_ANDROID == 2
    assert(swaps == (!skipped && mode != RENDER_SYSTEM));
    assert(uploads == (!skipped && mode == RENDER_SYSTEM));
    assert(desktop_presents == 0);
#elif TEST_ANDROID == 1
    assert(uploads == !skipped && swaps == 0 && desktop_presents == 0);
#else
    assert(desktop_presents == !skipped && uploads == 0 && swaps == 0);
#endif
    assert(!Perf_TakeAverages(&a)); /* consumption resets the sample window */
}

#if TEST_ANDROID != 1
static void check_primitives(void)
{
    static const struct { unsigned int mode, count, triangles; } cases[] = {
        {GL_TRIANGLES, 6, 2}, {GL_TRIANGLE_STRIP, 6, 4}, {GL_TRIANGLE_FAN, 6, 4},
        {GL_POINTS, 6, 0}, {GL_LINES, 6, 0}, {GL_LINE_STRIP, 6, 0}, {GL_LINE_LOOP, 6, 0},
        {GL_TRIANGLES, 2, 0}, {GL_TRIANGLE_STRIP, 0, 0}, {GL_TRIANGLE_FAN, 1, 0},
        {GL_TRIANGLE_STRIP, 2, 0}, {GL_TRIANGLE_FAN, 2, 0},
        {GL_TRIANGLE_STRIP, UINT_MAX, UINT_MAX - 2u}
    };
    PerfAverages a;
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        unsigned int triangles = Perf_TriangleCount(cases[i].mode, cases[i].count);
        assert(triangles == cases[i].triangles);
        Perf_BeginFrame(); Perf_LogicDone();
        Perf_CountDraw(triangles);
        Perf_DrawDone(); Perf_WaitDone(); Perf_PresentDone();
        assert(Perf_TakeAverages(&a));
        assert(a.calls == 1 && a.triangles == (double)cases[i].triangles);
    }
    Perf_BeginFrame(); Perf_LogicDone(); Perf_CountDraw(2);
    Perf_BeginFrame(); /* abandon an incomplete frame */
    Perf_LogicDone(); Perf_DrawDone(); Perf_WaitDone(); Perf_PresentDone();
    assert(Perf_TakeAverages(&a) && a.frames == 1 && a.calls == 0 && a.triangles == 0);
}
#endif

static void check_trace(void)
{
    PerfTraceFrame fixture[2] = {{0}};
    int uses_fixture = frame_trace.frames == NULL;
    size_t first = frame_trace.count;
    Uint64 start, swap;
    PerfTraceFrame *drawn, *skipped;

    if (uses_fixture) frame_trace.frames = fixture;
    memset(&game, 0, sizeof(game));
    game.frame_skip = -1;
    game.screen_kind = 4;
    g_timerFrequency = 1000;
    g_timerFrameTicks = 17;

    start = clock_ticks;
    Perf_BeginFrame();
    clock_ticks += 2;
    Perf_LogicDone();
    clock_ticks += 3;
    Perf_DrawDone();
    clock_ticks += 1;
    Perf_WaitDone();
    swap = Perf_TraceSwapBegin();
    clock_ticks += 7;
    Perf_TraceSwapEnd(swap);
    clock_ticks += 1;
    Perf_PresentDone();
    timer_late = 2;
    Frame_UpdateSkip();
    drawn = &frame_trace.frames[first];
    assert(frame_trace.count == first + 1 && drawn->screen == 4 && drawn->drawn);
    assert(drawn->late == 2 && drawn->next_skip && !drawn->resync);
    assert(drawn->present_ms == 8 && drawn->swap_ticks == 7 && drawn->swap_calls == 1);
    assert(drawn->budget_ms == 17 && drawn->present_end == start + 14);

    clock_ticks += 4;
    Perf_BeginFrame();
    assert(drawn->next_start == start + 18);
    Perf_LogicDone();
    Perf_DrawDone();
    Perf_WaitDone();
    Perf_PresentDone();
    timer_late = 5;
    Frame_UpdateSkip();
    skipped = &frame_trace.frames[first + 1];
    assert(frame_trace.count == first + 2 && !skipped->drawn && skipped->swap_calls == 0);
    assert(skipped->late == 5 && !skipped->next_skip && skipped->resync);
    assert(skipped->next_start == 0);
    timer_late = 0;

    if (uses_fixture) {
        frame_trace.frames = NULL;
        frame_trace.previous = NULL;
        frame_trace.count = first;
    }
}

int main(void)
{
    printf("abi: %d-bit build\n", (int)(sizeof(void *) * 8));
    g_timerFrequency = 1000;
    g_timerFrameTicks = 17;
    check_frame(RENDER_SYSTEM, 0, 0, 0);
    check_frame(RENDER_OPENGL, 0, 0, 0);
    check_frame(RENDER_OPENGL_SCREEN, 0, 0, 0);
    check_frame(RENDER_SYSTEM, 1, 0, 0);
    check_frame(RENDER_OPENGL, 1, 0, 0);
    check_frame(RENDER_OPENGL, 0, 1, 0);
#if TEST_ANDROID
    check_frame(RENDER_SYSTEM, 0, 0, 1);
#endif
#if TEST_ANDROID != 1
    check_primitives();
#endif
    check_trace();
    puts("ok: frame phases include presentation; skips, speedup, failures, primitive counts and frame trace");
    return 0;
}
