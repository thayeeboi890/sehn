/* ui.cpp

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

#include "ui.h"
#include "camera.h"
#include "panel.h"
#include "overlay.h"
#include "capture.h"
#include "input.h"
#include "signals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>

#include <sys/ipc.h>
#include <sys/shm.h>

struct UIState {
    Display            *dpy;
    Window              win;
    GC                  gc;
    XShmSegmentInfo     shm_info;
    XImage             *ximg;
    int                 screen;
    bool                shm_available;
    unsigned char      *rgb_buf;    // converted RGB frame for display
    size_t              rgb_size;
};

static UIState ui = {};

// ── format conversion ────────────────────────────────────────────────────────

// YUYV to RGB24
static void yuyv_to_rgb(const uint8_t *yuyv, uint8_t *rgb,
                         uint32_t width, uint32_t height) {
    size_t n = (size_t)width * height / 2;
    for (size_t i = 0; i < n; i++) {
        int y0 = yuyv[0], u = yuyv[1], y1 = yuyv[2], v = yuyv[3];
        yuyv += 4;

        auto clamp = [](int x) -> uint8_t {
            return x < 0 ? 0 : x > 255 ? 255 : (uint8_t)x;
        };

        int c, d, e;

        c = y0 - 16; d = u - 128; e = v - 128;
        *rgb++ = clamp((298*c         + 409*e + 128) >> 8);
        *rgb++ = clamp((298*c - 100*d - 208*e + 128) >> 8);
        *rgb++ = clamp((298*c + 516*d         + 128) >> 8);

        c = y1 - 16;
        *rgb++ = clamp((298*c         + 409*e + 128) >> 8);
        *rgb++ = clamp((298*c - 100*d - 208*e + 128) >> 8);
        *rgb++ = clamp((298*c + 516*d         + 128) >> 8);
    }
}

// MJPEG: libjpeg decompresses directly into rgb_buf
#include <jpeglib.h>
static bool mjpeg_to_rgb(const uint8_t *data, size_t len,
                          uint8_t *rgb, uint32_t /*width*/, uint32_t height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)len);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    uint32_t row_stride = cinfo.output_width * 3;
    while (cinfo.output_scanline < height) {
        uint8_t *row = rgb + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

// ── XShm setup ───────────────────────────────────────────────────────────────

static bool shm_init(AppState *state) {
    int screen = DefaultScreen(ui.dpy);
    Visual *vis = DefaultVisual(ui.dpy, screen);
    int depth   = DefaultDepth(ui.dpy, screen);

    ui.ximg = XShmCreateImage(ui.dpy, vis, depth, ZPixmap,
                               nullptr, &ui.shm_info,
                               state->width, state->height);
    if (!ui.ximg) return false;

    ui.shm_info.shmid = shmget(IPC_PRIVATE,
                                ui.ximg->bytes_per_line * ui.ximg->height,
                                IPC_CREAT | 0600);
    if (ui.shm_info.shmid < 0) return false;

    ui.shm_info.shmaddr = ui.ximg->data =
        (char *)shmat(ui.shm_info.shmid, nullptr, 0);
    ui.shm_info.readOnly = False;

    if (!XShmAttach(ui.dpy, &ui.shm_info)) return false;
    return true;
}

static void shm_cleanup() {
    if (ui.ximg) {
        XShmDetach(ui.dpy, &ui.shm_info);
        XDestroyImage(ui.ximg);
        shmdt(ui.shm_info.shmaddr);
        shmctl(ui.shm_info.shmid, IPC_RMID, nullptr);
        ui.ximg = nullptr;
    }
}

// ── rgb -> XImage ─────────────────────────────────────────────────────────────
// Converts planar RGB into the packed BGRX format X11 expects.

static void rgb_to_ximage(const uint8_t *rgb, uint32_t width, uint32_t height) {
    uint32_t *px = (uint32_t *)ui.ximg->data;
    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t r = rgb[i*3+0];
        uint8_t g = rgb[i*3+1];
        uint8_t b = rgb[i*3+2];
        px[i] = (r << 16) | (g << 8) | b;
    }
}

// ── input handling ────────────────────────────────────────────────────────────

static void handle_key(AppState *state, XKeyEvent *ev) {
    KeySym sym = XLookupKeysym(ev, 0);
    switch (sym) {
        case XK_q:
        case XK_Escape:
            state->running = false;
            break;
        case XK_space:
        case XK_c: {
        size_t frame_size = 0;
        const void *frame = camera_next_frame(state, &frame_size);
        if (frame) {
            if (state->mode == Mode::Burst)
                capture_burst(state);
            else if (state->mode == Mode::Video) {
                if (!state->recording) {
                    capture_video_start(state);
                    state->recording = true;
                } else {
                    capture_video_stop(state);
                    state->recording = false;
                }
            } else {
                capture_photo(state, frame, frame_size);
            }
        }
        break;
    }
        case XK_m:
            // cycle mode forward
            state->mode = (Mode)(((int)state->mode + 1) % 4);
            break;
        case XK_M:
            // cycle mode backward
            state->mode = (Mode)(((int)state->mode + 3) % 4);
            break;
        case XK_f:
            // TODO: toggle fullscreen
            state->fullscreen = !state->fullscreen;
            break;
        case XK_Tab:
            state->panel_visible = !state->panel_visible;
            break;
        case XK_o:
            state->overlay_visible = !state->overlay_visible;
            break;
        default:
            break;
    }
}

// ── main loop ────────────────────────────────────────────────────────────────

