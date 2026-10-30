# libresample 0.1.3 (vendored, multi-channel)

Upstream: **libresample 0.1.3** by Dominic Mazzoni, based on `resample-1.7` by
Julius Smith. Dual-licensed **BSD / LGPL** — both licence texts are here
(`LICENSE-BSD.txt`, `LICENSE-LGPL.txt`). This project uses it under the BSD
terms.

Source: `https://github.com/minorninth/libresample` (`master`,
`src/resample.c`, `src/resamplesubs.c`, `src/filterkit.c`, `src/resample_defs.h`).
`README.upstream.md` is the upstream readme, unchanged.

## Why it is vendored rather than linked as-is

M.U.G.E.N 1.1 shipped `libresample.dll`, and `src/wav.c` calls it directly. That
DLL is **not** upstream libresample: Elecbyte patched it to resample
**multi-channel interleaved** audio. The ABIs differ:

| | upstream 0.1.3 | Elecbyte's DLL / `src/wav.c` |
|---|---|---|
| `resample_open` | `(highQuality, minFactor, maxFactor)` | `(quality, channels, minFactor, maxFactor)` |
| sample buffers | mono, flat sample array | interleaved frames, length in frames |
| `resample_process` returns | output **samples** | output **frames** |
| extra exports | — | `resample_init`, `resample_deinit`, `resample_reset`, `resample_get_inbuffer_size`, `resample_get_outbuffer_size` |

`src/wav.c` passes `mid.channels` as the second argument and indexes buffers as
`in_float + mid.channels * used`, so upstream's ABI does not fit and cannot be
made to fit by linking tricks. This copy is upstream with the channel parameter
restored. See the tracker note in `PLANS.md`.

## What was changed

Everything below is marked in the source with a comment explaining why.

1. **`rsdata` gained `int Channels`.** Buffer lengths (`XSize`, `YSize`, `Xoff`,
   `Xp`, `Xread`, `Yp`, `Nx`) stay counted in **frames**; only the allocation
   sizes and the sample strides multiply by `Channels`.
2. **`resample_open(quality, channels, minFactor, maxFactor)`** — added the
   channel argument, rejects `channels < 1`, allocates `X` as
   `(XSize + Xoff) * channels` floats and `Y` as `YSize * channels`, and zeroes
   `Xoff * channels` floats of leading history.
3. **`resample_process`** — every "sample count" in it now means frames. The
   four buffer copies (pending output out, input in, zero-pad tail, reuse
   shift) became `memcpy`/`memmove` of `frames * Channels * sizeof(float)`, and
   the output copy-out/pending logic is frame-granular.
4. **`lrsSrcUp` / `lrsSrcUD`** — take `int Channels`. The frame loop and all
   `Time` arithmetic are unchanged (channel-independent); inside each frame an
   inner loop runs the filter once per channel, stepping by `Channels` instead
   of 1. Return value is `(Y - Ystart) / Channels`, i.e. frames.
5. **`lrsFilterUp` / `lrsFilterUD` — the one real bug found.** Both tested
   `Inc == 1` to mean "this is the right wing", which was only ever valid
   because the step was always one sample. With interleaving the right-wing
   step is the channel count, so the test silently failed for `Channels > 1`,
   dropping the phase-zero coefficient skip. That produced an error of ~1e-4 on
   every frame whose phase was exactly zero (for ratio 0.75, every third
   frame). Both now test `Inc > 0`.
6. **`resample_reset`** (was only an Elecbyte export) — resets `Xp`, `Xread`,
   `Yp`, `Time` and re-zeroes the history.
7. **`resample_init` / `resample_deinit`** — no-ops returning success. Upstream
   builds its impulse response per handle inside `resample_open`, so there are
   no global tables; the symbols exist because `src/wav.c` calls them.
8. **`resample_get_inbuffer_size` / `resample_get_outbuffer_size`** — return
   `XSize` / `YSize`, matching the DLL's exports.
9. `const void *` on the read-only accessors (upstream used non-const in some
   places), and `(size_t)` casts on allocation sizes.

`resample_dup` copies `Channels` before any channel-sized allocation.

## Equivalence guarantee

For `channels == 1` the port is required to produce **identical** output to
upstream, because every stride is then 1 and `Inc > 0` agrees with `Inc == 1`.
For `channels > 1` each channel must come out exactly as it would if resampled
on its own.

`tools/check_libresample.c` verifies both, plus channel isolation and
`resample_reset`. Build and run it with:

```sh
export PATH=/c/x86devkit/bin:$PATH
gcc -O2 -DLIBRESAMPLE_STATIC -Ithird_party/libresample/include -Ithird_party/libresample/src \
    tools/check_libresample.c third_party/libresample/src/*.c -lm -o build/check_libresample.exe
./build/check_libresample.exe
```

It is not part of the build. It checks ratios 1.0, 0.5, 2.0, 0.75 and 1.3333,
which covers both the `lrsSrcUp` (`factor >= 1`) and `lrsSrcUD` (`factor < 1`)
paths, and asserts bit-exact equality per channel rather than a tolerance.

**Not verified against the shipped `libresample.dll`.** The check proves the
port is self-consistent; it does not prove Elecbyte's build computes the same
floats. `mugen.log` carries no audio samples, so that difference, if any, is
inaudible-only. If it ever matters, the DLL is in the game's
`Elecbyte.MUGEN.libs` and a comparison harness could be written.

## Building

The Makefile builds it as `build/mingw/lib/libresample.a` from the three `.c`
files, with `-DLIBRESAMPLE_STATIC` so the header does not say `dllimport`.
`-w` is used, as for the other vendored code.
