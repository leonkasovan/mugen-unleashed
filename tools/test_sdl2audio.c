/*
 * Runtime tests for the SDL2 build's audio paths that a -nosound run cannot
 * reach: the custom music hook (bgmthread.c) and the Mix_QuerySpecEx shim.
 *
 * Built and run by tools/test_sdl2audio.sh. Links the engine's own
 * bgmthread.c and sdlmixercompat.c, so these are the real functions.
 *
 * The engine's own mixer-and-device path (sound.c) needs an audio device; this
 * test uses SDL_mixer's dummy-free path but tolerates a machine with no output
 * and reports what it could not check rather than passing silently.
 */
#include "mugen.h"
#include <SDL_mixer.h>

extern DECLSPEC int SDLCALL Mix_QuerySpecEx(int *frequency, Uint16 *format, int *channels, int *chunksize);

#include <stdio.h>
#include <string.h>

/* Globals the callback reads. Declared in symbols.h with these types. */
BgmRing *g_bgmRing;
int g_bgmMuted;
int g_bgmPlayedSeq;
int g_sndChunkSize;
unsigned int g_bgmChunkBytes;
AudioFormat g_bgmThreadFormat;
void *g_bgmScratch;

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

/*
 * bgmthread.c holds the whole BGM thread next to the callback. Only the
 * callback is under test, so everything else it needs to link is stubbed --
 * with the exact types from src/symbols.h, so the compiler checks them.
 */
int g_outOfMemory;
int g_streamError;
int g_bgmQueueCount;
BgmCommand g_bgmQueue[BGM_QUEUE_SIZE];
void *g_bgmMutex;
BgmThreadId g_bgmThread;
int g_bgmThreadRunning;
int g_bgmHooked;
int g_bgmFillSeq;
int g_bgmEventCount;
BgmEvent g_bgmEvents[BGM_EVENT_QUEUE_SIZE];
int g_bgmBackgroundChanged;
BgmStream *g_bgmStream;
unsigned int g_bgmPauseFlags;

BgmRing *BgmRing_Create(int size)
{
    (void)size;
    return NULL;
}

void *Mem_Calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

/* The BGM stream interface; the callback never calls these. Signatures are the
 * ones in src/symbols.h so the compiler checks them against the real ones. */
BgmStream *Bgm_StreamCreate(void) { return NULL; }
void Bgm_StreamFree(void) { }
void Bgm_StreamClose(BgmStream *s) { (void)s; }
int Bgm_StreamOpen(const BgmInfo *info) { (void)info; return 0; }
unsigned int Bgm_StreamRead(void *dst, int *status) { (void)dst; (void)status; return 0; }
int Bgm_StreamCompare(const BgmInfo *info) { (void)info; return 0; }
int Bgm_StreamSetMute(BgmStream *s, int muted) { (void)s; (void)muted; return 0; }
void Bgm_StreamSetVolume(double volume) { (void)volume; }
int Bgm_StreamStart(BgmStream *s, unsigned int flags) { (void)s; (void)flags; return 0; }
void Bgm_StreamStop(void) { }

void MatchLog_Printf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

/* ------------------------------------------------------------------ hook */

#define CHUNK 64

/*
 * The callback must produce an exact, predictable image in the buffer it is
 * given.
 *
 * SDL_mixer hands the hook a buffer it expects to be fully filled; the previous
 * contents are whatever the mixer had there. So every test below compares the
 * WHOLE buffer -- payload bytes, silence bytes and the guard bytes past `len` --
 * against the image the callback is required to produce. Spot-checking a few
 * offsets let real defects through: corrupting a payload byte, or leaving the
 * final silence byte poisoned, both passed the earlier version of this test.
 */

#define GUARD 16

/* The expected image for a call that must produce nothing but silence. */
static void expect_silence(unsigned char *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] != 0) {
            printf("  byte %d is 0x%02X, expected silence\n", i, buf[i]);
            check(0, "callback left un-silenced bytes in the buffer");
            return;
        }
    }
}

