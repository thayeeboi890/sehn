/* camera.cpp

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

#include "camera.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "utils.h"

// one CameraState per app, lives here
static CameraState cam = {};

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);
    return r;
}

int camera_negotiate(AppState *state) { LOG_FN();
    struct v4l2_format fmt = {};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = state->width;
    fmt.fmt.pix.height      = state->height;

    // prefer MJPEG, fall back to YUYV
    if (state->v4l2_format == "mjpeg")
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    else
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(cam.fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    // read back what the driver actually accepted
    state->width       = fmt.fmt.pix.width;
    state->height      = fmt.fmt.pix.height;
    cam.fmt            = fmt;

    // set framerate
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = state->framerate;
    xioctl(cam.fd, VIDIOC_S_PARM, &parm); // non-fatal if unsupported

    if (state->verbose)
        LOG_DEBUG("negotiated %ux%u @ %u fps", state->width, state->height, state->framerate);

    return 0;
}

int camera_open(AppState *state) { LOG_FN();
    LOG_DEBUG("opening camera device %s", state->device.c_str());
    // open device
    cam.fd = open(state->device.c_str(), O_RDWR | O_NONBLOCK);
    if (cam.fd < 0) {
        perror("open");
        return -1;
    }

    // check it's a capture device
    struct v4l2_capability cap = {};
    if (xioctl(cam.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(cam.fd);
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_ERROR("%s is not a capture device", state->device.c_str());
        close(cam.fd);
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOG_ERROR("%s does not support streaming", state->device.c_str());
        close(cam.fd);
        return -1;
    }

    // negotiate format
    if (camera_negotiate(state) < 0) {
        close(cam.fd);
        return -1;
    }

    // request mmap buffers
    struct v4l2_requestbuffers req = {};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(cam.fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(cam.fd);
        return -1;
    }

    // mmap each buffer
    cam.buffers  = (CameraBuffer *)calloc(req.count, sizeof(CameraBuffer));
    cam.n_buffers = req.count;

    for (unsigned i = 0; i < cam.n_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(cam.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(cam.fd);
            return -1;
        }
        cam.buffers[i].length = buf.length;
        cam.buffers[i].start  = mmap(nullptr, buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, cam.fd, buf.m.offset);
        if (cam.buffers[i].start == MAP_FAILED) {
            perror("mmap");
            close(cam.fd);
            return -1;
        }
    }

    // enqueue all buffers
    for (unsigned i = 0; i < cam.n_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    state->camera_fd = cam.fd;
    return 0;
}

int camera_start(AppState *state) { LOG_FN();
    (void)state;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(cam.fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    cam.streaming = true;
    return 0;
}

const void *camera_next_frame(AppState *state, size_t *out_size) { LOG_FN();
    (void)state;

    // wait for a frame with 2s timeout
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(cam.fd, &fds);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };

    int r = select(cam.fd + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0) {
        if (r == 0) LOG_WARN("frame timeout");
        else        perror("select");
        return nullptr;
    }

    struct v4l2_buffer buf = {};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return nullptr;
    }

    *out_size = buf.bytesused;
    const void *data = cam.buffers[buf.index].start;

    // re-enqueue immediately
    if (xioctl(cam.fd, VIDIOC_QBUF, &buf) < 0)
        perror("VIDIOC_QBUF");

    return data;
}

void camera_stop(AppState *state) {
    (void)state;
    if (!cam.streaming) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam.fd, VIDIOC_STREAMOFF, &type);
    cam.streaming = false;
}

void camera_close(AppState *state) { LOG_FN();
    camera_stop(state);
    for (unsigned i = 0; i < cam.n_buffers; i++)
        munmap(cam.buffers[i].start, cam.buffers[i].length);
    free(cam.buffers);
    cam.buffers   = nullptr;
    cam.n_buffers = 0;
    close(cam.fd);
    cam.fd           = -1;
    state->camera_fd = -1;
}

void camera_apply_controls(AppState *state) {
    // TODO: VIDIOC_QUERYCTRL + VIDIOC_S_CTRL for each control in state
    (void)state;
}
