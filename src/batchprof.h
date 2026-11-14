#ifndef MUGEN_BATCHPROF_H
#define MUGEN_BATCHPROF_H

/*
 * Port addition: a batch-rendering profiler, compiled only under MUGEN_DEBUG
 * (`make DEBUG=1`, or `port\arm64\build.bat --debug`).
 *
 * Why it exists. Both GL backends submit one sprite quad per GL draw call and,
 * on GLES, re-stream the whole four-vertex buffer for each of them
 * (GlesDraw_Quad -> glBufferData + glDrawArrays). Nothing is accumulated, so
 * "batching" is currently a batch of exactly one, and the per-quad cost is
 * dominated by driver overhead rather than by pixels. The per-frame `perf:`
 * line cannot show that: it counts draw calls and triangles, which say how much
 * work was submitted but not how *fragmented* the submission was.
 *
 * What this measures, per frame:
 *
 *   calls    GL draw calls issued
 *   quads    sprite/solid quads submitted (a model draw is not one)
 *   verts    vertices streamed to the GPU
 *   stream   vertex bytes uploaded to a vertex buffer -- the GLES backend
 *            re-sends the whole four-vertex buffer per quad, so this grows
 *            with quad count and is what a batch would collapse. The desktop
 *            backend draws immediate mode and has no buffer to re-send, so it
 *            reports 0 here; compare `calls`, `verts` and `cpu` instead.
 *   prog     program binds, counting only the ones that actually changed
 *   tex      texture binds, counting only the ones that actually changed
 *   cpu      microseconds spent inside the submission code
 *
 * `quads / calls` is the batching ratio. It is 1.00 while each quad is its own
 * draw; a batched path would submit many quads per call and this number would
 * rise. `prog` and `tex` are the state changes a batch would amortize.
 *
 * Honest limits: the CPU figure includes the profiler's own two counter reads
 * per submission (roughly 0.1 us each on the handhelds this targets), so treat
 * it as an upper bound on submission time, not an exact one. Counts are exact.
 * None of this is compiled into a release build: the macros below expand to
 * nothing and the cost is zero, not "small".
 */

typedef struct BatchProfAverages {
    double calls;
    double quads;
    double vertices;
    double stream_kib;
    double programs;
    double textures;
    double cpu_us;
    /* The submission broken down, in microseconds per frame. `up` is the
     * texture upload that happens before the draw, `st` the program/uniform/
     * attribute setup, `buf` the vertex buffer upload and `dr` the draw call
     * itself. Their sum is close to `cpu_us` plus `up`. */
    double split_us[7];
    /* Quads per frame that the clip rectangle would reject (port addition). */
    double outside_clip;
    double zero_area;
    int frames;
} BatchProfAverages;

/* Indices into BatchProfAverages::split_us. */
enum {
    BATCHPROF_SPLIT_UPLOAD = 0,   /* sprite and palette texture uploads, summed */
    BATCHPROF_SPLIT_UPLOAD_SPRITE,/* the sprite texture alone */
    BATCHPROF_SPLIT_STATE,        /* program, uniforms, attributes */
    BATCHPROF_SPLIT_BUFFER,       /* glBufferData of the quad */
    BATCHPROF_SPLIT_DRAW,         /* glDrawArrays and the attribute teardown */
    BATCHPROF_SPLIT_PIXELS,       /* Sprite_GetPixels: decode of a cached sprite */
    BATCHPROF_SPLIT_PALETTE,      /* SpriteSet_GetPaletteConverted */
    BATCHPROF_SPLIT_COUNT
};

#ifdef MUGEN_DEBUG

/* Zeroes the frame in progress. Call once per frame, before any submission. */
void BatchProf_FrameBegin(void);

/* Closes the frame in progress into the second being accumulated. Call once
 * per frame, after the last submission. A End with no Begin is ignored. */
void BatchProf_FrameEnd(void);

/* One submission: `quads` quads / `vertices` vertices, `bytes` of vertex data
 * streamed, `triangles` drawn, and `ticks` of CPU time (BATCHPROF_NOW deltas). */
void BatchProf_Submit(int quads, int vertices, unsigned long bytes, unsigned int triangles,
                      unsigned long long ticks);

/* A glUseProgram, counted only when it changes the bound program. */
void BatchProf_Program(unsigned int program);

/* A glBindTexture on `unit`, counted only when it changes that unit's texture. */
void BatchProf_Texture(int unit, unsigned int texture);

/* Time attributed to one region of a submission (BATCHPROF_SPLIT_*), in counter
 * ticks. Lets the breakdown say whether the per-quad cost is state setup, the
 * vertex upload, the draw call or the texture upload -- guessing between those
 * is how an optimisation ends up aimed at the wrong call. */
void BatchProf_Split(int which, unsigned long long ticks);

/* Port addition: how many submitted quads the clip rectangle would reject
 * outright. Counted per frame and reported with the batch line, so the question
 * "is the engine submitting invisible work?" is answered by a number. */
void BatchProf_CountQuad(int outside_clip, int zero_area);

/* High-resolution counter for the submission timing. */
unsigned long long BatchProf_Now(void);

/* Means over the last whole second; starts a new one. Returns 0 when there is
 * nothing worth printing. */
int BatchProf_TakeAverages(BatchProfAverages *out);

/* A "0" for the timing variable in builds that measure nothing, so the call
 * site can stay identical in both. */
#define BATCHPROF_NOW() BatchProf_Now()

#else /* !MUGEN_DEBUG */

static inline void BatchProf_FrameBegin(void) {}
static inline void BatchProf_FrameEnd(void) {}
static inline void BatchProf_Submit(int quads, int vertices, unsigned long bytes, unsigned int triangles,
                                    unsigned long long ticks)
{
    (void)quads; (void)vertices; (void)bytes; (void)triangles; (void)ticks;
}
static inline void BatchProf_Program(unsigned int program) { (void)program; }
static inline void BatchProf_Texture(int unit, unsigned int texture) { (void)unit; (void)texture; }
static inline void BatchProf_Split(int which, unsigned long long ticks) { (void)which; (void)ticks; }
static inline void BatchProf_CountQuad(int outside_clip, int zero_area) { (void)outside_clip; (void)zero_area; }
static inline unsigned long long BatchProf_Now(void) { return 0ULL; }
static inline int BatchProf_TakeAverages(BatchProfAverages *out) { (void)out; return 0; }

#define BATCHPROF_NOW() 0ULL

#endif /* MUGEN_DEBUG */

#endif /* MUGEN_BATCHPROF_H */
