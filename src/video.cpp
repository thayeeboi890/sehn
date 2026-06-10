/* video.cpp

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

#include "video.h"
#include "capture.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jpeglib.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct VideoState {
    AVFormatContext  *fmt_ctx;
    AVCodecContext   *codec_ctx;
    AVStream         *stream;
    AVFrame          *frame;
    AVPacket         *pkt;
    SwsContext       *sws_ctx;
    int64_t           pts;
    bool              open;
    struct            timespec start_ts;
    int64_t last_pts;
};

static VideoState vid = {};
bool vid_is_open() { return vid.open; }

// YUYV -> AVFrame (YUV420P)
static void yuyv_to_yuv420(const uint8_t *yuyv, AVFrame *frame,
                             uint32_t width, uint32_t height) {
    uint8_t *y  = frame->data[0];
    uint8_t *u  = frame->data[1];
    uint8_t *v  = frame->data[2];

    for (uint32_t row = 0; row < height; row += 2) {
        for (uint32_t col = 0; col < width; col += 2) {
            size_t i = (row * width + col) * 2;
            y[row       * width + col]     = yuyv[i];
            y[row       * width + col + 1] = yuyv[i + 2];
            y[(row + 1) * width + col]     = yuyv[(i + width * 2)];
            y[(row + 1) * width + col + 1] = yuyv[(i + width * 2 + 2)];
            u[(row / 2) * (width / 2) + col / 2] = yuyv[i + 1];
            v[(row / 2) * (width / 2) + col / 2] = yuyv[i + 3];
        }
    }
}

int video_open(AppState *state, const char *path) { LOG_FN();
    LOG_DEBUG("video: opening output %s", path);
    int ret;

    // allocate output context
    ret = avformat_alloc_output_context2(&vid.fmt_ctx, nullptr,
                                          nullptr, path);
    if (ret < 0 || !vid.fmt_ctx) {
        LOG_ERROR("video: could not allocate format context");
        return -1;
    }

    // find H264 encoder
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR("video: H264 encoder not found");
        return -1;
    }

    // create stream
    vid.stream = avformat_new_stream(vid.fmt_ctx, nullptr);
    if (!vid.stream) {
        LOG_ERROR("video: could not create stream");
        return -1;
    }

    // allocate codec context
    vid.codec_ctx = avcodec_alloc_context3(codec);
    if (!vid.codec_ctx) return -1;

    vid.codec_ctx->width     = (int)state->width;
    vid.codec_ctx->height    = (int)state->height;
    vid.codec_ctx->time_base = { 1, (int)state->framerate };
    vid.codec_ctx->framerate = { (int)state->framerate, 1 };
    vid.codec_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    vid.codec_ctx->bit_rate  = 4000000;
    vid.codec_ctx->gop_size  = 12;

    // needed for some formats
    if (vid.fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        vid.codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // set preset
    av_opt_set(vid.codec_ctx->priv_data, "preset", "fast", 0);
    av_opt_set(vid.codec_ctx->priv_data, "crf",    "23",   0);

    // open codec
    ret = avcodec_open2(vid.codec_ctx, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR("video: could not open codec");
        return -1;
    }

    // copy params to stream
    avcodec_parameters_from_context(vid.stream->codecpar, vid.codec_ctx);
    vid.stream->time_base = vid.codec_ctx->time_base;

    // open output file
    if (!(vid.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&vid.fmt_ctx->pb, path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_ERROR("video: could not open output file");
            return -1;
        }
    }

    // write header
    ret = avformat_write_header(vid.fmt_ctx, nullptr);
    if (ret < 0) {
        LOG_ERROR("video: could not write header");
        return -1;
    }

    // allocate frame
    vid.frame = av_frame_alloc();
    vid.frame->format = AV_PIX_FMT_YUV420P;
    vid.frame->width  = (int)state->width;
    vid.frame->height = (int)state->height;
    av_frame_get_buffer(vid.frame, 32);

    // allocate packet
    vid.pkt = av_packet_alloc();

    // sws context for MJPEG -> YUV420P if needed
    vid.sws_ctx = sws_getContext(
        (int)state->width, (int)state->height, AV_PIX_FMT_RGB24,
        (int)state->width, (int)state->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    vid.pts  = 0;
    vid.last_pts = -1;
    vid.open = true;
    LOG_INFO("recording to %s", path);
    return 0;
}

void video_write_frame(AppState *state, const void *data, size_t size) { LOG_FN();
    if (!vid.open) return;

    av_frame_make_writable(vid.frame);

    if (state->v4l2_format == "yuyv") {
        // direct YUYV -> YUV420P
        yuyv_to_yuv420((const uint8_t *)data, vid.frame,
                        state->width, state->height);
    } else {
        // MJPEG: decode to RGB first then swscale to YUV420P
        uint8_t *rgb = (uint8_t *)malloc(state->width * state->height * 3);

        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, (const uint8_t *)data, (unsigned long)size);
        jpeg_read_header(&cinfo, TRUE);
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);
        uint32_t row_stride = state->width * 3;
        while (cinfo.output_scanline < state->height) {
            uint8_t *row = rgb + cinfo.output_scanline * row_stride;
            jpeg_read_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);

        const uint8_t *src_slices[1] = { rgb };
        int src_stride[1] = { (int)state->width * 3 };
        sws_scale(vid.sws_ctx, src_slices, src_stride,
                  0, (int)state->height,
                  vid.frame->data, vid.frame->linesize);
        free(rgb);
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (vid.last_pts < 0) {
        vid.start_ts = ts;
    }

    int64_t elapsed_ms = (int64_t)(ts.tv_sec - vid.start_ts.tv_sec) * 1000LL +
                         (int64_t)(ts.tv_nsec - vid.start_ts.tv_nsec) / 1000000LL;

// convert elapsed time to codec timebase
vid.frame->pts = av_rescale_q(
    elapsed_ms,
    AVRational{1, 1000},
    vid.codec_ctx->time_base);

// enforce strictly increasing PTS
if (vid.frame->pts <= vid.last_pts)
    vid.frame->pts = vid.last_pts + 1;

vid.last_pts = vid.frame->pts;
vid.pts++;
    // encode
    avcodec_send_frame(vid.codec_ctx, vid.frame);
    while (avcodec_receive_packet(vid.codec_ctx, vid.pkt) == 0) {
        av_packet_rescale_ts(vid.pkt, vid.codec_ctx->time_base,
                             vid.stream->time_base);
        vid.pkt->stream_index = vid.stream->index;
        av_interleaved_write_frame(vid.fmt_ctx, vid.pkt);
        av_packet_unref(vid.pkt);
    }
}

void video_close(AppState *state) { LOG_FN();
    (void)state;
    if (!vid.open) return;

    // flush encoder
    avcodec_send_frame(vid.codec_ctx, nullptr);
    while (avcodec_receive_packet(vid.codec_ctx, vid.pkt) == 0) {
        av_packet_rescale_ts(vid.pkt, vid.codec_ctx->time_base,
                             vid.stream->time_base);
        vid.pkt->stream_index = vid.stream->index;
        av_interleaved_write_frame(vid.fmt_ctx, vid.pkt);
        av_packet_unref(vid.pkt);
    }

    av_write_trailer(vid.fmt_ctx);
    avcodec_free_context(&vid.codec_ctx);
    av_frame_free(&vid.frame);
    av_packet_free(&vid.pkt);
    sws_freeContext(vid.sws_ctx);
    if (!(vid.fmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&vid.fmt_ctx->pb);
    avformat_free_context(vid.fmt_ctx);
    vid = {};
    LOG_INFO("recording stopped");
}
