/*
 * libresample 0.1.3, multi-channel, as used by M.U.G.E.N 1.1.
 *
 * Upstream libresample (Dominic Mazzoni, based on resample-1.7 by Julius
 * Smith) resamples mono signals only: its buffers are flat sample arrays and
 * resample_open takes no channel count. Elecbyte's libresample.dll instead
 * exports a channel-aware resample_open and treats the sample buffers as
 * interleaved frames. mugen.exe calls the channel-aware form
 * (src/wav.c passes mid.channels as the second argument and indexes the
 * buffers as `in_float + channels * used`), so the upstream ABI does not fit.
 *
 * This vendored copy is upstream 0.1.3 with the channel parameter added back.
 * See VENDORED.md for exactly what changed and why the resampled output is
 * numerically identical to the mono original for one channel.
 */
#ifndef LIBRESAMPLE_H
#define LIBRESAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIBRESAMPLE_STATIC
#define LIBRESAMPLE_API
#else
#define LIBRESAMPLE_API __declspec(dllimport)
#endif

/*
 * Builds the shared filter tables before the first resample_open.
 * Kept for ABI compatibility with the shipped libresample.dll; the vendored
 * implementation computes its tables per handle and ignores these.
 */
LIBRESAMPLE_API int __cdecl resample_init(int low_quality, int high_quality);

LIBRESAMPLE_API void __cdecl resample_deinit(void);

/* A resampler for `channels` interleaved channels and factors in [min_factor, max_factor]. */
LIBRESAMPLE_API void *__cdecl resample_open(int quality, int channels, double min_factor, double max_factor);

/*
 * Resamples up to in_len input frames by factor (output rate / input rate);
 * last is nonzero for the final block. Buffers hold interleaved frames:
 * in must have in_len * channels floats, out has room for out_len * channels.
 * Stores the input frames consumed in *in_used and returns the output frames
 * written (at most out_len).
 */
LIBRESAMPLE_API int __cdecl resample_process(void *handle, double factor, float *in, int in_len, int last,
                                             int *in_used, float *out, int out_len);

/* Forgets buffered samples so the handle can start a new stream. */
LIBRESAMPLE_API void __cdecl resample_reset(void *handle);

LIBRESAMPLE_API void __cdecl resample_close(void *handle);

/* Upstream API, kept because it costs nothing and the DLL exports them. */
LIBRESAMPLE_API void *__cdecl resample_dup(const void *handle);
LIBRESAMPLE_API int __cdecl resample_get_filter_width(const void *handle);

#ifdef __cplusplus
}
#endif

#endif /* LIBRESAMPLE_H */
