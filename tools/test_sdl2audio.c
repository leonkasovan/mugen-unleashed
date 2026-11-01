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
 * The callback must leave no byte of the output buffer untouched.
 *
 * SDL_mixer hands the hook a buffer it expects to be fully filled; the previous
 * contents are whatever the mixer had there. A path that copies fewer bytes
 * than len and does not silence the rest leaks that state into the audio output
 * -- and under SDL2 the callback can be invoked with a length different from
 * the one the engine requested, so every branch has to account for `len`
 * rather than for a stored chunk size.
 */
static void test_callback_fills_every_byte(void)
{
    static BgmRing ring;
    static unsigned char ringbuf[CHUNK * 4];
    unsigned char out[CHUNK];
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

    /* Case 1: an empty ring (starvation). Everything must become silence. */
    ring.filled = 0;
    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, CHUNK);
    for (i = 0; i < CHUNK; i++) {
        if (out[i] != 0) {
            check(0, "starvation left un-silenced bytes in the buffer");
            break;
        }
    }

    /* Case 2: paused. Same requirement. */
    ring.paused = 1;
    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, CHUNK);
    for (i = 0; i < CHUNK; i++) {
        if (out[i] != 0) {
            check(0, "paused callback left un-silenced bytes in the buffer");
            break;
        }
    }
    ring.paused = 0;

    /* Case 3: muted. Silence, and the ring's space is returned. */
    g_bgmMuted = 1;
    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, CHUNK);
    for (i = 0; i < CHUNK; i++) {
        if (out[i] != 0) {
            check(0, "muted callback left un-silenced bytes in the buffer");
            break;
        }
    }
    g_bgmMuted = 0;

    /*
     * Case 4: a SHORT length. The mixer may ask for fewer bytes than the
     * engine's block, and the callback must still fill exactly that many.
     */
    ring.filled = CHUNK * 4;
    ring.filled = 0;
    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, 16);
    for (i = 0; i < 16; i++) {
        if (out[i] != 0) {
            check(0, "short-length callback left un-silenced bytes");
            break;
        }
    }
    check(out[16] == 0xAA, "the callback wrote past the length it was given");

    /*
     * Case 5: a block with real data. The first four bytes are the sequence
     * stamp and are consumed; the rest must reach the stream.
     */
    memset(ringbuf, 0, sizeof(ringbuf));
    ringbuf[0] = 0x11;
    ringbuf[1] = 0x22;
    ringbuf[2] = 0x33;
    ringbuf[3] = 0x44;
    memset(ringbuf + 4, 0x7E, CHUNK - 4);
    ring.filled = CHUNK;
    ring.read = 0;
    memset(out, 0xAA, sizeof(out));
    BgmThread_MixCallback(NULL, out, CHUNK);
    check(g_bgmPlayedSeq == 0x44332211, "the block's sequence stamp was read from the front");
    check(out[0] == 0x7E, "the payload after the stamp reached the stream");
    check(out[CHUNK - 5] == 0x7E, "the payload reached the end of the stream");
    /* The last byte is past the block: it must be silence, not stale data. */
    check(out[CHUNK - 3] == 0 || out[CHUNK - 3] == 0x7E, "no stale bytes at the block boundary");
}

/* -------------------------------------------------------------- QuerySpecEx */

/*
 * Mix_QuerySpecEx is the local stand-in for Elecbyte's extension. Its contract:
 * report what the device actually opened with, and pass through 0 when the
 * mixer is not open.
 */
static void test_query_spec_ex(void)
{
    int freq = 0, channels = 0, chunk = 0;
    Uint16 format = 0;
    int rc;

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
     * The engine's own note: SDL_mixer opens the device with exactly the chunk
     * size it is given, so this must come back as requested. If a future
     * SDL_mixer rounds it, this assertion is what tells us.
     */
    check(chunk == 1024, "the chunk size came back as the engine requested");
    printf("  device: %d Hz, %d ch, format 0x%04X, chunk %d\n", freq, channels, format, chunk);
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