/*
 * Runs one call with a poisoned buffer plus a guard area past `len`, and
 * compares everything: [0,len) against `expected_payload`, and [len, len+GUARD)
 * against the poison, which proves the callback did not write past its length.
 */
static void run_and_compare(const char *what, int len, const unsigned char *payload, int payload_len)
{
    unsigned char out[CHUNK + GUARD];
    int i;

    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, len);
    for (i = 0; i < len; i++) {
        unsigned char want = (i < payload_len) ? payload[i] : 0;

        if (out[i] != want) {
            printf("  %s: byte %d is 0x%02X, expected 0x%02X\n", what, i, out[i], want);
            check(0, what);
            return;
        }
    }
    for (i = len; i < len + GUARD; i++) {
        if (out[i] != 0xAA) {
            printf("  %s: wrote 0x%02X at byte %d, which is past the length %d\n", what, out[i], i, len);
            check(0, "the callback wrote past the length it was given");
            return;
        }
    }
}

static void test_callback_fills_every_byte(void)
{
    static BgmRing ring;
    static unsigned char ringbuf[CHUNK * 4];
    unsigned char expected[CHUNK];
    int i;

    memset(&ring, 0, sizeof(ring));
    ring.buf = ringbuf;
    ring.block = CHUNK;
    ring.count = 4;
    ring.size = CHUNK * 4;
    ring.read = 0;
    g_bgmRing = &ring;
    g_bgmChunkBytes = CHUNK + 4;
    g_bgmThreadFormat.format = AUDIO_FMT_S16;
    g_bgmMuted = 0;
    g_bgmPlayedSeq = 1;
    memset(ringbuf, 0, sizeof(ringbuf));

    /* Case 1: an empty ring (starvation) -- all silence. */
    ring.filled = 0;
    run_and_compare("starvation left un-silenced bytes", CHUNK, NULL, 0);

    /* Case 2: paused -- all silence. */
    ring.paused = 1;
    run_and_compare("paused callback left un-silenced bytes", CHUNK, NULL, 0);
    ring.paused = 0;

    /* Case 3: muted -- all silence. */
    g_bgmMuted = 1;
    run_and_compare("muted callback left un-silenced bytes", CHUNK, NULL, 0);
    g_bgmMuted = 0;

    /*
     * Case 4: a ring that HAS data, asked for FEWER bytes than the block holds.
     *
     * This is the case the previous version never ran: it set ring.filled and
     * then immediately overwrote it with 0, so only the starvation branch was
     * exercised and deleting the `n > len` clamp went unnoticed. The clamp is
     * what stops the callback copying a whole block into a short buffer.
     */
    for (i = 0; i < CHUNK; i++) {
        ringbuf[i] = (unsigned char)(0x40 + i);   /* varied, so a wrong offset shows */
    }
    expected[0] = 0;                              /* filled below */
    ring.filled = CHUNK;
    ring.read = 0;
    for (i = 0; i < CHUNK; i++) {
        ringbuf[i] = (unsigned char)(0x40 + i);
    }
    /* Block layout: 4-byte sequence stamp, then payload. Ask for 16 bytes: the
     * callback copies the first 16 of the payload and must not run past len. */
    {
        unsigned char want[16];

        for (i = 0; i < 16; i++) {
            want[i] = ringbuf[4 + i];
        }
        run_and_compare("short-length callback copied the wrong bytes", 16, want, 16);
    }

    /*
     * Case 5: a full block. Everything after the 4-byte stamp must arrive at
     * byte-for-byte the right offset, and the tail beyond the payload must be
     * silence.
     */
    ring.filled = CHUNK;
    ring.read = 0;
    memset(ringbuf, 0, sizeof(ringbuf));
    ringbuf[0] = 0x11;
    ringbuf[1] = 0x22;
    ringbuf[2] = 0x33;
    ringbuf[3] = 0x44;
    for (i = 4; i < CHUNK; i++) {
        ringbuf[i] = (unsigned char)(0xA0 + i);
    }
    {
        unsigned char want[CHUNK];

        /* The callback consumes the stamp and copies up to len bytes of the
         * payload; a block of CHUNK including the stamp yields CHUNK-4 payload
         * bytes, so the last four output bytes are silence. */
        for (i = 0; i < CHUNK; i++) {
            int src = 4 + i;

            want[i] = (src < CHUNK) ? ringbuf[src] : 0;
        }
        run_and_compare("full block did not reach the stream byte for byte", CHUNK, want, CHUNK);
    }
    check(g_bgmPlayedSeq == 0x44332211, "the block's sequence stamp was read from the front");

    /*
     * Case 6: a block whose payload is entirely non-silent, so a callback that
     * drops the final byte cannot be mistaken for one that pads correctly. The
     * stamp is zero here; everything after it is 0x5A, so the last four output
     * bytes are the only ones that may be silence.
     */
    ring.read = 0;
    ring.filled = CHUNK;
    memset(ringbuf, 0x5A, sizeof(ringbuf));
    memset(ringbuf, 0x00, 4);
    {
        unsigned char want[CHUNK];

        for (i = 0; i < CHUNK; i++) {
            want[i] = (i < CHUNK - 4) ? 0x5A : 0;
        }
        run_and_compare("uniform payload did not fill the stream", CHUNK, want, CHUNK);
    }
}

