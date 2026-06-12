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
#include "capture.h"
#include "input.h"
#include "overlay.h"
#include "panel.h"
#include "signals.h"
#include "video.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/keysym.h>

#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

struct UIState {
    Display* dpy;
    Window win;
    GC gc;
    XShmSegmentInfo shm_info;
    XImage* ximg;
    int screen;
    bool shm_available;
    unsigned char* rgb_buf; // fallback XImage buffer
    size_t rgb_size;
    uint8_t* last_rgb; // last converted frame (RGB)
    size_t last_rgb_size;

    // frame grabber thread
    pthread_t frame_thread;
    pthread_mutex_t last_rgb_mutex;
    volatile int frame_thread_running;
    volatile int frame_available;
};

static UIState ui = {};
static volatile int ui_shm_error = 0; /* set by X error handler when XShmAttach fails */
static int ui_shm_error_handler(Display* d, XErrorEvent* ev)
{
    (void)d;
    (void)ev;
    ui_shm_error = 1;
    return 0;
}

// forward declarations for converters used by frame thread
static void yuyv_to_rgb(const uint8_t* yuyv, uint8_t* rgb, uint32_t width, uint32_t height);
static bool mjpeg_to_rgb(const uint8_t* data, size_t len, uint8_t* rgb, uint32_t width,
                         uint32_t height);

// frame grabber thread: reads raw frames and converts into ui.last_rgb
static void* frame_thread_func(void* arg)
{
    AppState* s = (AppState*)arg;
    while (ui.frame_thread_running) {
        size_t frame_size = 0;
        const void* frame = camera_next_frame(s, &frame_size);
        if (!frame)
            continue;
        pthread_mutex_lock(&ui.last_rgb_mutex);
        if (s->v4l2_format == "mjpeg") {
            mjpeg_to_rgb((const uint8_t*)frame, frame_size, ui.last_rgb, s->width, s->height);
        }
        else {
            yuyv_to_rgb((const uint8_t*)frame, ui.last_rgb, s->width, s->height);
        }
        ui.frame_available = 1;
        pthread_mutex_unlock(&ui.last_rgb_mutex);
        if (s->recording && vid_is_open())
            capture_video_frame(s, frame, frame_size);
    }
    return nullptr;
}

// ── format conversion ────────────────────────────────────────────────────────

// YUYV to RGB24
static void yuyv_to_rgb(const uint8_t* yuyv, uint8_t* rgb, uint32_t width, uint32_t height)
{
    size_t n = (size_t)width * height / 2;
    for (size_t i = 0; i < n; i++) {
        int y0 = yuyv[0], u = yuyv[1], y1 = yuyv[2], v = yuyv[3];
        yuyv += 4;

        auto clamp = [](int x) -> uint8_t { return x < 0 ? 0 : x > 255 ? 255 : (uint8_t)x; };

        int c, d, e;

        c = y0 - 16;
        d = u - 128;
        e = v - 128;
        *rgb++ = clamp((298 * c + 409 * e + 128) >> 8);
        *rgb++ = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        *rgb++ = clamp((298 * c + 516 * d + 128) >> 8);

        c = y1 - 16;
        *rgb++ = clamp((298 * c + 409 * e + 128) >> 8);
        *rgb++ = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        *rgb++ = clamp((298 * c + 516 * d + 128) >> 8);
    }
}

