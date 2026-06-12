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
#include "utils.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

// one CameraState per app, lives here
static CameraState cam = {};

// detected PTZ control info (populated at open)
struct ControlInfo {
    int id;
    long minimum;
    long maximum;
    long step;
    bool valid;
};
static ControlInfo pan_ctrl = {0, 0, 0, 0, false};
static ControlInfo tilt_ctrl = {0, 0, 0, 0, false};
static ControlInfo zoom_ctrl = {0, 0, 0, 0, false};
static ControlInfo exposure_ctrl = {0, 0, 0, 0, false};
static ControlInfo exposure_auto_ctrl = {0, 0, 0, 0, false};
static ControlInfo gain_ctrl = {0, 0, 0, 0, false};
static ControlInfo wb_ctrl = {0, 0, 0, 0, false};
static ControlInfo autofocus_ctrl = {0, 0, 0, 0, false};

// cache last set values to avoid hammering device and permission errors
static long last_exposure_val = LONG_MIN;
static long last_gain_val = LONG_MIN;
static long last_wb_val = LONG_MIN;
static int last_autofocus_val = INT_MIN;

// last UI values we synced to hardware
static int last_pan_x = 0;
static int last_pan_y = 0;
static float last_zoom = 1.0f;

// case-insensitive substring search
static bool contains_ci(const char* s, const char* sub)
{
    if (!s || !sub)
        return false;
    for (; *s; s++) {
        const char *p = s, *q = sub;
        while (*p && *q && (tolower((unsigned char)*p) == tolower((unsigned char)*q))) {
            p++;
            q++;
        }
        if (!*q)
            return true;
    }
    return false;
}

static int xioctl(int fd, unsigned long request, void* arg);

static void camera_probe_controls(int fd)
{
    if (fd < 0)
        return;
    struct v4l2_query_ext_ctrl qc = {};
    qc.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

    while (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qc) == 0) {
        if (qc.flags & V4L2_CTRL_FLAG_DISABLED) {
            qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
            continue;
        }
        if (qc.type == V4L2_CTRL_TYPE_INTEGER || qc.type == V4L2_CTRL_TYPE_BOOLEAN ||
            qc.type == V4L2_CTRL_TYPE_MENU) {

            if (qc.id == V4L2_CID_PAN_ABSOLUTE) {
                pan_ctrl.id = qc.id;
                pan_ctrl.minimum = qc.minimum;
                pan_ctrl.maximum = qc.maximum;
                pan_ctrl.step = qc.step;
                pan_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_TILT_ABSOLUTE) {
                tilt_ctrl.id = qc.id;
                tilt_ctrl.minimum = qc.minimum;
                tilt_ctrl.maximum = qc.maximum;
                tilt_ctrl.step = qc.step;
                tilt_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_ZOOM_ABSOLUTE) {
                zoom_ctrl.id = qc.id;
                zoom_ctrl.minimum = qc.minimum;
                zoom_ctrl.maximum = qc.maximum;
                zoom_ctrl.step = qc.step;
                zoom_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_EXPOSURE_ABSOLUTE) {
                exposure_ctrl.id = qc.id;
                exposure_ctrl.minimum = qc.minimum;
                exposure_ctrl.maximum = qc.maximum;
                exposure_ctrl.step = qc.step;
                exposure_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_EXPOSURE_AUTO) {
                exposure_auto_ctrl.id = qc.id;
                exposure_auto_ctrl.minimum = qc.minimum;
                exposure_auto_ctrl.maximum = qc.maximum;
                exposure_auto_ctrl.step = qc.step;
                exposure_auto_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_GAIN) {
                gain_ctrl.id = qc.id;
                gain_ctrl.minimum = qc.minimum;
                gain_ctrl.maximum = qc.maximum;
                gain_ctrl.step = qc.step;
                gain_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_WHITE_BALANCE_TEMPERATURE) {
                wb_ctrl.id = qc.id;
                wb_ctrl.minimum = qc.minimum;
                wb_ctrl.maximum = qc.maximum;
                wb_ctrl.step = qc.step;
                wb_ctrl.valid = true;
            }
            else if (qc.id == V4L2_CID_FOCUS_AUTO) {
                autofocus_ctrl.id = qc.id;
                autofocus_ctrl.minimum = qc.minimum;
                autofocus_ctrl.maximum = qc.maximum;
                autofocus_ctrl.step = qc.step;
                autofocus_ctrl.valid = true;
            }        }

        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    }
}

