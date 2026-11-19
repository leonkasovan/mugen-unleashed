/* mugen-unleashed: bounded background-video decoding off the render thread. */
#ifdef MUGEN_VIDEO_BG
#include "video_bg.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

enum { FRAME_COUNT = 3 };

typedef struct {
    AVFrame *frame;
    int64_t pts;
} VideoFrame;

struct VideoBg {
    AVFormatContext *input;
    AVCodecContext *decoder;
    int stream, loop;
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    int started, stop;
    VideoFrame queue[FRAME_COUNT];
    unsigned head, count;
    AVFrame *current;
    struct SwsContext *scaler;
    uint8_t *rgba;
    int rgba_width, rgba_height;
    unsigned serial, converted;
};

static int QueueFrame(VideoBg *video, AVFrame *frame, int64_t pts)
{
    pthread_mutex_lock(&video->mutex);
    while (video->count == FRAME_COUNT && !video->stop)
        pthread_cond_wait(&video->ready, &video->mutex);
    if (video->stop) {
        pthread_mutex_unlock(&video->mutex);
        return 0;
    }
    VideoFrame *slot = &video->queue[(video->head + video->count) % FRAME_COUNT];
    av_frame_move_ref(slot->frame, frame);
    slot->pts = pts;
    ++video->count;
    pthread_mutex_unlock(&video->mutex);
    return 1;
}