int ui_run(AppState *state) {

    KeyMap km = keys_load(state);
    ui.dpy = XOpenDisplay(nullptr);
    if (!ui.dpy) {
        fprintf(stderr, "sehn: cannot open display\n");
        return 1;
    }

    ui.screen = DefaultScreen(ui.dpy);
    int win_w  = state->win_w + (state->panel_visible ? state->panel_width : 0);
    int win_h  = state->win_h;

    ui.win = XCreateSimpleWindow(
        ui.dpy, RootWindow(ui.dpy, ui.screen),
        0, 0, win_w, win_h, 0,
        BlackPixel(ui.dpy, ui.screen),
        BlackPixel(ui.dpy, ui.screen));

    XStoreName(ui.dpy, ui.win, "sehn");
    XSelectInput(ui.dpy, ui.win,
                 ExposureMask | KeyPressMask |
                 ButtonPressMask | StructureNotifyMask);

    // handle WM_DELETE_WINDOW so closing the window quits cleanly
    Atom wm_delete = XInternAtom(ui.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui.dpy, ui.win, &wm_delete, 1);

    ui.gc = XCreateGC(ui.dpy, ui.win, 0, nullptr);
    XMapWindow(ui.dpy, ui.win);
    if (state->borderless) {
        struct {
            unsigned long flags, functions, decorations;
            long input_mode;
            unsigned long status;
        } hints = { 2, 0, 0, 0, 0 };
        Atom mwm = XInternAtom(ui.dpy, "_MOTIF_WM_HINTS", False);
        XChangeProperty(ui.dpy, ui.win, mwm, mwm, 32,
                        PropModeReplace, (unsigned char *)&hints, 5);
    }
    if (state->fullscreen) {
        Atom wm_state    = XInternAtom(ui.dpy, "_NET_WM_STATE", False);
        Atom wm_fullscr  = XInternAtom(ui.dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(ui.dpy, ui.win, wm_state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&wm_fullscr, 1);
    }
    if (state->hide_pointer) {
        Pixmap blank = XCreatePixmap(ui.dpy, ui.win, 1, 1, 1);
        XColor dummy = {};
        Cursor cursor = XCreatePixmapCursor(ui.dpy, blank, blank,
                                             &dummy, &dummy, 0, 0);
        XDefineCursor(ui.dpy, ui.win, cursor);
        XFreePixmap(ui.dpy, blank);
    }    
    XFlush(ui.dpy);

    // try XShm, fall back to XPutImage
    ui.shm_available = shm_init(state);
    if (!ui.shm_available) {
        fprintf(stderr, "sehn: XShm unavailable, falling back to XPutImage\n");
        int screen   = DefaultScreen(ui.dpy);
        Visual *vis  = DefaultVisual(ui.dpy, screen);
        int depth    = DefaultDepth(ui.dpy, screen);
        ui.rgb_size  = (size_t)state->width * state->height * 4;
        ui.rgb_buf   = (unsigned char *)malloc(ui.rgb_size);
        ui.ximg      = XCreateImage(ui.dpy, vis, depth, ZPixmap, 0,
                                     (char *)ui.rgb_buf,
                                     state->width, state->height, 32, 0);
    }

    // intermediate RGB buffer for format conversion
    size_t rgb_sz   = (size_t)state->width * state->height * 3;
    uint8_t *rgb    = (uint8_t *)malloc(rgb_sz);

    state->running  = true;

    while (state->running) {
        signals_dispatch(state);
        while (XPending(ui.dpy)) {
            XEvent ev;
            XNextEvent(ui.dpy, &ev);
            if (ev.type == KeyPress)
                input_handle_key(state, km, &ev.xkey);
            else if (ev.type == ClientMessage) {
                if ((Atom)ev.xclient.data.l[0] == wm_delete)
                    state->running = false;
            } else if (ev.type == ButtonPress) {
                input_handle_button(state, &ev.xbutton);
            } else if (ev.type == ConfigureNotify) {
                state->win_w = ev.xconfigure.width  - state->panel_width;
                state->win_h = ev.xconfigure.height;
            }
        }

        // grab a frame
        size_t frame_size = 0;
        const void *frame = camera_next_frame(state, &frame_size);
        if (!frame) continue;

        // convert to RGB
        if (state->v4l2_format == "mjpeg") {
            mjpeg_to_rgb((const uint8_t *)frame, frame_size,
                         rgb, state->width, state->height);
        } else {
            yuyv_to_rgb((const uint8_t *)frame, rgb,
                        state->width, state->height);
        }

        // push to XImage
        rgb_to_ximage(rgb, state->width, state->height);

        // blit to window
        if (ui.shm_available)
            XShmPutImage(ui.dpy, ui.win, ui.gc, ui.ximg,
                         0, 0, 0, 0, state->width, state->height, False);
        else
            XPutImage(ui.dpy, ui.win, ui.gc, ui.ximg,
                      0, 0, 0, 0, state->width, state->height);
        if (state->panel_visible)
            panel_draw(state, ui.dpy, ui.win, ui.gc);
        if (state->overlay_visible)
            overlay_draw(state, ui.dpy, ui.win, ui.gc);
        XFlush(ui.dpy);

    }

    free(rgb);
    ui_cleanup(state);
    return 0;
}

void ui_cleanup(AppState *state) {
    (void)state;
    shm_cleanup();
    if (ui.gc)  XFreeGC(ui.dpy, ui.gc);
    if (ui.win) XDestroyWindow(ui.dpy, ui.win);
    if (ui.dpy) XCloseDisplay(ui.dpy);
    ui = {};
}