static int xioctl(int fd, unsigned long request, void* arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int camera_negotiate(AppState* state)
{
    LOG_FN();
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = state->width;
    fmt.fmt.pix.height = state->height;

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
    state->width = fmt.fmt.pix.width;
    state->height = fmt.fmt.pix.height;
    cam.fmt = fmt;

    // set framerate
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = state->framerate;
    xioctl(cam.fd, VIDIOC_S_PARM, &parm); // non-fatal if unsupported

    if (state->verbose)
        LOG_DEBUG("negotiated %ux%u @ %u fps", state->width, state->height, state->framerate);

    return 0;
}

int camera_open(AppState* state)
{
    LOG_FN();
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
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(cam.fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(cam.fd);
        return -1;
    }

    // mmap each buffer
    cam.buffers = (CameraBuffer*)calloc(req.count, sizeof(CameraBuffer));
    cam.n_buffers = req.count;

    for (unsigned i = 0; i < cam.n_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(cam.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(cam.fd);
            return -1;
        }
        cam.buffers[i].length = buf.length;
        cam.buffers[i].start =
            mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam.fd, buf.m.offset);
        if (cam.buffers[i].start == MAP_FAILED) {
            perror("mmap");
            close(cam.fd);
            return -1;
        }
    }

    // enqueue all buffers
    for (unsigned i = 0; i < cam.n_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    state->camera_fd = cam.fd;
    // probe for PTZ controls once camera is opened
    camera_probe_controls(cam.fd);

    // if camera supports zoom, set to fully zoomed out (minimum)
    if (zoom_ctrl.valid) {
        struct v4l2_control ctrl = {};
        ctrl.id = zoom_ctrl.id;
        ctrl.value = (int)zoom_ctrl.minimum;
        if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0) {
            perror("VIDIOC_S_CTRL zoom init");
        }
    }

    return 0;
}

int camera_start(AppState* state)
{
    LOG_FN();
    (void)state;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(cam.fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    cam.streaming = true;
    return 0;
}

const void* camera_next_frame(AppState* state, size_t* out_size)
{
    LOG_FN();
    (void)state;

    // wait for a frame with 2s timeout
    // poll with short timeout so signals don't kill us
    struct pollfd pfd = { cam.fd, POLLIN, 0 };
    int r;
    do {
        r = poll(&pfd, 1, 500);  // 500ms timeout
    } while (r < 0 && errno == EINTR);

    if (r < 0 || r == 0) return nullptr;

    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN)
            return nullptr;
        perror("VIDIOC_DQBUF");
        return nullptr;
    }
    *out_size = buf.bytesused;
    const void* data = cam.buffers[buf.index].start;

    // re-enqueue immediately
    if (xioctl(cam.fd, VIDIOC_QBUF, &buf) < 0)
        perror("VIDIOC_QBUF");

    return data;
}

void camera_stop(AppState* state)
{
    (void)state;
    if (!cam.streaming)
        return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam.fd, VIDIOC_STREAMOFF, &type);
    cam.streaming = false;
}

void camera_close(AppState* state)
{
    LOG_FN();
    camera_stop(state);
    for (unsigned i = 0; i < cam.n_buffers; i++)
        munmap(cam.buffers[i].start, cam.buffers[i].length);
    free(cam.buffers);
    cam.buffers = nullptr;
    cam.n_buffers = 0;
    close(cam.fd);
    cam.fd = -1;
    state->camera_fd = -1;
}

bool camera_has_pan() { return pan_ctrl.valid; }
bool camera_has_tilt() { return tilt_ctrl.valid; }
bool camera_has_zoom() { return zoom_ctrl.valid; }

void camera_pan_rel(int dx, int dy)
{
    if (cam.fd < 0)
        return;
    time_t now = time(nullptr);

    // horizontal
    if (dx != 0 && pan_ctrl.valid) {
        struct v4l2_control ctrl = {};
        ctrl.id = pan_ctrl.id;
        if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
            long cur = ctrl.value;
            long step = pan_ctrl.step ? pan_ctrl.step : (pan_ctrl.maximum - pan_ctrl.minimum) / 20;
            long nv = cur + dx * step; // dx is ±1 per press
            if (nv < pan_ctrl.minimum)
                nv = pan_ctrl.minimum;
            if (nv > pan_ctrl.maximum)
                nv = pan_ctrl.maximum;
            ctrl.value = (int)nv;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                perror("VIDIOC_S_CTRL pan_rel");
        }
    }

    // vertical (invert so Up moves camera up visually)
    if (dy != 0 && tilt_ctrl.valid) {
        struct v4l2_control ctrl = {};
        ctrl.id = tilt_ctrl.id;
        if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
            long cur = ctrl.value;
            long step =
                tilt_ctrl.step ? tilt_ctrl.step : (tilt_ctrl.maximum - tilt_ctrl.minimum) / 20;
            long nv = cur - dy * step; // note inversion
            if (nv < tilt_ctrl.minimum)
                nv = tilt_ctrl.minimum;
            if (nv > tilt_ctrl.maximum)
                nv = tilt_ctrl.maximum;
            ctrl.value = (int)nv;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                perror("VIDIOC_S_CTRL tilt_rel");
        }
    }
}

