/* capture.cpp

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

#include "capture.h"
#include "camera.h"
#include "exif.h"
#include "utils.h"
#include "video.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <sys/stat.h>
#include <unistd.h>

#include <jpeglib.h>
#include <png.h>

#ifdef HAVE_LIBEXIF
#include "utils.h"
#include <libexif/exif-data.h>
#endif

// session capture counter, resets each run
static int session_counter = 0;

// ── filename builder ──────────────────────────────────────────────────────────

void capture_build_filename(AppState* state, char* buf, size_t buf_len, const char* ext)
{
    LOG_FN();
    // expand strftime tokens first
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);

    char time_part[256];
    strftime(time_part, sizeof(time_part), state->filename_pattern.c_str(), tm);

    // replace %c with zero-padded session counter
    char with_counter[512];
    char counter_str[16];
    snprintf(counter_str, sizeof(counter_str), "%04d", session_counter++);
    const char* p = time_part;
    char* out = with_counter;
    while (*p && out < with_counter + sizeof(with_counter) - 1) {
        if (p[0] == '#' && p[1] == '#') {
            const char* c = counter_str;
            while (*c)
                *out++ = *c++;
            p += 2;
        }
        else {
            *out++ = *p++;
        }
    }
    *out = '\0';

    snprintf(buf, buf_len, "%s/%s.%s", state->output_dir.c_str(), with_counter, ext);
}

// ── JPEG save ────────────────────────────────────────────────────────────────

static int save_jpeg(AppState* state, const uint8_t* rgb, uint32_t width, uint32_t height)
{
    LOG_FN();
    char path[1024];
    capture_build_filename(state, path, sizeof(path), "jpg");

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, state->jpeg_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    uint32_t row_stride = width * 3;
    while (cinfo.next_scanline < height) {
        const uint8_t* row = rgb + cinfo.next_scanline * row_stride;
        jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(fp);
#ifdef HAVE_LIBEXIF
    exif_write(state, path);
#endif
    LOG_INFO("saved %s", path);
    return 0;
}

// ── PNG save ─────────────────────────────────────────────────────────────────

static int save_png(AppState* state, const uint8_t* rgb, uint32_t width, uint32_t height)
{
    LOG_FN();
    char path[1024];
    capture_build_filename(state, path, sizeof(path), "png");

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        fclose(fp);
#ifdef HAVE_LIBEXIF
        exif_write(state, path);
#endif
        return -1;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png, state->png_compression);
    png_write_info(png, info);

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = rgb + y * width * 3;
        png_write_row(png, row);
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    LOG_INFO("saved %s", path);
    return 0;
}

// ── MJPEG decode to RGB ───────────────────────────────────────────────────────

static uint8_t* mjpeg_decode(const void* data, size_t len, uint32_t width, uint32_t height)
{
    uint8_t* rgb = (uint8_t*)malloc(width * height * 3);

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (const uint8_t*)data, (unsigned long)len);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    uint32_t row_stride = width * 3;
    while (cinfo.output_scanline < height) {
        uint8_t* row = rgb + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return rgb;
}

// ── YUYV decode to RGB ───────────────────────────────────────────────────────

static uint8_t* yuyv_decode(const void* data, uint32_t width, uint32_t height)
{
    uint8_t* rgb = (uint8_t*)malloc(width * height * 3);
    const uint8_t* yuyv = (const uint8_t*)data;

    auto clamp = [](int x) -> uint8_t { return x < 0 ? 0 : x > 255 ? 255 : (uint8_t)x; };

    size_t n = (size_t)width * height / 2;
    uint8_t* out = rgb;
    for (size_t i = 0; i < n; i++) {
        int y0 = yuyv[0], u = yuyv[1], y1 = yuyv[2], v = yuyv[3];
        yuyv += 4;
        int c, d, e;
        c = y0 - 16;
        d = u - 128;
        e = v - 128;
        *out++ = clamp((298 * c + 409 * e + 128) >> 8);
        *out++ = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        *out++ = clamp((298 * c + 516 * d + 128) >> 8);
        c = y1 - 16;
        *out++ = clamp((298 * c + 409 * e + 128) >> 8);
        *out++ = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        *out++ = clamp((298 * c + 516 * d + 128) >> 8);
    }
    return rgb;
}

// ── public api ────────────────────────────────────────────────────────────────

int capture_photo(AppState* state, const void* frame, size_t frame_size)
{
    LOG_FN();
    uint8_t* rgb;

    if (state->v4l2_format == "mjpeg")
        rgb = mjpeg_decode(frame, frame_size, state->width, state->height);
    else
        rgb = yuyv_decode(frame, state->width, state->height);

    int ret;
    if (state->save_format == "png")
        ret = save_png(state, rgb, state->width, state->height);
    else
        ret = save_jpeg(state, rgb, state->width, state->height);

    free(rgb);
    return ret;
}

void capture_burst(AppState* state)
{
    LOG_FN();
    LOG_INFO("burst start (%d frames, %dms interval)", state->burst_count,
             state->burst_interval_ms);

    for (int i = 0; i < state->burst_count; i++) {
        size_t frame_size = 0;
        const void* frame = camera_next_frame(state, &frame_size);
        if (!frame) {
            LOG_WARN("burst frame %d failed", i);
            continue;
        }
        capture_photo(state, frame, frame_size);
        usleep((useconds_t)state->burst_interval_ms * 1000);
    }

    LOG_INFO("burst done");
}

int capture_video_start(AppState* state)
{
    LOG_FN();
    char path[1024];
    capture_build_filename(state, path, sizeof(path), state->video_format.c_str());
    return video_open(state, path);
}

void capture_video_frame(AppState* state, const void* frame, size_t frame_size)
{
    video_write_frame(state, frame, frame_size);
}

void capture_video_stop(AppState* state) { video_close(state); }
