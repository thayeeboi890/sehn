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

    int pw = state->panel_width; // panel width
    int ph = state->win_h;       // panel height
    int px = state->win_w;       // panel x offset (right of viewfinder)
    int bw = pw - 4;             // button width
    int bh = pw - 4;             // button height (square)
    // if theme provides a preferred button size, use that (clamped to panel width)
    if (current_theme.panel_button_size > 0) {
        bh = current_theme.panel_button_size;
        if (bh > pw - 4)
            bh = pw - 4;
        bw = bh;
    }
    int pad = current_theme.panel_padding > 0 ? current_theme.panel_padding : 4;

    // panel background
    XSetForeground(dpy, gc, current_theme.panel_bg);
    XFillRectangle(dpy, win, gc, px, 0, pw, ph);

    // separator line between viewfinder and panel
    XSetForeground(dpy, gc, current_theme.panel_separator);
    XDrawLine(dpy, win, gc, px, 0, px, ph);

    // ── hamburger menu — top ─────────────────────────────────────────────────
    draw_button(dpy, win, gc, px + pad, pad, bw, bh, "=", false);

    // ── mode button — middle ─────────────────────────────────────────────────
    int mid_y = ph / 2 - bh / 2;
    draw_button(dpy, win, gc, px + pad, mid_y, bw, bh, mode_short(state->mode), false);

    // ── shutter button — bottom ──────────────────────────────────────────────
    int bot_y = ph - bh - pad;
    draw_button(dpy, win, gc, px + pad, bot_y, bw, bh, state->recording ? "■" : "●",
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

    int pw = state->panel_width;
    int ph = state->win_h;
    int bh = pw - 4;

    // hamburger
    if (y >= 4 && y < 4 + bh)
        return PANEL_MENU;
    // mode
    int mid_y = ph / 2 - bh / 2;
    if (y >= mid_y && y < mid_y + bh)
        return PANEL_MODE;
    // shutter
    int bot_y = ph - bh - 4;
    if (y >= bot_y && y < bot_y + bh)
        return PANEL_SHUTTER;

    return -1;
}