static void *DecodeVideo(void *arg)
{
    VideoBg *video = arg;
    AVStream *stream = video->input->streams[video->stream];
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVRational rate = av_guess_frame_rate(video->input, stream, NULL);
    int64_t step = rate.num > 0 ? av_rescale_q(1, av_inv_q(rate), AV_TIME_BASE_Q) : 16667;
    if (step < 1)
        step = 1;
    int64_t base = AV_NOPTS_VALUE, offset = 0, next = 0;
    int eof = 0, have_packet = 0;

    if (!packet || !frame)
        goto done;
    for (;;) {
        int result = avcodec_receive_frame(video->decoder, frame);
        if (result == 0) {
            int64_t pts = frame->best_effort_timestamp;
            int64_t due = next;
            if (pts != AV_NOPTS_VALUE) {
                if (base == AV_NOPTS_VALUE)
                    base = pts;
                due = offset + av_rescale_q(pts - base, stream->time_base, AV_TIME_BASE_Q);
                if (due < next - step)
                    due = next;
            }
            next = due + step;
            if (!QueueFrame(video, frame, due))
                break;
            continue;
        }
        if (result == AVERROR_EOF && video->loop && next > offset &&
            av_seek_frame(video->input, video->stream,
                          stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time,
                          AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(video->decoder);
            offset = next;
            base = AV_NOPTS_VALUE;
            eof = 0;
            continue;
        }
        if (result != AVERROR(EAGAIN) || eof)
            break;
        if (!have_packet) {
            while ((result = av_read_frame(video->input, packet)) >= 0) {
                if (packet->stream_index == video->stream) {
                    have_packet = 1;
                    break;
                }
                av_packet_unref(packet);
            }
            if (result < 0 && result != AVERROR_EOF)
                break;
        }
        result = avcodec_send_packet(video->decoder, have_packet ? packet : NULL);
        if (result < 0)
            break;
        eof = !have_packet;
        have_packet = 0;
        av_packet_unref(packet);
    }

done:
    av_frame_free(&frame);
    av_packet_free(&packet);
    return NULL;
}

VideoBg *VideoBg_Open(const char *path, int loop)
{
    VideoBg *video = calloc(1, sizeof(*video));
    const AVCodec *codec = NULL;
    if (!video)
        return NULL;
    if (pthread_mutex_init(&video->mutex, NULL) != 0) {
        free(video);
        return NULL;
    }
    if (pthread_cond_init(&video->ready, NULL) != 0) {
        pthread_mutex_destroy(&video->mutex);
        free(video);
        return NULL;
    }
    video->loop = loop;
    if (avformat_open_input(&video->input, path, av_find_input_format("matroska"), NULL) < 0 ||
        avformat_find_stream_info(video->input, NULL) < 0)
        goto fail;
    video->stream = av_find_best_stream(video->input, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (video->stream < 0)
        goto fail;
    video->decoder = avcodec_alloc_context3(codec);
    if (!video->decoder ||
        avcodec_parameters_to_context(video->decoder, video->input->streams[video->stream]->codecpar) < 0)
        goto fail;
    video->decoder->thread_count = 2;
    video->decoder->max_pixels = 3840 * 2160;
    if (avcodec_open2(video->decoder, codec, NULL) < 0)
        goto fail;
    video->current = av_frame_alloc();
    if (!video->current)
        goto fail;
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        video->queue[i].frame = av_frame_alloc();
        if (!video->queue[i].frame)
            goto fail;
    }
    return video;

fail:
    VideoBg_Close(video);
    return NULL;
}

static void StopVideo(VideoBg *video)
{
    if (!video->started)
        return;
    pthread_mutex_lock(&video->mutex);
    video->stop = 1;
    pthread_cond_signal(&video->ready);
    pthread_mutex_unlock(&video->mutex);
    pthread_join(video->worker, NULL);
    video->started = 0;
}

void VideoBg_Reset(VideoBg *video)
{
    if (!video->started)
        return;
    StopVideo(video);
    AVStream *stream = video->input->streams[video->stream];
    int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if (av_seek_frame(video->input, video->stream, start, AVSEEK_FLAG_BACKWARD) < 0)
        return;
    avcodec_flush_buffers(video->decoder);
    for (unsigned i = 0; i < FRAME_COUNT; ++i)
        av_frame_unref(video->queue[i].frame);
    av_frame_unref(video->current);
    video->head = video->count = 0;
    video->stop = 0;
}

void VideoBg_Close(VideoBg *video)
{
    if (!video)
        return;
    StopVideo(video);
    for (unsigned i = 0; i < FRAME_COUNT; ++i)
        av_frame_free(&video->queue[i].frame);
    av_frame_free(&video->current);
    avcodec_free_context(&video->decoder);
    avformat_close_input(&video->input);
    sws_freeContext(video->scaler);
    av_free(video->rgba);
    pthread_cond_destroy(&video->ready);
    pthread_mutex_destroy(&video->mutex);
    free(video);
}

const uint8_t *VideoBg_Frame(VideoBg *video, int64_t time_us, int width, int height,
                             int *frame_width, int *frame_height, unsigned *serial)
{
    if (video->stop || width <= 0 || height <= 0)
        return NULL;
    if (!video->started) {
        if (pthread_create(&video->worker, NULL, DecodeVideo, video) != 0)
            return NULL;
        video->started = 1;
    }
    pthread_mutex_lock(&video->mutex);
    while (video->count && video->queue[video->head].pts <= time_us) {
        av_frame_unref(video->current);
        av_frame_move_ref(video->current, video->queue[video->head].frame);
        video->head = (video->head + 1) % FRAME_COUNT;
        --video->count;
        ++video->serial;
    }
    pthread_cond_signal(&video->ready);
    pthread_mutex_unlock(&video->mutex);

    const AVFrame *frame = video->current;
    if (!frame->data[0])
        return NULL;
    AVRational sar = frame->sample_aspect_ratio;
    double aspect = (double)frame->width / frame->height;
    if (sar.num > 0 && sar.den > 0)
        aspect *= av_q2d(sar);
    if ((double)width / height > aspect)
        width = (int)(height * aspect + 0.5);
    else
        height = (int)(width / aspect + 0.5);
    if (width < 1 || height < 1)
        return NULL;

    if (width != video->rgba_width || height != video->rgba_height) {
        int bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, 1);
        if (bytes < 0)
            return NULL;
        uint8_t *pixels = av_realloc(video->rgba, (size_t)bytes + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!pixels)
            return NULL;
        video->rgba = pixels;
        video->rgba_width = width;
        video->rgba_height = height;
        video->converted = video->serial - 1;
    }
    if (video->converted != video->serial) {
        video->scaler = sws_getCachedContext(video->scaler, frame->width, frame->height, frame->format,
                                            width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
        uint8_t *planes[] = {video->rgba, NULL, NULL, NULL};
        int strides[] = {width * 4, 0, 0, 0};
        if (!video->scaler ||
            sws_scale(video->scaler, (const uint8_t *const *)frame->data, frame->linesize,
                      0, frame->height, planes, strides) != height)
            return NULL;
        video->converted = video->serial;
    }
    *frame_width = width;
    *frame_height = height;
    *serial = video->serial;
    return video->rgba;
}
#endif