/* -------------------------------------------------------------- QuerySpecEx */

/*
 * Mix_QuerySpecEx is the local stand-in for Elecbyte's extension. Its contract:
 * report what the device actually opened with, and pass through 0 when the
 * mixer is not open.
 */
/* Set by the mixer's post-mix callback, which sees the real buffer length. */
static volatile int posts;
static volatile int measured_len;

static void SDLCALL MeasurePostMix(void *udata, Uint8 *stream, int len)
{
    (void)udata;
    (void)stream;
    if (posts == 0) {
        measured_len = len;
    }
    posts++;
}

static void test_query_spec_ex(void)
{
    int freq = 0, channels = 0, chunk = 0;
    Uint16 format = 0;
    int rc;
    int i;

    /* With no mixer open, Mix_QuerySpec returns 0 and so must the shim. */
    rc = Mix_QuerySpecEx(&freq, &format, &channels, &chunk);
    check(rc == 0, "Mix_QuerySpecEx reports 0 when no device is open");

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("SKIP: no audio subsystem (%s); Mix_OpenAudio not exercised\n", SDL_GetError());
        return;
    }
    g_sndChunkSize = 1024;
    if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, g_sndChunkSize) < 0) {
        printf("SKIP: no audio device (%s); Mix_OpenAudio not exercised\n", SDL_GetError());
        return;
    }
    freq = channels = chunk = 0;
    format = 0;
    rc = Mix_QuerySpecEx(&freq, &format, &channels, &chunk);
    check(rc > 0, "Mix_QuerySpecEx reports success once the mixer is open");
    check(freq > 0, "the frequency was reported");
    check(channels == 1 || channels == 2, "a sane channel count was reported");
    check(format == AUDIO_U8 || format == AUDIO_S16LSB || format == AUDIO_S16MSB ||
              format == AUDIO_S32LSB || format == AUDIO_F32LSB,
          "the format is one the engine understands");
    /*
     * Mix_QuerySpecEx echoes back the size the engine REQUESTED; it cannot
     * report what the backend actually does, because the shim has no way to ask.
     * That is why the value is labelled "requested" and the real callback
     * length is measured separately below.
     */
    check(chunk == 1024, "Mix_QuerySpecEx reports the requested chunk size (requested = 1024 frames)");
    printf("  device: %d Hz, %d ch, format 0x%04X, requested chunk %d frames\n", freq, channels, format,
           chunk);

    /*
     * Measure the length the mixer really calls back with.
     *
     * Mix_SetPostMix receives the same (stream, len) the mixer works on, so the
     * first call after opening tells us the backend's actual buffer size --
     * independent of the shim. If a backend rounded or clamped the request, the
     * two numbers differ and this is what shows it.
     */
    posts = 0;
    measured_len = 0;
    Mix_SetPostMix(MeasurePostMix, NULL);
    /* Let the device run until at least one callback has happened. */
    for (i = 0; i < 200 && posts == 0; i++) {
        SDL_Delay(5);
    }
    Mix_SetPostMix(NULL, NULL);
    if (posts == 0 || measured_len <= 0) {
        printf("SKIP: the device produced no callback in 1s; real buffer length not measured\n");
    } else {
        /*
         * Mix_OpenAudio's chunksize is in sample FRAMES, and the callback's len
         * is in BYTES, so the expected length is
         *
         *     frames * channels * bytes_per_sample
         *
         * SDL_AUDIO_BITSIZE gives the bits per sample from the format word.
         * Deriving bytes_per_sample from the sign bit instead -- as an earlier
         * version did with `format & 0x8000` -- reads signedness as size:
         * AUDIO_S16LSB (0x8010) is 16 bits, but that test says "4 bytes", so a
         * 4096-byte callback was reported as 512 frames instead of 1024.
         */
        int bytes_per_sample = SDL_AUDIO_BITSIZE(format) / 8;
        int bytes_per_frame = bytes_per_sample * channels;
        int expected = chunk * bytes_per_frame;

        printf("  measured: %d bytes = %d frames of %d bytes (%d-bit samples)\n", measured_len,
               measured_len / bytes_per_frame, bytes_per_frame, SDL_AUDIO_BITSIZE(format));
        check(measured_len == expected,
              "the callback length equals requested frames x channels x bytes per sample");
        if (measured_len != expected) {
            printf("   expected %d bytes (%d frames x %d ch x %d B)\n", expected, chunk, channels,
                   bytes_per_sample);
        }
    }
    Mix_CloseAudio();
}


