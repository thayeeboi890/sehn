/* panel.cpp

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

This file was made with the help of AI.
*/



#include "panel.h"
#include "theme.h"
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int g_hover = -1;
static int g_press = -1;

void panel_set_hover(int btn) { g_hover = btn; }
void panel_set_press(int btn) { g_press = btn; }

static XftFont  *g_font = nullptr;
static XftDraw  *g_draw = nullptr;
static Display  *g_dpy  = nullptr;

void panel_init(Display *dpy, Window win, const char *font_path) {
    g_dpy = dpy;
    if (font_path && *font_path)
        g_font = XftFontOpen(dpy, DefaultScreen(dpy),
                             XFT_FILE,      XftTypeString, font_path,
                             XFT_SIZE,      XftTypeDouble, 10.0,
                             XFT_ANTIALIAS, XftTypeBool,   True,
                             NULL);
    if (!g_font)
        g_font = XftFontOpenName(dpy, DefaultScreen(dpy), "monospace:size=9");
    g_draw = XftDrawCreate(dpy, win,
                           DefaultVisual(dpy, DefaultScreen(dpy)),
                           DefaultColormap(dpy, DefaultScreen(dpy)));
}

void panel_cleanup(Display *dpy) {
    if (g_draw) { XftDrawDestroy(g_draw); g_draw = nullptr; }
    if (g_font) { XftFontClose(dpy, g_font); g_font = nullptr; }
}

// ── colour helpers ────────────────────────────────────────────────────────────

