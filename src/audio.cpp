/* audio.cpp

Copyright (C) 2026 Santiago Silva.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies of the Software and its documentation and acknowledgment shall be
given in the documentation and software packages that this Software was
used.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "audio.h"
#include "video.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <pulse/simple.h>
#include <pulse/error.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

// ── constants ────────────────────────────────────────────────────────────────

#define AUDIO_SAMPLE_RATE   48000
#define AUDIO_CHANNELS      2
#define AUDIO_CHUNK_FRAMES  1024   // frames per PulseAudio read

// ── state ────────────────────────────────────────────────────────────────────

struct AudioState {
    pa_simple        *pa;           // PulseAudio simple API handle
    AVCodecContext   *codec_ctx;
    AVStream         *stream;
    AVFrame          *frame;
    AVPacket         *pkt;
    SwrContext       *swr;          // resampler (s16 interleaved -> fltp)
    pthread_t         thread;
    volatile bool     running;
    bool              open;
    int64_t           pts;
};

static AudioState aud = {};

// ── capture thread ────────────────────────────────────────────────────────────

static void *audio_thread_func(void *) {
    // raw PCM buffer from PulseAudio: s16le stereo
    int16_t pcm[AUDIO_CHUNK_FRAMES * AUDIO_CHANNELS];
    int pa_err = 0;

    while (aud.running) {
        // read one chunk from PulseAudio
        int bytes = (int)(sizeof(int16_t) * AUDIO_CHANNELS * AUDIO_CHUNK_FRAMES);
        if (pa_simple_read(aud.pa, pcm, bytes, &pa_err) < 0) {
            LOG_WARN("audio: pa_simple_read: %s", pa_strerror(pa_err));
            continue;
        }

        if (!aud.running) break;

        AVFormatContext *fmt_ctx = vid_get_fmt_ctx();
        if (!fmt_ctx || !vid_is_open()) continue;

        // fill AVFrame with converted samples
        av_frame_unref(aud.frame);
        aud.frame->nb_samples     = AUDIO_CHUNK_FRAMES;
        aud.frame->format         = AV_SAMPLE_FMT_FLTP;
        aud.frame->sample_rate    = AUDIO_SAMPLE_RATE;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_default(&aud.frame->ch_layout, AUDIO_CHANNELS);
#else
        aud.frame->channel_layout = AV_CH_LAYOUT_STEREO;
        aud.frame->channels       = AUDIO_CHANNELS;
#endif
        av_frame_get_buffer(aud.frame, 0);

        // convert s16 interleaved -> fltp planar
        const uint8_t *in_data[1] = { (const uint8_t *)pcm };
        int converted = swr_convert(aud.swr,
                                    aud.frame->data,
                                    AUDIO_CHUNK_FRAMES,
                                    in_data,
                                    AUDIO_CHUNK_FRAMES);
        if (converted < 0) continue;

        aud.frame->pts = aud.pts;
        aud.pts += converted;

        // encode
        if (avcodec_send_frame(aud.codec_ctx, aud.frame) < 0) continue;

        while (avcodec_receive_packet(aud.codec_ctx, aud.pkt) == 0) {
            av_packet_rescale_ts(aud.pkt,
                                 aud.codec_ctx->time_base,
                                 aud.stream->time_base);
            aud.pkt->stream_index = aud.stream->index;
            vid_write_packet(aud.pkt);
            av_packet_unref(aud.pkt);
        }
    }
    return nullptr;
}

// ── public api ────────────────────────────────────────────────────────────────

int audio_open(AppState *state) {
    (void)state;

    AVFormatContext *fmt_ctx = vid_get_fmt_ctx();
    if (!fmt_ctx) {
        LOG_ERROR("audio: video not open, cannot add audio stream");
        return -1;
    }

    // ── PulseAudio ───────────────────────────────────────────────────────────
    pa_sample_spec ss = {};
    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = AUDIO_SAMPLE_RATE;
    ss.channels = AUDIO_CHANNELS;

    int pa_err = 0;
    aud.pa = pa_simple_new(nullptr,       // server
                           "sehn",        // app name
                           PA_STREAM_RECORD,
                           nullptr,       // default source
                           "capture",     // stream name
                           &ss,
                           nullptr,       // channel map
                           nullptr,       // buffering attrs
                           &pa_err);
    if (!aud.pa) {
        LOG_ERROR("audio: pa_simple_new: %s", pa_strerror(pa_err));
        return -1;
    }

    // ── AAC encoder ─────────────────────────────────────────────────────────
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        LOG_ERROR("audio: AAC encoder not found");
        pa_simple_free(aud.pa);
        return -1;
    }

    aud.stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!aud.stream) {
        pa_simple_free(aud.pa);
        return -1;
    }

    aud.codec_ctx = avcodec_alloc_context3(codec);
    if (!aud.codec_ctx) {
        pa_simple_free(aud.pa);
        return -1;
    }

    aud.codec_ctx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    aud.codec_ctx->sample_rate = AUDIO_SAMPLE_RATE;
    aud.codec_ctx->bit_rate    = 128000;
    aud.codec_ctx->time_base   = { 1, AUDIO_SAMPLE_RATE };

#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_default(&aud.codec_ctx->ch_layout, AUDIO_CHANNELS);
#else
    aud.codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    aud.codec_ctx->channels       = AUDIO_CHANNELS;
#endif

    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        aud.codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(aud.codec_ctx, codec, nullptr) < 0) {
        LOG_ERROR("audio: could not open AAC encoder");
        pa_simple_free(aud.pa);
        avcodec_free_context(&aud.codec_ctx);
        return -1;
    }

    avcodec_parameters_from_context(aud.stream->codecpar, aud.codec_ctx);
    aud.stream->time_base = aud.codec_ctx->time_base;

    // ── swresample ───────────────────────────────────────────────────────────
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&aud.swr,
                        &stereo, AV_SAMPLE_FMT_FLTP, AUDIO_SAMPLE_RATE,
                        &stereo, AV_SAMPLE_FMT_S16,  AUDIO_SAMPLE_RATE,
                        0, nullptr);
#else
    aud.swr = swr_alloc_set_opts(nullptr,
                                  AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, AUDIO_SAMPLE_RATE,
                                  AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16,  AUDIO_SAMPLE_RATE,
                                  0, nullptr);
#endif
    if (!aud.swr || swr_init(aud.swr) < 0) {
        LOG_ERROR("audio: swr_init failed");
        pa_simple_free(aud.pa);
        avcodec_free_context(&aud.codec_ctx);
        return -1;
    }

    aud.frame = av_frame_alloc();
    aud.pkt   = av_packet_alloc();
    aud.pts   = 0;
    aud.open  = true;

    LOG_INFO("audio: ready at %d Hz stereo AAC", AUDIO_SAMPLE_RATE);
    return 0;
}

int audio_start(AppState *state) {
    (void)state;
    if (!aud.open || aud.running)
        return -1;

    aud.running = true;
    if (pthread_create(&aud.thread, nullptr, audio_thread_func, nullptr) != 0) {
        aud.running = false;
        LOG_ERROR("audio: could not start capture thread");
        return -1;
    }

    LOG_INFO("audio: capturing at %d Hz stereo AAC", AUDIO_SAMPLE_RATE);
    return 0;
}

void audio_close(AppState *state) {
    (void)state;
    if (!aud.open) return;

    if (aud.running) {
        aud.running = false;
        pthread_join(aud.thread, nullptr);
    }

    // flush encoder
    AVFormatContext *fmt_ctx = vid_get_fmt_ctx();
    if (fmt_ctx && vid_is_open()) {
        avcodec_send_frame(aud.codec_ctx, nullptr);
        while (avcodec_receive_packet(aud.codec_ctx, aud.pkt) == 0) {
            av_packet_rescale_ts(aud.pkt,
                                 aud.codec_ctx->time_base,
                                 aud.stream->time_base);
            aud.pkt->stream_index = aud.stream->index;
            vid_write_packet(aud.pkt);
            av_packet_unref(aud.pkt);
        }
    }

    pa_simple_free(aud.pa);
    avcodec_free_context(&aud.codec_ctx);
    swr_free(&aud.swr);
    av_frame_free(&aud.frame);
    av_packet_free(&aud.pkt);

    aud = {};
    LOG_INFO("audio: stopped");
}

bool audio_is_open() { return aud.open; }