// MJPEG: libjpeg decompresses directly into rgb_buf
#include "utils.h"
#include <jpeglib.h>
static bool mjpeg_to_rgb(const uint8_t* data, size_t len, uint8_t* rgb, uint32_t /*width*/,
                         uint32_t height)
{
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
        uint8_t* row = rgb + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

// ── XShm setup ───────────────────────────────────────────────────────────────

static bool shm_init(AppState* state)
{
    int screen = DefaultScreen(ui.dpy);
    Visual* vis = DefaultVisual(ui.dpy, screen);
    int depth = DefaultDepth(ui.dpy, screen);

    ui.ximg = XShmCreateImage(ui.dpy, vis, depth, ZPixmap, nullptr, &ui.shm_info, state->win_w,
                              state->win_h);
    if (!ui.ximg)
        return false;

    ui.shm_info.shmid =
        shmget(IPC_PRIVATE, ui.ximg->bytes_per_line * ui.ximg->height, IPC_CREAT | 0600);
    if (ui.shm_info.shmid < 0)
        return false;

    ui.shm_info.shmaddr = ui.ximg->data = (char*)shmat(ui.shm_info.shmid, nullptr, 0);
    ui.shm_info.readOnly = False;

    // XShmAttach may generate a BadAccess error if the X server disallows MIT-SHM.
    // Install a temporary error handler to catch that and fall back cleanly.
    ui_shm_error = 0;
    // error handler sets ui_shm_error

    XErrorHandler oldh = XSetErrorHandler(ui_shm_error_handler);
    XShmAttach(ui.dpy, &ui.shm_info);
    XSync(ui.dpy, False);
    XSetErrorHandler(oldh);
    if (ui_shm_error) {
        // detach any partial attach if necessary
        XShmDetach(ui.dpy, &ui.shm_info);
        return false;
    }
    return true;
}

static void shm_cleanup()
{
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

#include <cmath>

static inline uint8_t clamp8(int x) { return x < 0 ? 0 : x > 255 ? 255 : (uint8_t)x; }

static void rgb_to_ximage_region_fast(const uint8_t* rgb, uint32_t src_stride, uint32_t src_height,
                                      int sx, int sy, uint32_t sw, uint32_t sh, int dst_x,
                                      int dst_y, uint32_t dst_w, uint32_t dst_h)
{
    uint32_t img_w = (uint32_t)ui.ximg->width;
    uint32_t img_h = (uint32_t)ui.ximg->height;
    uint32_t* px = (uint32_t*)ui.ximg->data;

    // clear borders (letterbox/pillarbox) to black where image won't be drawn
    uint32_t black = 0;
    if (dst_x > 0 || dst_y > 0 || (uint32_t)(dst_x + dst_w) < img_w ||
        (uint32_t)(dst_y + dst_h) < img_h) {
        for (uint32_t y = 0; y < img_h; y++) {
            for (uint32_t x = 0; x < img_w; x++) {
                if (x < (uint32_t)dst_x || x >= (uint32_t)(dst_x + dst_w) || y < (uint32_t)dst_y ||
                    y >= (uint32_t)(dst_y + dst_h))
                    px[y * img_w + x] = black;
            }
        }
    }

    // nearest-neighbor into destination rectangle (fast)
    float scale_x = (float)sw / (float)dst_w;
    float scale_y = (float)sh / (float)dst_h;
    for (uint32_t dy = 0; dy < dst_h; dy++) {
        for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t fx = sx + (uint32_t)((float)dx * scale_x);
            uint32_t fy = sy + (uint32_t)((float)dy * scale_y);
            if (fx >= src_stride)
                fx = src_stride - 1;
            if (fy >= src_height)
                fy = src_height - 1;
            const uint8_t* p = rgb + (fy * src_stride + fx) * 3;
            px[(dst_y + dy) * img_w + (dst_x + dx)] =
                ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        }
    }
}

static void rgb_to_ximage_region(const uint8_t* rgb, uint32_t src_stride, uint32_t src_height,
                                 int sx, int sy, uint32_t sw, uint32_t sh, int dst_x, int dst_y,
                                 uint32_t dst_w, uint32_t dst_h)
{
    uint32_t img_w = (uint32_t)ui.ximg->width;
    uint32_t img_h = (uint32_t)ui.ximg->height;
    uint32_t* px = (uint32_t*)ui.ximg->data;

    // clear borders (letterbox/pillarbox) to black where image won't be drawn
    uint32_t black = 0;
    if (dst_x > 0 || dst_y > 0 || (uint32_t)(dst_x + dst_w) < img_w ||
        (uint32_t)(dst_y + dst_h) < img_h) {
        for (uint32_t y = 0; y < img_h; y++) {
            for (uint32_t x = 0; x < img_w; x++) {
                if (x < (uint32_t)dst_x || x >= (uint32_t)(dst_x + dst_w) || y < (uint32_t)dst_y ||
                    y >= (uint32_t)(dst_y + dst_h))
                    px[y * img_w + x] = black;
            }
        }
    }

    // scaling factors
    float scale_x = (float)sw / (float)dst_w;
    float scale_y = (float)sh / (float)dst_h;

    bool use_bilinear = (sw != dst_w) || (sh != dst_h);

    if (!use_bilinear) {
        // nearest-neighbor into destination rectangle
        for (uint32_t dy = 0; dy < dst_h; dy++) {
            for (uint32_t dx = 0; dx < dst_w; dx++) {
                uint32_t fx = sx + (uint32_t)((float)dx * scale_x);
                uint32_t fy = sy + (uint32_t)((float)dy * scale_y);
                if (fx >= src_stride)
                    fx = src_stride - 1;
                if (fy >= src_height)
                    fy = src_height - 1;
                const uint8_t* p = rgb + (fy * src_stride + fx) * 3;
                px[(dst_y + dy) * img_w + (dst_x + dx)] =
                    ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            }
        }
        return;
    }

    // bilinear interpolation into destination rectangle
    for (uint32_t dy = 0; dy < dst_h; dy++) {
        float src_y = sy + (dy + 0.5f) * scale_y - 0.5f;
        int y0 = (int)floorf(src_y);
        int y1 = y0 + 1;
        float wy = src_y - (float)y0;
        if (y0 < 0) {
            y0 = 0;
            wy = src_y;
        }
        if (y1 >= (int)src_height) {
            y1 = src_height - 1;
        }

        for (uint32_t dx = 0; dx < dst_w; dx++) {
            float src_x = sx + (dx + 0.5f) * scale_x - 0.5f;
            int x0 = (int)floorf(src_x);
            int x1 = x0 + 1;
            float wx = src_x - (float)x0;
            if (x0 < 0) {
                x0 = 0;
                wx = src_x;
            }
            if (x1 >= (int)src_stride) {
                x1 = src_stride - 1;
            }

            const uint8_t* p00 = rgb + (y0 * src_stride + x0) * 3;
            const uint8_t* p10 = rgb + (y0 * src_stride + x1) * 3;
            const uint8_t* p01 = rgb + (y1 * src_stride + x0) * 3;
            const uint8_t* p11 = rgb + (y1 * src_stride + x1) * 3;

            int r = (int)((1 - wx) * (1 - wy) * p00[0] + wx * (1 - wy) * p10[0] +
                          (1 - wx) * wy * p01[0] + wx * wy * p11[0]);
            int g = (int)((1 - wx) * (1 - wy) * p00[1] + wx * (1 - wy) * p10[1] +
                          (1 - wx) * wy * p01[1] + wx * wy * p11[1]);
            int b = (int)((1 - wx) * (1 - wy) * p00[2] + wx * (1 - wy) * p10[2] +
                          (1 - wx) * wy * p01[2] + wx * wy * p11[2]);

            px[(dst_y + dy) * img_w + (dst_x + dx)] =
                ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) | (uint32_t)clamp8(b);
        }
    }
}

