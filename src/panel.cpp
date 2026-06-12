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

*/

#include "panel.h"
#include "theme.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdio>
#include <cstring>

struct PanelLayout {
    int pw;
    int ph;
    int px;
    int bw;
    int bh;
    int pad;
    int top_y;
    int mid_y;
    int bot_y;
};

static PanelLayout panel_layout(AppState* state)
{
    PanelLayout l{};
    l.pw = state->panel_width;
    l.ph = state->win_h;
    l.px = state->win_w;
    l.bw = l.pw - 4;
    l.bh = l.pw - 4;

    if (current_theme.panel_button_size > 0) {
        l.bh = current_theme.panel_button_size;
        if (l.bh > l.pw - 4)
            l.bh = l.pw - 4;
        l.bw = l.bh;
    }

    l.pad = current_theme.panel_padding > 0 ? current_theme.panel_padding : 4;
    l.top_y = l.pad;
    l.mid_y = l.ph / 2 - l.bh / 2;
    l.bot_y = l.ph - l.bh - l.pad;
    return l;
}

// panel is a vertical strip on the right edge of the window.
// layout (top to bottom):
//   [ ☰ ]  hamburger menu       — top
//   [ ⊙ ]  mode indicator       — middle
//   [ ◉ ]  shutter button       — bottom

static void draw_button(Display* dpy, Window win, GC gc, int x, int y, int w, int h,
                        const char* label, bool active)
{
    // background
    XSetForeground(dpy, gc, active ? current_theme.button_bg : current_theme.button_bg);
    XFillRectangle(dpy, win, gc, x, y, w, h);

    // border
    XSetForeground(dpy, gc, current_theme.button_border);
    XDrawRectangle(dpy, win, gc, x, y, w - 1, h - 1);

    // label centered (approximate — no font metrics, just eyeball it)
    XSetForeground(dpy, gc, current_theme.button_text);
    int lx = x + (w - (int)strlen(label) * 6) / 2;
    int ly = y + h / 2 + 4;
    XDrawString(dpy, win, gc, lx, ly, label, strlen(label));
}

static const char* mode_short(Mode m)
{
    switch (m) {
    case Mode::Photo:
        return "PHO";
    case Mode::Burst:
        return "BST";
    case Mode::Video:
        return "VID";
    case Mode::Preview:
        return "PRV";
    default:
        return "???";
    }
}

void panel_draw(AppState* state, Display* dpy, Window win, GC gc)
{
    if (!state->panel_visible)
        return;

    PanelLayout l = panel_layout(state);

    // panel background
    XSetForeground(dpy, gc, current_theme.panel_bg);
    XFillRectangle(dpy, win, gc, l.px, 0, l.pw, l.ph);

    // separator line between viewfinder and panel
    XSetForeground(dpy, gc, current_theme.panel_separator);
    XDrawLine(dpy, win, gc, l.px, 0, l.px, l.ph);

    // ── hamburger menu — top ─────────────────────────────────────────────────
    draw_button(dpy, win, gc, l.px + l.pad, l.top_y, l.bw, l.bh, "=", false);

    // ── mode button — middle ─────────────────────────────────────────────────
    draw_button(dpy, win, gc, l.px + l.pad, l.mid_y, l.bw, l.bh, mode_short(state->mode), false);

    // ── shutter button — bottom ──────────────────────────────────────────────
    draw_button(dpy, win, gc, l.px + l.pad, l.bot_y, l.bw, l.bh, state->recording ? "■" : "●",
                state->recording);
}

// hit-test: returns which panel element was clicked, or -1 for none
// call from ui.cpp mouse handler
int panel_hittest(AppState* state, int x, int y)
{
    if (!state->panel_visible)
        return -1;
    if (x < state->win_w)
        return -1; // not in panel

    PanelLayout l = panel_layout(state);
    int button_x = l.px + l.pad;
    if (x < button_x || x >= button_x + l.bw)
        return -1;

    // hamburger
    if (y >= l.top_y && y < l.top_y + l.bh)
        return PANEL_MENU;
    // mode
    if (y >= l.mid_y && y < l.mid_y + l.bh)
        return PANEL_MODE;
    // shutter
    if (y >= l.bot_y && y < l.bot_y + l.bh)
        return PANEL_SHUTTER;

    return -1;
}
