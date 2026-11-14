#ifndef MUGEN_GPUTIME_H
#define MUGEN_GPUTIME_H

/*
 * Port addition: GPU-side timing with EXT_disjoint_timer_query.
 *
 * Every timing this project has taken so far is CPU wall-clock, which cannot
 * tell driver work apart from waiting. That gap mattered: batchprof attributes
 * ~4.5-5.5 ms of a 2D frame to the draw submission, the instanced path collapsed
 * that counter to zero, and the frame rate did not move -- so either the cost
 * moves elsewhere or it is GPU execution surfacing at the call. Only a GPU timer
 * separates those.
 *
 * The device exposes the extension (ARM Mali-G31, GLES 3.2; see
 * Gles_LogCapabilities), but the vendored glad build has only core ES 3.0 query
 * entry points, so the EXT functions are resolved through SDL at runtime. When
 * the extension or the entry points are missing everything here is a no-op and
 * GpuTime_Ready() stays 0.
 *
 * STATUS: opt-in and not yet trustworthy on the RG351MP. The extension is
 * advertised (Gles_LogCapabilities) and the queries are created, but the elapsed
 * values read back as 0xFFFF... rather than a duration, and enabling the probe
 * drops the frame rate from ~55 to ~14 -- the queries themselves perturb what
 * they measure. Set MUGEN_GPUTIME=1 to try it; the default is off so no other
 * measurement is affected. The CPU/GPU attribution question this was built to
 * answer is therefore still open.
 *
 * Usage: Begin/End around a span. Results are collected without ever blocking
 * the frame (a query still in flight is simply skipped), and they are discarded
 * when the driver reports a disjoint event, since then the numbers are garbage.
 */

#ifdef MUGEN_GLES

/* Resolves the entry points and creates the query objects. 1 when usable. */
int GpuTime_Init(void);

/* Nonzero when GPU timing is active (extension present and enabled). */
int GpuTime_Ready(void);

void GpuTime_BeginDraw(void);
void GpuTime_EndDraw(void);
void GpuTime_BeginPresent(void);
void GpuTime_EndPresent(void);

/*
 * Mean GPU milliseconds per sample over the samples completed since the last
 * call, which resets the accumulation. Returns the number of samples the means
 * cover (0 when none finished, and then the outputs are left at 0).
 */
int GpuTime_TakeAverages(double *draw_ms, double *present_ms);

/* Drops in-flight state after a context loss. */
void GpuTime_Reset(void);

#endif /* MUGEN_GLES */

#endif /* MUGEN_GPUTIME_H */
