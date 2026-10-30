/*
 * Validates the multi-channel libresample port. Not part of the build; run by
 * hand (see the header of third_party/libresample/VENDORED.md):
 *
 *   gcc -O2 -DLIBRESAMPLE_STATIC -Ithird_party/libresample/include \
 *       -Ithird_party/libresample/src tools/check_libresample.c \
 *       third_party/libresample/src/*.c -lm -o build/check_libresample.exe
 *   ./build/check_libresample.exe
 *
 * Two properties matter:
 *
 *   1. With channels == 1 the port must produce exactly what upstream 0.1.3
 *      produced. The change only adds a stride, and for one channel every
 *      stride is 1, so agreement should be bit-exact.
 *   2. With channels == 2 each channel must come out exactly as it would if
 *      resampled on its own. That is what the shipped DLL does, and it is the
 *      whole point of the port.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <libresample.h>

#define FRAMES 1000

static int failures;

static void fail(const char *what)
{
    printf("  FAIL: %s\n", what);
    failures++;
}

/* A deterministic, easily-checked signal: two different sines. */
static void fill(float *buf, int frames, int channels)
{
    int i;
    int ch;

    for (i = 0; i < frames; i++) {
        for (ch = 0; ch < channels; ch++) {
            double f = (ch == 0) ? 0.05 : 0.017;
            buf[i * channels + ch] = (float)sin(2.0 * 3.14159265358979 * f * i);
        }
    }
}

/* Resamples `frames` interleaved frames, returns output frames via *out. */
static int run(float *in, int frames, int channels, double ratio, float *out, int outCap)
{
    void *h = resample_open(1, channels, ratio, ratio);
    int used = 0;
    int got;
    int total = 0;

    if (h == NULL) {
        fail("resample_open returned NULL");
        return 0;
    }
    got = resample_process(h, ratio, in, frames, 1, &used, out, outCap);
    if (got < 0) {
        fail("resample_process returned -1 (factor outside range?)");
        resample_close(h);
        return 0;
    }
    total = got;
    resample_close(h);
    return total;
}

int main(void)
{
    static float in[FRAMES * 2];
    static float mono[FRAMES];
    static float out1[(FRAMES * 4) * 2];
    static float out2[(FRAMES * 4) * 2];
    static float ref[(FRAMES * 4) * 2];
    static float scratch[(FRAMES * 4) * 2];
    double ratios[] = { 1.0, 0.5, 2.0, 0.75, 1.3333 };
    int r;

    printf("libresample multi-channel port check\n");

    for (r = 0; r < (int)(sizeof ratios / sizeof ratios[0]); r++) {
        double ratio = ratios[r];
        int n1, n2, k;

        /* ---- 1. mono: must match the pre-port layout exactly ----
           With one channel an interleaved buffer is just the sample array,
           so feeding `mono` to channels=1 is the upstream call shape. */
        fill(mono, FRAMES, 1);
        n1 = run(mono, FRAMES, 1, ratio, out1, FRAMES * 4);
        if (n1 <= 0)
            fail("mono produced no output");
        printf("  ratio %.4f: mono %d frames in -> %d out\n", ratio, FRAMES, n1);

        /* ---- 2. stereo == two independent monos ---- */
        fill(in, FRAMES, 2);
        n2 = run(in, FRAMES, 2, ratio, out2, FRAMES * 4);
        printf("  ratio %.4f: stereo %d frames in -> %d out\n", ratio, FRAMES, n2);

        if (n2 != n1) {
            printf("  FAIL: stereo produced %d frames, mono %d\n", n2, n1);
            failures++;
            continue;
        }

        /* Channel 0 alone, and channel 1 alone, through the 1-channel path. */
        for (k = 0; k < FRAMES; k++) {
            scratch[0 * FRAMES + k] = in[k * 2 + 0];
        }
        {
            int a = run(scratch, FRAMES, 1, ratio, ref, FRAMES * 4);
            int mism = 0;
            if (a != n2) {
                printf("  FAIL: channel 0 alone gave %d frames, stereo gave %d\n", a, n2);
                failures++;
            } else {
                for (k = 0; k < a; k++)
                    if (ref[k] != out2[k * 2 + 0])
                        mism++;
                if (mism) {
                    printf("  FAIL: channel 0 differs in %d of %d frames\n", mism, a);
                    failures++;
                }
            }
        }
        for (k = 0; k < FRAMES; k++) {
            scratch[0 * FRAMES + k] = in[k * 2 + 1];
        }
        {
            int a = run(scratch, FRAMES, 1, ratio, ref, FRAMES * 4);
            int mism = 0;
            if (a != n2) {
                printf("  FAIL: channel 1 alone gave %d frames, stereo gave %d\n", a, n2);
                failures++;
            } else {
                for (k = 0; k < a; k++)
                    if (ref[k] != out2[k * 2 + 1])
                        mism++;
                if (mism) {
                    printf("  FAIL: channel 1 differs in %d of %d frames\n", mism, a);
                    failures++;
                }
            }
        }
    }

    /* ---- 3. channel isolation: a signal in one channel must not bleed ---- */
    {
        static float quiet[(FRAMES * 4) * 2];
        int k;
        int n;

        /* A pure channel-0 signal: fill() writes every channel, so build it
           by hand. Zeroing the whole buffer first is not enough. */
        memset(in, 0, sizeof in);
        for (k = 0; k < FRAMES; k++)
            in[k * 2 + 0] = (float)sin(2.0 * 3.14159265358979 * 0.05 * k);
        n = run(in, FRAMES, 2, 1.0, quiet, FRAMES * 4);
        for (k = 0; k < n; k++) {
            if (quiet[k * 2 + 1] != 0.0f) {
                printf("  FAIL: channel 1 leaked %g at frame %d\n",
                       (double)quiet[k * 2 + 1], k);
                failures++;
                break;
            }
        }
        printf("  channel isolation: checked %d frames\n", n);
    }

    /* ---- 4. invalid channel count is rejected, not crashed ---- */
    if (resample_open(1, 0, 1.0, 1.0) != NULL) {
        fail("resample_open(quality, 0, ...) should return NULL");
    }

    /* ---- 5. resample_reset restarts a stream ---- */
    {
        void *h = resample_open(0, 2, 1.0, 1.0);
        int used = 0;
        float o[(FRAMES * 4) * 2];
        int a, b;

        fill(in, FRAMES, 2);
        a = resample_process(h, 1.0, in, FRAMES, 1, &used, o, FRAMES * 4);
        resample_reset(h);
        b = resample_process(h, 1.0, in, FRAMES, 1, &used, o, FRAMES * 4);
        resample_close(h);
        if (a != b) {
            printf("  FAIL: after reset %d frames, before %d\n", b, a);
            failures++;
        }
        printf("  reset: %d frames before and after\n", a);
    }

    if (failures == 0) {
        printf("\nALL CHECKS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(S) FAILED\n", failures);
    return 1;
}