void camera_zoom_rel(float delta)
{
    if (cam.fd < 0 || !zoom_ctrl.valid)
        return;
    struct v4l2_control ctrl = {};
    ctrl.id = zoom_ctrl.id;
    if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
        long cur = ctrl.value;
        long step = zoom_ctrl.step ? zoom_ctrl.step : 1;
        long nv = cur + (delta > 0 ? step : -step);
        if (nv < zoom_ctrl.minimum)
            nv = zoom_ctrl.minimum;
        if (nv > zoom_ctrl.maximum)
            nv = zoom_ctrl.maximum;
        ctrl.value = (int)nv;
        if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
            perror("VIDIOC_S_CTRL zoom_rel");
    }
}

void camera_set_pan_tilt_frac(float pan_frac, float tilt_frac)
{
    if (cam.fd < 0)
        return;
    LOG_DEBUG("camera_set_pan_tilt_frac called pan=%.3f tilt=%.3f", (double)pan_frac,
              (double)tilt_frac);
    if (pan_ctrl.valid) {
        if (pan_frac < 0.0f)
            pan_frac = 0.0f;
        if (pan_frac > 1.0f)
            pan_frac = 1.0f;
        long range = pan_ctrl.maximum - pan_ctrl.minimum;
        long target = pan_ctrl.minimum + (long)(pan_frac * (float)range);
        LOG_DEBUG("pan target=%ld (min=%ld max=%ld)", target, pan_ctrl.minimum, pan_ctrl.maximum);
        struct v4l2_control ctrl = {};
        ctrl.id = pan_ctrl.id;
        ctrl.value = (int)target;
        if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
            perror("VIDIOC_S_CTRL pan_abs");
    }
    if (tilt_ctrl.valid) {
        if (tilt_frac < 0.0f)
            tilt_frac = 0.0f;
        if (tilt_frac > 1.0f)
            tilt_frac = 1.0f;
        long range = tilt_ctrl.maximum - tilt_ctrl.minimum;
        long target = tilt_ctrl.minimum + (long)(tilt_frac * (float)range);
        LOG_DEBUG("tilt target=%ld (min=%ld max=%ld)", target, tilt_ctrl.minimum,
                  tilt_ctrl.maximum);
        struct v4l2_control ctrl = {};
        ctrl.id = tilt_ctrl.id;
        ctrl.value = (int)target;
        if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
            perror("VIDIOC_S_CTRL tilt_abs");
    }
}

void camera_get_pan_tilt_frac(float* pan_frac, float* tilt_frac)
{
    if (pan_frac)
        *pan_frac = 0.5f;
    if (tilt_frac)
        *tilt_frac = 0.5f;
    if (cam.fd < 0)
        return;
    if (pan_ctrl.valid && pan_frac) {
        struct v4l2_control ctrl = {};
        ctrl.id = pan_ctrl.id;
        if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
            long v = ctrl.value;
            long range = pan_ctrl.maximum - pan_ctrl.minimum;
            if (range > 0)
                *pan_frac = (float)(v - pan_ctrl.minimum) / (float)range;
            else
                *pan_frac = 0.5f;
        }
    }
    if (tilt_ctrl.valid && tilt_frac) {
        struct v4l2_control ctrl = {};
        ctrl.id = tilt_ctrl.id;
        if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
            long v = ctrl.value;
            long range = tilt_ctrl.maximum - tilt_ctrl.minimum;
            if (range > 0)
                *tilt_frac = (float)(v - tilt_ctrl.minimum) / (float)range;
            else
                *tilt_frac = 0.5f;
        }
    }
}