// helper to present an RGB frame given a source rect
static void present_frame_static(AppState* state, uint8_t* frame_rgb, uint32_t p_src_w,
                                 uint32_t p_src_h, int p_src_x, int p_src_y)
{
    if (!frame_rgb)
        return;
    int dst_w = state->win_w;
    int dst_h = state->win_h;
    float src_aspect = (float)p_src_w / (float)p_src_h;
    float win_aspect = (float)state->win_w / (float)state->win_h;
    int dst_img_w, dst_img_h;
    if (win_aspect > src_aspect) {
        dst_img_h = state->win_h;
        dst_img_w = (int)(dst_img_h * src_aspect);
    }
    else {
        dst_img_w = state->win_w;
        dst_img_h = (int)(dst_img_w / src_aspect);
    }
    int dst_x = (state->win_w - dst_img_w) / 2;
    int dst_y = (state->win_h - dst_img_h) / 2;

    rgb_to_ximage_region(frame_rgb, state->width, state->height, p_src_x, p_src_y, p_src_w, p_src_h,
                         dst_x, dst_y, dst_img_w, dst_img_h);
    if (ui.shm_available)
        XShmPutImage(ui.dpy, ui.win, ui.gc, ui.ximg, 0, 0, 0, 0, dst_w, dst_h, False);
    else
        XPutImage(ui.dpy, ui.win, ui.gc, ui.ximg, 0, 0, 0, 0, dst_w, dst_h);
    if (state->panel_visible)
        panel_draw(state, ui.dpy, ui.win, ui.gc);
    if (state->overlay_visible)
        overlay_draw(state, ui.dpy, ui.win, ui.gc);
    XFlush(ui.dpy);
}