/* ----------------------------------------------------------------- RWops */

/*
 * SDL2_mixer queries rw->size when it loads a music file (MOD seeking and loop
 * points). A NULL or wrong size turns into a read past the end, so this checks
 * the engine's Stream_ToRWops answers it and that the ownership contract holds:
 * Mix_LoadMUS_RW is called with freesrc = 0, so the engine's stream must still
 * be usable after the music is freed.
 */
static void test_rwops_size(void)
{
    static const char payload[] = "abcdefghijklmnop";
    Stream *st;
    SDL_RWops *rw;
    Sint64 size;

    st = Stream_OpenMemory((void *)payload, (int)sizeof(payload) - 1);
    if (st == NULL) {
        printf("SKIP: Stream_OpenMemory failed\n");
        return;
    }
    rw = Stream_ToRWops(st);
    if (rw == NULL) {
        printf("SKIP: Stream_ToRWops returned NULL\n");
        Stream_Close(st);
        return;
    }
    /* SDL2's size callback is the one SDL_mixer will call. */
    if (rw->size == NULL) {
        check(0, "the RWops has no size callback, which SDL2_mixer needs");
    } else {
        size = rw->size(rw);
        check(size == (Sint64)strlen(payload), "rw->size reports the stream length");
        /* Reading moves the position; size must not change because of it. */
        rw->read(rw, (char[4]){0}, 1, 4);
        check(rw->size(rw) == (Sint64)strlen(payload), "rw->size is independent of the position");
        rw->seek(rw, 2, RW_SEEK_SET);
        check(rw->size(rw) == (Sint64)strlen(payload), "rw->size is independent of a seek");
    }
    /* Stream_ToRWops does not own the stream (close_stream is 0). */
    rw->close(rw);
    check(Stream_GetSize(st) == (long)strlen(payload), "closing the RWops left the engine's stream alive");
    Stream_Close(st);
}

int main(void)
{
    printf("bgm hook callback:\n");
    test_callback_fills_every_byte();
    printf("Mix_QuerySpecEx:\n");
    test_query_spec_ex();
    printf("RWops size:\n");
    test_rwops_size();
    if (failures == 0) {
        printf("ok: sdl2 audio tests passed\n");
    }
    return failures != 0;
}