static uint32_t lighten(uint32_t c, float pct) {
    int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    r = (int)(r + (255 - r) * pct); if (r > 255) r = 255;
    g = (int)(g + (255 - g) * pct); if (g > 255) g = 255;
    b = (int)(b + (255 - b) * pct); if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t darken(uint32_t c, float pct) {
    int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    r = (int)(r * (1.0f - pct)); if (r < 0) r = 0;
    g = (int)(g * (1.0f - pct)); if (g < 0) g = 0;
    b = (int)(b * (1.0f - pct)); if (b < 0) b = 0;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void set_fg(Display *dpy, GC gc, uint32_t c) {
    XSetForeground(dpy, gc, c);
}

// Unpack uint32_t 0xRRGGBB → cairo r,g,b in [0,1]
static void u32_to_cairo(uint32_t c, double &r, double &g, double &b) {
    r = ((c >> 16) & 0xff) / 255.0;
    g = ((c >>  8) & 0xff) / 255.0;
    b = ( c        & 0xff) / 255.0;
}

// ── layout ───────────────────────────────────────────────────────────────────

struct PanelLayout {
    int pw, ph, px;
    int bw, bh;
    int bx;
    int y_photos;
    int y_mode;
    int y_shutter;
};

static PanelLayout panel_layout(AppState *state) {
    PanelLayout l{};
    l.pw = state->panel_width;
    l.ph = state->win_h;
    l.px = state->win_w;

    int margin = 10;
    l.bw = l.pw - margin * 2;
    l.bh = l.bw;
    if (l.bh < 32) l.bh = l.bw = 32;

    l.bx = l.px + margin;

    int pad      = margin + 4;
    l.y_photos   = pad;
    l.y_mode     = (l.ph - l.bh) / 2;
    l.y_shutter  = l.ph - pad - l.bh;

    return l;
}

// ── Xlib rounded rect (for button bg only) ───────────────────────────────────

static void fill_rounded_rect(Display *dpy, Window win, GC gc,
                               uint32_t col, int x, int y, int w, int h, int r) {
    set_fg(dpy, gc, col);
    if (r <= 0) { XFillRectangle(dpy, win, gc, x, y, w, h); return; }
    XFillRectangle(dpy, win, gc, x + r, y,     w - r*2, h);
    XFillRectangle(dpy, win, gc, x,     y + r, w,       h - r*2);
    XFillArc(dpy, win, gc, x,         y,         r*2, r*2, 90*64,  90*64);
    XFillArc(dpy, win, gc, x+w-r*2,   y,         r*2, r*2, 0,      90*64);
    XFillArc(dpy, win, gc, x,         y+h-r*2,   r*2, r*2, 180*64, 90*64);
    XFillArc(dpy, win, gc, x+w-r*2,   y+h-r*2,   r*2, r*2, 270*64, 90*64);
}

static void stroke_rounded_rect_xlib(Display *dpy, Window win, GC gc,
                                      uint32_t col, int x, int y, int w, int h, int r) {
    set_fg(dpy, gc, col);
    XDrawLine(dpy, win, gc, x+r,   y,     x+w-r, y);
    XDrawLine(dpy, win, gc, x+r,   y+h,   x+w-r, y+h);
    XDrawLine(dpy, win, gc, x,     y+r,   x,     y+h-r);
    XDrawLine(dpy, win, gc, x+w,   y+r,   x+w,   y+h-r);
    XDrawArc(dpy, win, gc, x,         y,         r*2, r*2, 90*64,  90*64);
    XDrawArc(dpy, win, gc, x+w-r*2,   y,         r*2, r*2, 0,      90*64);
    XDrawArc(dpy, win, gc, x,         y+h-r*2,   r*2, r*2, 180*64, 90*64);
    XDrawArc(dpy, win, gc, x+w-r*2,   y+h-r*2,   r*2, r*2, 270*64, 90*64);
}

// ── Cairo icon drawing ────────────────────────────────────────────────────────
// Each icon is drawn into a temporary Cairo surface then composited onto the
// window.  `sz` is the logical icon size in pixels; cx/cy is button centre.

// Create a cairo-xlib surface for a region of the window, draw into it,
// destroy it.  We recreate per-button — cheap enough for a panel repaint.

struct CairoCtx {
    cairo_surface_t *surf;
    cairo_t         *cr;

    // origin of the surface in window coordinates
    int ox, oy;
};

static CairoCtx cairo_begin(Display *dpy, Window win, int x, int y, int w, int h) {
    CairoCtx c;
    c.ox = x; c.oy = y;
    c.surf = cairo_xlib_surface_create(dpy, win,
                                       DefaultVisual(dpy, DefaultScreen(dpy)),
                                       // surface covers whole window width so
                                       // we just clip — easier than sub-windows
                                       DisplayWidth(dpy, DefaultScreen(dpy)),
                                       DisplayHeight(dpy, DefaultScreen(dpy)));
    c.cr = cairo_create(c.surf);
    cairo_rectangle(c.cr, x, y, w, h);
    cairo_clip(c.cr);
    // translate so icon drawing code works in local coords (0,0) = top-left of btn
    cairo_translate(c.cr, x, y);
    return c;
}

static void cairo_end(CairoCtx &c) {
    cairo_destroy(c.cr);
    cairo_surface_destroy(c.surf);
}

// helper: set source from uint32_t + alpha
static void csrc(cairo_t *cr, uint32_t col, double a = 1.0) {
    double r, g, b;
    u32_to_cairo(col, r, g, b);
    cairo_set_source_rgba(cr, r, g, b, a);
}

// Rounded rectangle path in Cairo (local coords)
static void cairo_rrect(cairo_t *cr, double x, double y,
                         double w, double h, double r) {
    cairo_new_path(cr);
    cairo_arc(cr, x+r,   y+r,   r, M_PI,       3*M_PI/2);
    cairo_arc(cr, x+w-r, y+r,   r, 3*M_PI/2,   2*M_PI);
    cairo_arc(cr, x+w-r, y+h-r, r, 0,           M_PI/2);
    cairo_arc(cr, x+r,   y+h-r, r, M_PI/2,      M_PI);
    cairo_close_path(cr);
}

// ── icon: folder ─────────────────────────────────────────────────────────────
static void icon_folder(cairo_t *cr, double cx, double cy,
                         double sz, uint32_t col) {
    double bw = sz * 0.68, bh = sz * 0.50;
    double bx = cx - bw/2,  by = cy - bh/2 + sz*0.05;
    double tw = bw * 0.42,  th = sz * 0.12;
    double r  = sz * 0.06;

    // drop shadow
    csrc(cr, 0x000000, 0.30);
    cairo_rrect(cr, bx+2, by+2, bw, bh, r);
    cairo_fill(cr);

    // tab
    csrc(cr, col, 1.0);
    cairo_rrect(cr, bx, by - th + 1, tw, th + r, r * 0.7);
    cairo_fill(cr);

    // body
    cairo_rrect(cr, bx, by, bw, bh, r);
    cairo_fill(cr);

    // top highlight stripe
    double hr, hg, hb; u32_to_cairo(col, hr, hg, hb);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_rectangle(cr, bx + r, by + 1, bw - r*2, 2);
    cairo_fill(cr);
}

// ── icon: camera ─────────────────────────────────────────────────────────────
static void icon_camera(cairo_t *cr, double cx, double cy,
                          double sz, uint32_t col) {
    double bw = sz * 0.70, bh = sz * 0.50;
    double bx = cx - bw/2,  by = cy - bh/2 + sz*0.05;
    double r  = sz * 0.07;

    // shadow
    csrc(cr, 0x000000, 0.28);
    cairo_rrect(cr, bx+2, by+2, bw, bh, r);
    cairo_fill(cr);

    // body
    csrc(cr, col, 1.0);
    cairo_rrect(cr, bx, by, bw, bh, r);
    cairo_fill(cr);

    // viewfinder bump (top centre)
    double vw = bw * 0.32, vh = sz * 0.11;
    cairo_rrect(cr, cx - vw/2, by - vh + 1, vw, vh + r*0.5, r*0.5);
    cairo_fill(cr);

    // highlight
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_rectangle(cr, bx + r, by + 2, bw - r*2, 2);
    cairo_fill(cr);

    // lens: dark outer ring
    double lcy = cy + sz*0.05;
    double lr  = sz * 0.18;
    csrc(cr, darken(col, 0.55), 1.0);
    cairo_arc(cr, cx, lcy, lr, 0, 2*M_PI);
    cairo_fill(cr);

    // lens: mid ring (slightly lighter than body)
    csrc(cr, lighten(col, 0.12), 1.0);
    cairo_arc(cr, cx, lcy, lr * 0.78, 0, 2*M_PI);
    cairo_fill(cr);

    // lens: dark iris
    csrc(cr, darken(col, 0.30), 1.0);
    cairo_arc(cr, cx, lcy, lr * 0.52, 0, 2*M_PI);
    cairo_fill(cr);

    // lens: glint
    cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
    cairo_arc(cr, cx - lr*0.22, lcy - lr*0.22, lr * 0.18, 0, 2*M_PI);
    cairo_fill(cr);
}

// ── icon: burst (3×3 dot grid) ───────────────────────────────────────────────
static void icon_burst(cairo_t *cr, double cx, double cy,
                        double sz, uint32_t col) {
    double r  = sz * 0.07;
    double sp = sz * 0.28;
    for (int row = -1; row <= 1; row++) {
        for (int c2 = -1; c2 <= 1; c2++) {
            double ox = cx + c2 * sp;
            double oy = cy + row * sp;
            double alpha = (row == 0 && c2 == 0) ? 1.0 : 0.75;
            csrc(cr, col, alpha);
            cairo_arc(cr, ox, oy, r, 0, 2*M_PI);
            cairo_fill(cr);
        }
    }
}

// ── icon: video ──────────────────────────────────────────────────────────────
static void icon_video(cairo_t *cr, double cx, double cy,
                        double sz, uint32_t col) {
    double bw = sz * 0.44, bh = sz * 0.42;
    double bx = cx - sz*0.30, by = cy - bh/2;
    double r  = sz * 0.06;

    // shadow
    csrc(cr, 0x000000, 0.28);
    cairo_rrect(cr, bx+2, by+2, bw, bh, r);
    cairo_fill(cr);

    // body
    csrc(cr, col, 1.0);
    cairo_rrect(cr, bx, by, bw, bh, r);
    cairo_fill(cr);

    // lens on body
    double lcy = cy, lcx = bx + bw*0.5;
    double lr = bh * 0.26;
    csrc(cr, darken(col, 0.50), 1.0);
    cairo_arc(cr, lcx, lcy, lr, 0, 2*M_PI);
    cairo_fill(cr);
    csrc(cr, lighten(col, 0.20), 1.0);
    cairo_arc(cr, lcx, lcy, lr * 0.60, 0, 2*M_PI);
    cairo_fill(cr);

    // play-head triangle
    double tx = bx + bw + 3;
    double th = bh;
    csrc(cr, col, 1.0);
    cairo_move_to(cr, tx,            by);
    cairo_line_to(cr, tx,            by + th);
    cairo_line_to(cr, tx + sz*0.28,  by + th/2);
    cairo_close_path(cr);
    cairo_fill(cr);
}

// ── icon: shutter ring ───────────────────────────────────────────────────────
static void icon_shutter(cairo_t *cr, double cx, double cy,
                           double sz, uint32_t col) {
    double r  = sz * 0.30;
    double r2 = r  * 0.70;

    // outer ring (2.5px stroke)
    csrc(cr, col, 1.0);
    cairo_set_line_width(cr, sz * 0.055);
    cairo_arc(cr, cx, cy, r, 0, 2*M_PI);
    cairo_stroke(cr);

    // inner fill
    cairo_arc(cr, cx, cy, r2, 0, 2*M_PI);
    cairo_fill(cr);
}

// ── icon: record dot ─────────────────────────────────────────────────────────
static void icon_record_dot(cairo_t *cr, double cx, double cy,
                              double sz, uint32_t col) {
    double r = sz * 0.28;

    // outer ring
    csrc(cr, darken(col, 0.25), 1.0);
    cairo_set_line_width(cr, sz * 0.06);
    cairo_arc(cr, cx, cy, r, 0, 2*M_PI);
    cairo_stroke(cr);

    // filled dot
    csrc(cr, col, 1.0);
    cairo_arc(cr, cx, cy, r * 0.68, 0, 2*M_PI);
    cairo_fill(cr);
}

// ── icon: stop square ────────────────────────────────────────────────────────
static void icon_stop(cairo_t *cr, double cx, double cy,
                       double sz, uint32_t col) {
    double hw = sz * 0.26;

    // outer ring
    csrc(cr, darken(col, 0.22), 1.0);
    cairo_set_line_width(cr, sz * 0.06);
    cairo_arc(cr, cx, cy, hw + sz*0.10, 0, 2*M_PI);
    cairo_stroke(cr);

    // rounded square
    csrc(cr, col, 1.0);
    cairo_rrect(cr, cx - hw, cy - hw, hw*2, hw*2, sz*0.05);
    cairo_fill(cr);
}

// ── button ───────────────────────────────────────────────────────────────────

static void draw_button(Display *dpy, Window win, GC gc,
                        int btn_id, int x, int y, int w, int h,
                        AppState *state) {
    uint32_t bg = current_theme.button_bg;
    if (g_press == btn_id)      bg = darken(bg, 0.28f);
    else if (g_hover == btn_id) bg = lighten(bg, 0.18f);

    // background + border via Xlib (no alpha needed)
    int radius = 10;
    fill_rounded_rect(dpy, win, gc, bg, x, y, w, h, radius);

    // inner highlight
    set_fg(dpy, gc, lighten(bg, 0.22f));
    XDrawLine(dpy, win, gc, x + radius, y + 1, x + w - radius, y + 1);

    stroke_rounded_rect_xlib(dpy, win, gc, current_theme.button_border,
                              x, y, w, h, radius);

    // Cairo icon — draw into a surface covering the button area
    double sz = h * 0.62;
    CairoCtx c = cairo_begin(dpy, win, x, y, w, h);
    // re-express cx/cy in surface-local coords (after translate by x,y)
    double lcx = w / 2.0, lcy = h / 2.0;

    cairo_set_antialias(c.cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_cap(c.cr,  CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(c.cr, CAIRO_LINE_JOIN_ROUND);

    uint32_t ic = current_theme.button_text;

    switch (btn_id) {
        case PANEL_PHOTOS:
            icon_folder(c.cr, lcx, lcy, sz, ic);
            break;
        case PANEL_MODE:
            switch (state->mode) {
                case Mode::Photo:   icon_camera (c.cr, lcx, lcy, sz, ic); break;
                case Mode::Burst:   icon_burst  (c.cr, lcx, lcy, sz, ic); break;
                case Mode::Video:   icon_video  (c.cr, lcx, lcy, sz, ic); break;
            }
            break;
        case PANEL_SHUTTER:
            if (state->recording)
                icon_stop      (c.cr, lcx, lcy, sz, 0xff4444);
            else if (state->mode == Mode::Video)
                icon_record_dot(c.cr, lcx, lcy, sz, 0xff4444);
            else
                icon_shutter   (c.cr, lcx, lcy, sz, ic);
            break;
    }
    cairo_end(c);

}

// ── public API ───────────────────────────────────────────────────────────────

void panel_draw(AppState *state, Display *dpy, Window win, GC gc) {
    if (!state->panel_visible) return;

    PanelLayout l = panel_layout(state);

    set_fg(dpy, gc, current_theme.panel_bg);
    XFillRectangle(dpy, win, gc, l.px, 0, l.pw, l.ph);

    set_fg(dpy, gc, current_theme.panel_separator);
    XDrawLine(dpy, win, gc, l.px, 0, l.px, l.ph);

    set_fg(dpy, gc, lighten(current_theme.panel_bg, 0.08f));
    XDrawLine(dpy, win, gc, l.px + 1, 0, l.px + 1, l.ph);

    draw_button(dpy, win, gc, PANEL_PHOTOS,  l.bx, l.y_photos,  l.bw, l.bh, state);
    draw_button(dpy, win, gc, PANEL_MODE,    l.bx, l.y_mode,    l.bw, l.bh, state);
    draw_button(dpy, win, gc, PANEL_SHUTTER, l.bx, l.y_shutter, l.bw, l.bh, state);
}

int panel_hittest(AppState *state, int x, int y) {
    if (!state->panel_visible) return -1;
    if (x < state->win_w)     return -1;

    PanelLayout l = panel_layout(state);
    if (x < l.bx || x >= l.bx + l.bw) return -1;

    if (y >= l.y_photos  && y < l.y_photos  + l.bh) return PANEL_PHOTOS;
    if (y >= l.y_mode    && y < l.y_mode    + l.bh) return PANEL_MODE;
    if (y >= l.y_shutter && y < l.y_shutter + l.bh) return PANEL_SHUTTER;

    return -1;
}
