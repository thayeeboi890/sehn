/* camera.h

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

#pragma once

#include "sehn.h"
#include <linux/videodev2.h>

struct CameraBuffer {
    void* start;
    size_t length;
};

struct CameraState {
    int fd;
    CameraBuffer* buffers;
    unsigned int n_buffers;
    struct v4l2_format fmt;
    bool streaming;
};

int camera_open(AppState* state);
int camera_start(AppState* state);
const void* camera_next_frame(AppState* state, size_t* out_size);
void camera_stop(AppState* state);
void camera_close(AppState* state);
void camera_apply_controls(AppState* state);

bool camera_has_pan();
bool camera_has_tilt();
bool camera_has_zoom();

void camera_pan_rel(int dx, int dy); /* dx/dy in UI-pan steps (±1) */
void camera_zoom_rel(float delta);   /* delta is zoom multiplier delta */

/* Set absolute pan/tilt as fractions 0.0..1.0 of their ranges */
void camera_set_pan_tilt_frac(float pan_frac, float tilt_frac);
void camera_get_pan_tilt_frac(float* pan_frac, float* tilt_frac);
int camera_negotiate(AppState* state);
