/* Regression: decoder results must survive LP64 struct padding, and a failed
 * resampler must never leave an open stream available for playback. */
#include "mugen.h"
#include <assert.h>

BgmStream *g_bgmStream;
Resampler *g_bgmResampler;
AudioFormat g_bgmFormat;
static BgmStream stream;
static Stream file;
static int closed_files, closed_plugins, fail_begin, plugin_mode;
static AudioFormat decoded = {44100.0, AUDIO_FMT_S16, 2};
static AudioFormat current;
static Resampler converter;
static ResamplerSession session;
static ResamplerVtbl methods;
static BgmSubsystem plugin;

void MatchLog_Printf(int level, const char *fmt, ...) {}
const BgmSubsystem *Bgm_FindSubsystem(const char *path) { return &plugin; }
Stream *Stream_OpenFile(const char *path, const char *mode) { return &file; }
int Stream_Close(Stream *s) { assert(s == &file); ++closed_files; return 0; }
size_t Stream_Read(void *buf, size_t size, size_t count, Stream *s) { return 0; }
int Stream_Eof(Stream *s) { return 0; }
int Stream_Seek(Stream *s, long offset, int origin) { return 0; }
long Stream_Tell(Stream *s) { return 0; }
int Wav_FrameSize(int format, int channels) { return channels * 2; }

static void *create(void) { return &plugin; }
static void destroy(void *instance) { assert(instance == &plugin); }
static int close_plugin(void *instance) { ++closed_plugins; return 0; }
static int set_mute(void *instance, int muted) { return 0; }
static const char *error_string(void *instance, int err) { return "test error"; }
static int open_plugin(void *instance, const BgmPluginIo *io, BgmInfo *info, BgmOpenResult *out)
{
    assert(io->handle == &file);
    out->mode = plugin_mode ? BGM_MODE_PLUGIN : BGM_MODE_DECODED;
    out->format = decoded;
    return 0;
}
static const AudioFormat *in_format(ResamplerSession *s) { return &current; }
static unsigned int in_block(ResamplerSession *s) { return 32; }
static void reset(ResamplerSession *s) {}
static void close_session(ResamplerSession *s) { assert(s == &session); }
static int done(ResamplerSession *s) { return RESAMPLER_DONE; }
static ResamplerSession *begin(Resampler *r, const WavInfo *in)
{
    if (fail_begin)
        return NULL;
    /* This rejects the four-byte shift from aliasing BgmStream.mode on x64. */
    assert(in->format.rate == decoded.rate);
    assert(in->format.format == decoded.format);
    assert(in->format.channels == decoded.channels);
    current = in->format;
    return &session;
}
static int get_format(void *instance, AudioFormat *format)
{
    *format = decoded;
    format->rate = 22050.0;
    return 0;
}

int main(void)
{
    BgmInfo info = {0};
    unsigned char block[128];
    int status = 1;

    strcpy(info.path, "test.mp3");
    info.loop_start = info.loop_end = -1;
    info.volume = 100.0;
    plugin.create = create;
    plugin.destroy = destroy;
    plugin.open = open_plugin;
    plugin.close = close_plugin;
    plugin.set_mute = set_mute;
    plugin.strerror = error_string;
    plugin.get_format = get_format;
    methods.in_format = in_format;
    methods.in_block = in_block;
    methods.reset = reset;
    methods.close = close_session;
    methods.state = done;
    session.vtbl = &methods;
    converter.begin = begin;
    g_bgmResampler = &converter;
    g_bgmStream = &stream;
    g_bgmFormat = decoded;
    stream.block_bytes = sizeof(block);

    assert(Bgm_StreamOpen(&info) == 1);
    assert(stream.mode == BGM_MODE_DECODED);
    assert(stream.format.rate == decoded.rate);
    assert(stream.format.format == decoded.format);
    assert(stream.format.channels == decoded.channels);
    assert(stream.resampler == &session);
    assert(stream.decode_size == 32 * 2 * 2);

    /* Force a format change to request a replacement that cannot be created. */
    stream.playing = 1;
    stream.format_changed = 1;
    fail_begin = 1;
    assert(Bgm_StreamRead(block, &status) == 0);
    assert(status == 0 && stream.playing == 0 && stream.file == NULL);
    assert(stream.resampler == NULL && closed_files == 1);

    /* Initial resampler creation can fail too; close the decoder and file. */
    assert(Bgm_StreamOpen(&info) == 0);
    assert(stream.file == NULL && stream.playing == 0 && stream.resampler == NULL);
    assert(closed_files == 2 && closed_plugins > 0);

    /* Self-playing plugins do not need a resampler at all. */
    plugin_mode = 1;
    assert(Bgm_StreamOpen(&info) == 1);
    assert(stream.mode == BGM_MODE_PLUGIN && stream.file == &file);
    Bgm_StreamClose(&stream);
    free(stream.decode_buf);
    puts("PASS: BGM result layout, resampler failure cleanup, and plugin playback");
    return 0;
}
