/*
 * Port addition: GPU-side timing with EXT_disjoint_timer_query (gputime.h).
 *
 * Not part of the original binary; there is no address to name. It exists to
 * answer a question CPU wall-clock could not: whether a measured span is work or
 * waiting.
 */
#include "mugen.h"

#ifdef MUGEN_GLES

#include <SDL.h>
#include <stdlib.h>

#include "gputime.h"

/* From GL_EXT_disjoint_timer_query (not in the vendored glad header). */
#define GPUTIME_TIME_ELAPSED_EXT 0x88BF
#define GPUTIME_GPU_DISJOINT_EXT 0x8FBB

typedef void (*PFN_GENQUERIESEXT)(GLsizei, GLuint *);
typedef void (*PFN_DELETEQUERIESEXT)(GLsizei, const GLuint *);
typedef void (*PFN_BEGINQUERYEXT)(GLenum, GLuint);
typedef void (*PFN_ENDQUERYEXT)(GLenum);
typedef void (*PFN_GETQUERYOBJECTUIVEXT)(GLuint, GLenum, GLuint *);
typedef void (*PFN_GETQUERYOBJECTUI64VEXT)(GLuint, GLenum, GLuint64 *);

static PFN_GENQUERIESEXT p_glGenQueriesEXT;
static PFN_DELETEQUERIESEXT p_glDeleteQueriesEXT;
static PFN_BEGINQUERYEXT p_glBeginQueryEXT;
static PFN_ENDQUERYEXT p_glEndQueryEXT;
static PFN_GETQUERYOBJECTUIVEXT p_glGetQueryObjectuivEXT;
static PFN_GETQUERYOBJECTUI64VEXT p_glGetQueryObjectui64vEXT;

/* Queries per span, so a frame never waits for the previous result. */
#define GPUTIME_SLOTS 3

typedef struct GpuSpan {
    GLuint query[GPUTIME_SLOTS];
    int pending[GPUTIME_SLOTS];
    int next;
    int open;                   /* a query of this span is running now */
    int open_slot;
    double sum_ms;
    int samples;
} GpuSpan;

static GpuSpan g_gpu_draw;
static GpuSpan g_gpu_present;
static int g_gpu_state;         /* 0 untried, 1 ready, -1 unusable */

/* Nonzero when the driver says results since the last check are unreliable. */
static int gputime_disjoint(void)
{
    GLint disjoint = 0;

    glGetIntegerv(GPUTIME_GPU_DISJOINT_EXT, &disjoint);
    return disjoint != 0;
}

