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

static int swaps, uploads, desktop_presents, fail_upload;
static GameVars game;

int Timer_WaitFrame(void) { clock_ticks += 3; return 1; }
int64_t Timer_GetFrame(void) { return g_timerFrame; }
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

int main(void)
{
    printf("abi: %d-bit build\n", (int)(sizeof(void *) * 8));
    check_frame(RENDER_SYSTEM, 0, 0, 0);
    check_frame(RENDER_OPENGL, 0, 0, 0);
    check_frame(RENDER_OPENGL_SCREEN, 0, 0, 0);
    check_frame(RENDER_SYSTEM, 1, 0, 0);
    check_frame(RENDER_OPENGL, 1, 0, 0);
    check_frame(RENDER_OPENGL, 0, 1, 0);
#if TEST_ANDROID
    check_frame(RENDER_SYSTEM, 0, 0, 1);
#endif
    check_primitives();
    puts("ok: frame phases include presentation; skips, speedup, failures and primitive counts");
    return 0;
}
