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

#include "camera.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);
    return r;
}

int camera_open(AppState *state) {
    (void)state;
    // TODO: open device, VIDIOC_QUERYCAP, camera_negotiate, VIDIOC_REQBUFS, mmap, VIDIOC_QBUF
    return -1;
}

int camera_negotiate(AppState *state) {
    (void)state;
    // TODO: VIDIOC_ENUM_FMT, VIDIOC_S_FMT, VIDIOC_G_FMT, VIDIOC_S_PARM
    return -1;
}

int camera_start(AppState *state) {
    (void)state;
    // TODO: VIDIOC_STREAMON
    return -1;
}

const void *camera_next_frame(AppState *state, size_t *out_size) {
    (void)state;
    (void)out_size;
    // TODO: poll, VIDIOC_DQBUF, VIDIOC_QBUF
    return nullptr;
}

void camera_stop(AppState *state) {
    (void)state;
    // TODO: VIDIOC_STREAMOFF
}

void camera_close(AppState *state) {
    (void)state;
    // TODO: camera_stop, munmap, close, reset camera_fd = -1
}

void camera_apply_controls(AppState *state) {
    (void)state;
    // TODO: VIDIOC_QUERYCTRL + VIDIOC_S_CTRL for each control in state
}