void camera_apply_controls(AppState* state)
{
    LOG_DEBUG("exposure_ctrl valid=%d id=0x%x min=%ld max=%ld", exposure_ctrl.valid,
              exposure_ctrl.id, exposure_ctrl.minimum, exposure_ctrl.maximum);
    if (cam.fd < 0)
        return;
    time_t now = time(nullptr);

    // For legacy behavior (state-based), fall back to the previous implementation
    // PAN
    int dx = state->pan_x - last_pan_x;
    if (dx != 0) {
        if (pan_ctrl.valid) {
            struct v4l2_control ctrl = {};
            ctrl.id = pan_ctrl.id;
            if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
                long cur = ctrl.value;
                long step =
                    pan_ctrl.step ? pan_ctrl.step : (pan_ctrl.maximum - pan_ctrl.minimum) / 20;
                long steps = dx / 20;
                if (steps == 0)
                    steps = (dx > 0) ? 1 : -1;
                long nv = cur + steps * step;
                if (nv < pan_ctrl.minimum)
                    nv = pan_ctrl.minimum;
                if (nv > pan_ctrl.maximum)
                    nv = pan_ctrl.maximum;
                ctrl.value = (int)nv;
                if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                    perror("VIDIOC_S_CTRL pan");
            }
        }
        else {
            state->notification = "Camera has no pan control";
            state->notification_until = now + 2;
        }
        last_pan_x = state->pan_x;
    }

    // TILT (legacy)
    int dy = state->pan_y - last_pan_y;
    if (dy != 0) {
        if (tilt_ctrl.valid) {
            struct v4l2_control ctrl = {};
            ctrl.id = tilt_ctrl.id;
            if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
                long cur = ctrl.value;
                long step =
                    tilt_ctrl.step ? tilt_ctrl.step : (tilt_ctrl.maximum - tilt_ctrl.minimum) / 20;
                long steps = dy / 20;
                if (steps == 0)
                    steps = (dy > 0) ? 1 : -1;
                long nv = cur - steps * step; // inverted
                if (nv < tilt_ctrl.minimum)
                    nv = tilt_ctrl.minimum;
                if (nv > tilt_ctrl.maximum)
                    nv = tilt_ctrl.maximum;
                ctrl.value = (int)nv;
                if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                    perror("VIDIOC_S_CTRL tilt");
            }
        }
        else {
            state->notification = "Camera has no tilt control";
            state->notification_until = now + 2;
        }
        last_pan_y = state->pan_y;
    }

    // ZOOM (legacy)
    if (zoom_ctrl.valid) {
        float z = state->zoom;
        if (z != last_zoom) {
            struct v4l2_control ctrl = {};
            ctrl.id = zoom_ctrl.id;
            if (xioctl(cam.fd, VIDIOC_G_CTRL, &ctrl) == 0) {
                long cur = ctrl.value;
                // map zoom float around 1.0.. to control range
                long range = zoom_ctrl.maximum - zoom_ctrl.minimum;
                long target = zoom_ctrl.minimum + (long)((z - 0.1f) / 10.0f * range);
                if (target < zoom_ctrl.minimum)
                    target = zoom_ctrl.minimum;
                if (target > zoom_ctrl.maximum)
                    target = zoom_ctrl.maximum;
                ctrl.value = (int)target;
                if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                    perror("VIDIOC_S_CTRL zoom");
            }
            last_zoom = z;
        }
    }
    else {
        // if UI requested zoom but camera has no zoom control, notify once
        if (state->zoom != last_zoom) {
            state->notification = "Camera has no zoom control";
            state->notification_until = now + 2;
            last_zoom = state->zoom;
        }
    }

    // EXPOSURE MODE — must be set before exposure time
    if (exposure_auto_ctrl.valid) {
        struct v4l2_control ctrl = {};
        ctrl.id = exposure_auto_ctrl.id;
        ctrl.value = (state->exposure_mode == "manual") ? 1 : 3;
        xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl);
        if (state->exposure_mode != "manual")
            last_exposure_val = LONG_MIN;
    }

    // EXPOSURE TIME — only when manual
    if (exposure_ctrl.valid && state->exposure_mode == "manual") {
        long v = (long)state->exposure_time;
        if (v != last_exposure_val) {
            struct v4l2_control ctrl = {};
            ctrl.id = exposure_ctrl.id;
            ctrl.value = (int)v;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                perror("VIDIOC_S_CTRL exposure");
            else
                last_exposure_val = v;
        }
    }

    // GAIN (only if changed)
    if (gain_ctrl.valid) {
        long v = (long)state->gain;
        if (v != last_gain_val) {
            struct v4l2_control ctrl = {};
            ctrl.id = gain_ctrl.id;
            ctrl.value = v;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                perror("VIDIOC_S_CTRL gain");
            }
            else {
                last_gain_val = v;
            }
        }
    }

    // AUTO WHITE BALANCE toggle
    {
        struct v4l2_control ctrl = {};
        ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
        ctrl.value = (state->wb_mode == "auto") ? 1 : 0;
        xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl);
    }

    // WHITE BALANCE TEMP — only when manual
    if (wb_ctrl.valid && state->wb_mode == "manual") {
        long v = (long)state->wb_temp;
        if (v != last_wb_val) {
            struct v4l2_control ctrl = {};
            ctrl.id = wb_ctrl.id;
            ctrl.value = v;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0)
                perror("VIDIOC_S_CTRL wb");
            else
                last_wb_val = v;
        }
    }

    // AUTOFOCUS (only if changed)
    if (autofocus_ctrl.valid) {
        int v = state->autofocus ? 1 : 0;
        if (v != last_autofocus_val) {
            struct v4l2_control ctrl = {};
            ctrl.id = autofocus_ctrl.id;
            ctrl.value = v;
            if (xioctl(cam.fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                perror("VIDIOC_S_CTRL autofocus");
            }
            else {
                last_autofocus_val = v;
            }
        }
    }
}
