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
 *   wait     the frame limiter / vsync wait in Screen_EndFrame
 *   present  the flip (Screen_Present), including any scaling blit
 *
 * A frame that is capped by vsync shows it in `wait`; a frame the engine cannot
 * keep up with shows it in `logic` or `draw`. Per-frame draw calls and triangles
 * are counted alongside, because "draw" being expensive with 20 draw calls and
 * with 2000 mean very different things.
 */

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
    int frames;         /* frames the means cover; 0 when there is nothing */
} PerfAverages;

void Perf_BeginFrame(void);
void Perf_LogicDone(void);
void Perf_DrawDone(void);
void Perf_WaitDone(void);
void Perf_PresentDone(void);

/* One GL draw: n triangles, counted for the frame in progress. */
void Perf_CountDraw(int triangles);

/* Means over the last whole second, and starts a new one. 0 when the samples
 * are incomplete or too few to be worth printing. */
int Perf_TakeAverages(PerfAverages *out);

#endif /* MUGEN_PERF_H */