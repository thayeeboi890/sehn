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
#include "theme.h"
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <cstdio>
#include <cstring>
#include <ctime>

static XftFont   *g_font = nullptr;
static XftDraw   *g_draw = nullptr;
static Display   *g_dpy  = nullptr;
static Window     g_win  = 0;

void overlay_init(Display *dpy, Window win, const char *font_path) {
    g_dpy = dpy;
    g_win = win;
    if (font_path && *font_path)
        g_font = XftFontOpen(dpy, DefaultScreen(dpy),
                             XFT_FILE,   XftTypeString, font_path,
                             XFT_SIZE,   XftTypeDouble, 12.0,
                             XFT_ANTIALIAS, XftTypeBool, True,
                             NULL);
    if (!g_font)
        g_font = XftFontOpenName(dpy, DefaultScreen(dpy), "monospace:size=10");
    g_draw = XftDrawCreate(dpy, win,
                           DefaultVisual(dpy, DefaultScreen(dpy)),
                           DefaultColormap(dpy, DefaultScreen(dpy)));
}

void overlay_cleanup() {
    if (g_draw) { XftDrawDestroy(g_draw); g_draw = nullptr; }
    if (g_font) { XftFontClose(g_dpy, g_font); g_font = nullptr; }
}

static void xft_draw_string(uint32_t color, int x, int y, const char *text) {
    if (!g_draw || !g_font || !text) return;
    XftColor xc;
    XRenderColor rc = {
        (unsigned short)((color >> 16 & 0xff) * 257),
        (unsigned short)((color >>  8 & 0xff) * 257),
        (unsigned short)((color       & 0xff) * 257),
        0xffff
    };
    XftColorAllocValue(g_dpy,
                       DefaultVisual(g_dpy, DefaultScreen(g_dpy)),
                       DefaultColormap(g_dpy, DefaultScreen(g_dpy)),
                       &rc, &xc);
    XftDrawStringUtf8(g_draw, &xc, g_font, x, y,
                      (const FcChar8 *)text, (int)strlen(text));
    XftColorFree(g_dpy,
                 DefaultVisual(g_dpy, DefaultScreen(g_dpy)),
                 DefaultColormap(g_dpy, DefaultScreen(g_dpy)),
                 &xc);
}

static const char* mode_str(Mode m)
{
    switch (m) {
    case Mode::Photo:
        return "PHOTO";
    case Mode::Burst:
        return "BURST";
    case Mode::Video:
        return "VIDEO";
    default:
        return "???";
    }
}

void overlay_draw(AppState* state, Display* dpy, Window win, GC gc)
{
    (void)dpy;
    (void)win;
    (void)gc;

    if (!state->overlay_visible)
        return;

    char buf[128];

    // top-left: mode
    snprintf(buf, sizeof(buf), "%s", mode_str(state->mode));
    xft_draw_string(current_theme.overlay_text, 8, 20, buf);

    // top-left second line: resolution
    snprintf(buf, sizeof(buf), "%ux%u @ %ufps", state->width, state->height, state->framerate);
    xft_draw_string(current_theme.overlay_text, 8, 36, buf);

    // top-left third line: recording indicator
    if (state->recording) {
        snprintf(buf, sizeof(buf), "● REC");
        xft_draw_string(current_theme.rec_color, 8, 52, buf);
    }

    // bottom-left: timestamp
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    int margin = current_theme.overlay_margin > 0 ? current_theme.overlay_margin : 8;
    xft_draw_string(current_theme.overlay_text, margin, state->win_h - margin, buf);

    // center: transient notification (if any)
    if (!state->notification.empty()) {
        if (state->notification_until > now) {
            // approximate centering using character width ~8 px
            int len = (int)strlen(state->notification.c_str());
            int x = state->win_w / 2 - (len * 8) / 2;
            if (x < 8)
                x = 8;
            xft_draw_string(current_theme.overlay_text, x, state->win_h / 2,
                            state->notification.c_str());
        }
        else {
            state->notification.clear();
        }
    }
}