/* Collects every finished query of a span, without blocking. */
static void gputime_collect(GpuSpan *span)
{
    int i;

    for (i = 0; i < GPUTIME_SLOTS; i++) {
        GLuint available = 0;
        GLuint64 ns = 0;

        if (!span->pending[i]) {
            continue;
        }
        if (p_glGetQueryObjectuivEXT == NULL || p_glGetQueryObjectui64vEXT == NULL) {
            span->pending[i] = 0;
            continue;
        }
        p_glGetQueryObjectuivEXT(span->query[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            continue;
        }
        p_glGetQueryObjectui64vEXT(span->query[i], GL_QUERY_RESULT, &ns);
        span->pending[i] = 0;
        span->sum_ms += (double)ns / 1000000.0;
        span->samples++;
    }
}

int GpuTime_Init(void)
{
    const char *ext;
    int i;

    if (g_gpu_state != 0) {
        return g_gpu_state == 1;
    }
    g_gpu_state = -1;
    /*
     * OPT-IN (MUGEN_GPUTIME=1): on this driver the probe is not yet trustworthy.
     * The extension is advertised and the queries are created, but the elapsed
     * values come back as 0xFFFF... (18446743708803 ms was observed), and merely
     * running the queries drops the frame rate from ~55 to ~14, so leaving it on
     * by default would corrupt every other measurement.
     */
    {
        const char *enable = getenv("MUGEN_GPUTIME");

        if (enable == NULL || enable[0] != '1') {
            return 0;
        }
    }
    ext = (const char *)glGetString(GL_EXTENSIONS);
    if (ext == NULL) {
        /* ES 3.0+ exposes the strings through glGetStringi; a NULL here means
         * the legacy query is unavailable, and the extension list must be
         * searched that way. Reported rather than guessed at. */
        Log_Printf("gpu: GL_EXTENSIONS unavailable through glGetString; GPU timing off\n");
        return 0;
    }
    if (strstr(ext, "GL_EXT_disjoint_timer_query") == NULL &&
        strstr(ext, "GL_EXT_disjoint_timer_query_webgl2") == NULL) {
        Log_Printf("gpu: no EXT_disjoint_timer_query; GPU timing off\n");
        return 0;
    }
    p_glGenQueriesEXT = (PFN_GENQUERIESEXT)SDL_GL_GetProcAddress("glGenQueriesEXT");
    p_glDeleteQueriesEXT = (PFN_DELETEQUERIESEXT)SDL_GL_GetProcAddress("glDeleteQueriesEXT");
    p_glBeginQueryEXT = (PFN_BEGINQUERYEXT)SDL_GL_GetProcAddress("glBeginQueryEXT");
    p_glEndQueryEXT = (PFN_ENDQUERYEXT)SDL_GL_GetProcAddress("glEndQueryEXT");
    p_glGetQueryObjectuivEXT =
        (PFN_GETQUERYOBJECTUIVEXT)SDL_GL_GetProcAddress("glGetQueryObjectuivEXT");
    p_glGetQueryObjectui64vEXT =
        (PFN_GETQUERYOBJECTUI64VEXT)SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT");
    if (p_glGenQueriesEXT == NULL || p_glBeginQueryEXT == NULL || p_glEndQueryEXT == NULL ||
        p_glGetQueryObjectuivEXT == NULL || p_glGetQueryObjectui64vEXT == NULL) {
        Log_Printf("gpu: EXT_disjoint_timer_query listed but its entry points are missing\n");
        return 0;
    }
    p_glGenQueriesEXT(GPUTIME_SLOTS, g_gpu_draw.query);
    p_glGenQueriesEXT(GPUTIME_SLOTS, g_gpu_present.query);
    for (i = 0; i < GPUTIME_SLOTS; i++) {
        if (g_gpu_draw.query[i] == 0 || g_gpu_present.query[i] == 0) {
            Log_Printf("gpu: glGenQueriesEXT returned 0; GPU timing off\n");
            return 0;
        }
    }
    g_gpu_state = 1;
    Log_Printf("gpu: EXT_disjoint_timer_query active (GPU times are the GPU's own)\n");
    return 1;
}

int GpuTime_Ready(void)
{
    return g_gpu_state == 1;
}

void GpuTime_Reset(void)
{
    memset(&g_gpu_draw, 0, sizeof(g_gpu_draw));
    memset(&g_gpu_present, 0, sizeof(g_gpu_present));
}

static void gputime_begin(GpuSpan *span)
{
    int slot;

    if (g_gpu_state != 1 || span->open) {
        return;
    }
    gputime_collect(span);
    if (gputime_disjoint()) {
        /* Results from before the event are meaningless: drop what is pending. */
        int i;

        for (i = 0; i < GPUTIME_SLOTS; i++) {
            span->pending[i] = 0;
        }
        return;
    }
    slot = span->next;
    span->next = (span->next + 1) % GPUTIME_SLOTS;
    if (span->pending[slot]) {
        return;                 /* still in flight: skip this frame's sample */
    }
    p_glBeginQueryEXT(GPUTIME_TIME_ELAPSED_EXT, span->query[slot]);
    span->open = 1;
    span->open_slot = slot;
}

static void gputime_end(GpuSpan *span)
{
    if (g_gpu_state != 1 || !span->open) {
        return;
    }
    p_glEndQueryEXT(GPUTIME_TIME_ELAPSED_EXT);
    span->pending[span->open_slot] = 1;
    span->open = 0;
}

void GpuTime_BeginDraw(void)
{
    gputime_begin(&g_gpu_draw);
}

void GpuTime_EndDraw(void)
{
    gputime_end(&g_gpu_draw);
}

void GpuTime_BeginPresent(void)
{
    gputime_begin(&g_gpu_present);
}

void GpuTime_EndPresent(void)
{
    gputime_end(&g_gpu_present);
}

int GpuTime_TakeAverages(double *draw_ms, double *present_ms)
{
    int samples;

    if (draw_ms != NULL) {
        *draw_ms = 0.0;
    }
    if (present_ms != NULL) {
        *present_ms = 0.0;
    }
    if (g_gpu_state != 1) {
        return 0;
    }
    gputime_collect(&g_gpu_draw);
    gputime_collect(&g_gpu_present);
    samples = g_gpu_draw.samples < g_gpu_present.samples ? g_gpu_draw.samples : g_gpu_present.samples;
    if (samples > 0) {
        if (draw_ms != NULL) {
            *draw_ms = g_gpu_draw.sum_ms / (double)g_gpu_draw.samples;
        }
        if (present_ms != NULL) {
            *present_ms = g_gpu_present.sum_ms / (double)g_gpu_present.samples;
        }
    }
    g_gpu_draw.sum_ms = 0.0;
    g_gpu_draw.samples = 0;
    g_gpu_present.sum_ms = 0.0;
    g_gpu_present.samples = 0;
    return samples;
}

#endif /* MUGEN_GLES */
