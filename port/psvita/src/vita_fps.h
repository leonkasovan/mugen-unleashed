/*
 * PS Vita port: frame-rate logging to mugen.log.
 *
 * See vita_fps.c for what the numbers mean. Called once per frame from
 * Screen_EndFrame under MUGEN_VITA_GL.
 */
#ifndef MUGEN_VITA_FPS_H
#define MUGEN_VITA_FPS_H

/*
 * `drawn` is the presentation decision for the frame that just ended: non-zero
 * when that frame was drawn, zero when frame-skip dropped it. The caller reads
 * it before Frame_UpdateSkip, because that call sets the *next* frame's state.
 */
void Vita_FrameLog(int drawn);

/*
 * Per-frame cost accounting, for deciding what to optimise next. Values are in
 * QueryPerformanceCounter ticks (100 ns). Reported by Vita_FrameLog.
 */
void Vita_PerfAddScale(__int64 ticks);
void Vita_PerfAddFlip(__int64 ticks);

#endif /* MUGEN_VITA_FPS_H */