// ── input handling ────────────────────────────────────────────────────────────

void ui_present_last_rgb_region(AppState* state, int src_x, int src_y, uint32_t src_w,
                                uint32_t src_h)
{
    pthread_mutex_lock(&ui.last_rgb_mutex);
    if (ui.last_rgb && ui.last_rgb_size >= (size_t)src_w * src_h * 3) {
        present_frame_static(state, ui.last_rgb, (uint32_t)src_w, (uint32_t)src_h, src_x, src_y);
    }
    pthread_mutex_unlock(&ui.last_rgb_mutex);
}

static void handle_key(AppState* state, XKeyEvent* ev)
{
    KeySym sym = XLookupKeysym(ev, 0);
    switch (sym) {
    case XK_q:
    case XK_Escape:
        state->running = false;
        break;
    case XK_space:
    case XK_c: {
        size_t frame_size = 0;
        const void* frame = camera_next_frame(state, &frame_size);
        if (frame) {
            if (state->mode == Mode::Burst)
                capture_burst(state);
            else if (state->mode == Mode::Video) {
                if (!state->recording) {
                    capture_video_start(state);
                    state->recording = true;
                }
                else {
                    state->recording = false;
                    usleep(50000);
                    capture_video_stop(state);
                }
            }
            else {
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

int ui_run(AppState* state)
{
    LOG_FN();

    KeyMap km = keys_load(state);
    ui.dpy = XOpenDisplay(nullptr);
    if (!ui.dpy) {
        LOG_ERROR("cannot open display");
        return 1;
    }
    LOG_DEBUG("opened X display");

    ui.screen = DefaultScreen(ui.dpy);
    int win_w = state->win_w + (state->panel_visible ? state->panel_width : 0);
    int win_h = state->win_h;

    ui.win = XCreateSimpleWindow(ui.dpy, RootWindow(ui.dpy, ui.screen), 0, 0, win_w, win_h, 0,
                                 BlackPixel(ui.dpy, ui.screen), BlackPixel(ui.dpy, ui.screen));

    XStoreName(ui.dpy, ui.win, "sehn");
    XSelectInput(ui.dpy, ui.win,
                 ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask);
    // grab arrow keys and other special keys so WM doesn't steal them
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_Left), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_Right), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_Up), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_Down), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_plus), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_equal), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_KP_Add), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_minus), AnyModifier, ui.win, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(ui.dpy, XKeysymToKeycode(ui.dpy, XK_KP_Subtract), AnyModifier, ui.win, True,
             GrabModeAsync, GrabModeAsync);
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
        } hints = {2, 0, 0, 0, 0};
        Atom mwm = XInternAtom(ui.dpy, "_MOTIF_WM_HINTS", False);
        XChangeProperty(ui.dpy, ui.win, mwm, mwm, 32, PropModeReplace, (unsigned char*)&hints, 5);
    }
    if (state->fullscreen) {
        Atom wm_state = XInternAtom(ui.dpy, "_NET_WM_STATE", False);
        Atom wm_fullscr = XInternAtom(ui.dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(ui.dpy, ui.win, wm_state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)&wm_fullscr, 1);
    }
    if (state->hide_pointer) {
        Pixmap blank = XCreatePixmap(ui.dpy, ui.win, 1, 1, 1);
        XColor dummy = {};
        Cursor cursor = XCreatePixmapCursor(ui.dpy, blank, blank, &dummy, &dummy, 0, 0);
        XDefineCursor(ui.dpy, ui.win, cursor);
        XFreePixmap(ui.dpy, blank);
    }
    XFlush(ui.dpy);

    // try XShm, fall back to XPutImage
    ui.shm_available = shm_init(state);
    if (!ui.shm_available) {
        LOG_DEBUG("XShm unavailable, falling back to XPutImage");
        int screen = DefaultScreen(ui.dpy);
        Visual* vis = DefaultVisual(ui.dpy, screen);
        int depth = DefaultDepth(ui.dpy, screen);
        ui.rgb_size = (size_t)state->win_w * state->win_h * 4;
        ui.rgb_buf = (unsigned char*)malloc(ui.rgb_size);
        ui.ximg = XCreateImage(ui.dpy, vis, depth, ZPixmap, 0, (char*)ui.rgb_buf, state->win_w,
                               state->win_h, 32, 0);
    }

    // intermediate RGB buffer for format conversion
    size_t rgb_sz = (size_t)state->width * state->height * 3;
    uint8_t* rgb = (uint8_t*)malloc(rgb_sz);
    ui.last_rgb = rgb;
    ui.last_rgb_size = rgb_sz;

    // init frame grabber thread resources
    pthread_mutex_init(&ui.last_rgb_mutex, nullptr);
    ui.frame_available = 0;
    ui.frame_thread_running = 1;

    // start frame grabber thread
    pthread_create(&ui.frame_thread, nullptr, frame_thread_func, state);

    state->running = true;
    bool prev_fullscreen = state->fullscreen;

    while (state->running) {
        signals_dispatch(state);

        // wait briefly for X events so UI remains responsive; frame grabbing runs in background
        // thread
        int xfd = ConnectionNumber(ui.dpy);
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        struct timeval tv = {0, 20000}; // 20ms
        int sel = select(xfd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // process any X events if available
        if (FD_ISSET(xfd, &rfds)) {
            while (XPending(ui.dpy)) {
                XEvent ev;
                XNextEvent(ui.dpy, &ev);
                if (ev.type == KeyPress)
                    input_handle_key(state, km, &ev.xkey);
                else if (ev.type == ClientMessage) {
                    if ((Atom)ev.xclient.data.l[0] == wm_delete)
                        state->running = false;
                }
                else if (ev.type == ButtonPress) {
                    input_handle_button(state, &ev.xbutton);
                }
                else if (ev.type == ButtonRelease) {
                    input_handle_button_release(state, &ev.xbutton);
                }
                else if (ev.type == MotionNotify) {
                    input_handle_motion(state, &ev.xmotion);
                }
                else if (ev.type == ConfigureNotify) {
                    int new_win_w = ev.xconfigure.width - state->panel_width;
                    int new_win_h = ev.xconfigure.height;

                    // preserve camera viewport center for manual zoom/pan mode
                    if (state->zoom_mode == ZoomMode::Percent) {
                        // old source rect
                        uint32_t old_src_w = (uint32_t)((float)state->width / state->zoom);
                        uint32_t old_src_h = (uint32_t)((float)state->height / state->zoom);
                        if (old_src_w > state->width)
                            old_src_w = state->width;
                        if (old_src_h > state->height)
                            old_src_h = state->height;
                        int old_src_x = state->pan_x;
                        int old_src_y = state->pan_y;
                        float center_x = old_src_x + old_src_w * 0.5f;
                        float center_y = old_src_y + old_src_h * 0.5f;

                        state->win_w = new_win_w;
                        state->win_h = new_win_h;

                        // new source rect (same zoom)
                        uint32_t new_src_w = (uint32_t)((float)state->width / state->zoom);
                        uint32_t new_src_h = (uint32_t)((float)state->height / state->zoom);
                        if (new_src_w > state->width)
                            new_src_w = state->width;
                        if (new_src_h > state->height)
                            new_src_h = state->height;

                        state->pan_x = (int)(center_x - new_src_w * 0.5f);
                        state->pan_y = (int)(center_y - new_src_h * 0.5f);

                        int max_x = (int)state->width - (int)new_src_w;
                        int max_y = (int)state->height - (int)new_src_h;
                        if (state->pan_x < 0)
                            state->pan_x = 0;
                        if (state->pan_y < 0)
                            state->pan_y = 0;
                        if (state->pan_x > max_x)
                            state->pan_x = max_x;
                        if (state->pan_y > max_y)
                            state->pan_y = max_y;
                    }
                    else {
                        state->win_w = new_win_w;
                        state->win_h = new_win_h;
                    }

                    // recreate XImage / shared memory if window size changed
                    if (ui.ximg &&
                        (ui.ximg->width != state->win_w || ui.ximg->height != state->win_h)) {
                        // clean up previous image/shm
                        shm_cleanup();

                        // attempt to re-init XShm with new window size
                        ui.shm_available = shm_init(state);
                        if (ui.shm_available) {
                            // if we previously had a fallback rgb buffer, free it
                            if (ui.rgb_buf) {
                                free(ui.rgb_buf);
                                ui.rgb_buf = nullptr;
                                ui.rgb_size = 0;
                            }
                        }
                        else {
                            LOG_DEBUG("XShm unavailable after resize, falling back to XPutImage");
                            int screen = DefaultScreen(ui.dpy);
                            Visual* vis = DefaultVisual(ui.dpy, screen);
                            int depth = DefaultDepth(ui.dpy, screen);
                            ui.rgb_size = (size_t)state->win_w * state->win_h * 4;
                            ui.rgb_buf = (unsigned char*)realloc(ui.rgb_buf, ui.rgb_size);
                            ui.ximg =
                                XCreateImage(ui.dpy, vis, depth, ZPixmap, 0, (char*)ui.rgb_buf,
                                             state->win_w, state->win_h, 32, 0);
                        }
                    }
                }
            }
        }

        if (state->fullscreen != prev_fullscreen) {
            Atom wm_state = XInternAtom(ui.dpy, "_NET_WM_STATE", False);
            Atom wm_fullscr = XInternAtom(ui.dpy, "_NET_WM_STATE_FULLSCREEN", False);
            XEvent xev = {};
            xev.xclient.type = ClientMessage;
            xev.xclient.message_type = wm_state;
            xev.xclient.window = ui.win;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] =
                state->fullscreen ? 1 : 0; // _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE
            xev.xclient.data.l[1] = (long)wm_fullscr;
            xev.xclient.data.l[2] = 0;
            XSendEvent(ui.dpy, RootWindow(ui.dpy, ui.screen), False,
                       SubstructureRedirectMask | SubstructureNotifyMask, &xev);
            prev_fullscreen = state->fullscreen;
        }

        // compute src region for current zoom/pan
        uint32_t src_w, src_h;
        int src_x, src_y;

        if (state->zoom_mode == ZoomMode::Fit || state->zoom_mode == ZoomMode::Fill) {
            // fit: scale so the whole frame fits in the window
            float scale_x = (float)state->win_w / (float)state->width;
            float scale_y = (float)state->win_h / (float)state->height;
            float scale = (state->zoom_mode == ZoomMode::Fill)
                              ? (scale_x > scale_y ? scale_x : scale_y)
                              : (scale_x < scale_y ? scale_x : scale_y);
            src_w = state->width;
            src_h = state->height;
            src_x = 0;
            src_y = 0;
            state->zoom = scale;
        }
        else {
            // manual percent zoom — src region is a crop of the frame
            float inv = 1.0f / state->zoom;
            src_w = (uint32_t)((float)state->width * inv);
            src_h = (uint32_t)((float)state->height * inv);

            // never exceed actual frame dimensions
            if (src_w > state->width)
                src_w = state->width;
            if (src_h > state->height)
                src_h = state->height;

            // clamp pan so we never go out of frame
            // clamp pan so we never go out of frame
            int max_x = (int)state->width - (int)src_w;
            int max_y = (int)state->height - (int)src_h;
            if (state->pan_x < 0)
                state->pan_x = 0;
            if (state->pan_y < 0)
                state->pan_y = 0;
            if (state->pan_x > max_x)
                state->pan_x = max_x;
            if (state->pan_y > max_y)
                state->pan_y = max_y;
            src_x = state->pan_x;
            src_y = state->pan_y;
        }

        pthread_mutex_lock(&ui.last_rgb_mutex);
        if (ui.frame_available)
            ui.frame_available = 0;
        if (ui.last_rgb)
            present_frame_static(state, ui.last_rgb, src_w, src_h, src_x, src_y);
        pthread_mutex_unlock(&ui.last_rgb_mutex);
    }

    // stop frame thread
    ui.frame_thread_running = 0;
    pthread_join(ui.frame_thread, nullptr);
    pthread_mutex_destroy(&ui.last_rgb_mutex);

    free(rgb);
    ui_cleanup(state);
    return 0;
}

void ui_cleanup(AppState* state)
{
    (void)state;
    shm_cleanup();
    if (ui.gc)
        XFreeGC(ui.dpy, ui.gc);
    if (ui.win)
        XDestroyWindow(ui.dpy, ui.win);
    if (ui.dpy)
        XCloseDisplay(ui.dpy);
    ui = {};
}
