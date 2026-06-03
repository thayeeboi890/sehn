/* overlay.cpp

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

#include "overlay.h"
#include <X11/Xlib.h>
#include <cstdio>
#include <ctime>
#include <cstring>

static const char *mode_str(Mode m) {
    switch (m) {
        case Mode::Photo:   return "PHOTO";
        case Mode::Burst:   return "BURST";
        case Mode::Video:   return "VIDEO";
        case Mode::Preview: return "PREVIEW";
        default:            return "???";
    }
}

void overlay_draw(AppState *state, Display *dpy, Window win, GC gc) {
    if (!state->overlay_visible) return;

    // white text
    XSetForeground(dpy, gc, 0xFFFFFF);

    char buf[128];

    // top-left: mode
    snprintf(buf, sizeof(buf), "%s", mode_str(state->mode));
    XDrawString(dpy, win, gc, 8, 16, buf, strlen(buf));

    // top-left second line: resolution
    snprintf(buf, sizeof(buf), "%ux%u @ %ufps",
             state->width, state->height, state->framerate);
    XDrawString(dpy, win, gc, 8, 32, buf, strlen(buf));

    // top-left third line: recording indicator
    if (state->recording) {
        XSetForeground(dpy, gc, 0xFF0000); // red
        snprintf(buf, sizeof(buf), "● REC");
        XDrawString(dpy, win, gc, 8, 48, buf, strlen(buf));
        XSetForeground(dpy, gc, 0xFFFFFF);
    }

    // bottom-left: timestamp
    time_t now = time(nullptr);
    struct tm *tm = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    XDrawString(dpy, win, gc, 8, state->win_h - 8, buf, strlen(buf));
}
