/* menu.cpp

Copyright (C) 2026 Santiago Silva.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software and its documentation and
acknowledgment shall be given in the documentation and software packages that
this Software was used.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "menu.h"
#include "camera.h"
#include "config.h"
#include "theme.h"
#include "utils.h"
#include "version.h"

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrender.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>

// ── constants ─────────────────────────────────────────────────────────────────

#define MENU_ITEM_H      26
#define MENU_MIN_W       180
#define MENU_PAD_X       14
#define MENU_PAD_Y       4
#define MENU_SEP_H       9
#define MENU_RADIUS      6
#define MENU_FONT_SIZE   11.0

// ── Xft state ────────────────────────────────────────────────────────────────

static XftFont  *g_font  = nullptr;
static Display  *g_dpy   = nullptr;

void menu_init(Display *dpy, Window /*win*/, const char *font_path) {
    g_dpy = dpy;
    if (font_path && *font_path)
        g_font = XftFontOpen(dpy, DefaultScreen(dpy),
                             XFT_FILE,      XftTypeString,  font_path,
                             XFT_SIZE,      XftTypeDouble,  MENU_FONT_SIZE,
                             XFT_ANTIALIAS, XftTypeBool,    True,
                             NULL);
    if (!g_font)
        g_font = XftFontOpenName(dpy, DefaultScreen(dpy),
                                 "monospace:size=10");
}

void menu_cleanup(Display *dpy) {
    if (g_font) { XftFontClose(dpy, g_font); g_font = nullptr; }
}

// ── item model ────────────────────────────────────────────────────────────────

enum class ItemType {
    Action,      // clickable leaf
    Separator,   // horizontal rule
    Header,      // grey section label (non-clickable)
    Submenu,     // opens a child menu
};

enum class ActionID {
    Noop,
    // File
    OpenOutputDir,
    SaveFormatJPEG,
    SaveFormatPNG,
    Quit,
    // Camera
    CameraDevice,
    FormatMJPEG,
    FormatYUYV,
    FPS30,
    FPS60,
    FPS15,
    // Audio
    AudioDevice,
    AudioNone,
    // Controls
    ToggleAutofocus,
    ExposureAuto,
    ExposureManual,
    WBAuto,
    WBManual,
    FlipH,
    FlipV,
    Rotate90,
    Rotate180,
    Rotate270,
    RotateReset,
    // View
    ToggleFullscreen,
    ZoomFit,
    ZoomFill,
    Zoom100,
};

struct MenuItem {
    ItemType    type      = ItemType::Action;
    ActionID    action    = ActionID::Noop;
    int         payload   = 0;
    std::string label;
    std::string sublabel;
    bool        checked   = false;
    bool        disabled  = false;
    std::vector<MenuItem> children;
};

// ── device enumeration ────────────────────────────────────────────────────────

struct VideoDevice {
    std::string path;
    std::string name;
};

static std::vector<VideoDevice> enum_video_devices() {
    std::vector<VideoDevice> devs;
    for (int i = 0; i < 16; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        struct v4l2_capability pcap = {};
        if (ioctl(fd, VIDIOC_QUERYCAP, &pcap) == 0 &&
            (pcap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            VideoDevice d;
            d.path = path;
            d.name = (const char *)pcap.card;
            devs.push_back(d);
        }
        close(fd);
    }
    return devs;
}

// ── PulseAudio source enumeration ────────────────────────────────────────────

struct PASourceList {
    std::vector<std::string> names;
    std::vector<std::string> descs;
    bool done = false;
};

static void pa_source_cb(pa_context * /*c*/, const pa_source_info *i,
                          int eol, void *userdata) {
    PASourceList *lst = (PASourceList *)userdata;
    if (eol) { lst->done = true; return; }
    if (i) {
        lst->names.push_back(i->name);
        lst->descs.push_back(i->description ? i->description : i->name);
    }
}

static void pa_ctx_cb(pa_context *c, void *userdata) {
    pa_context_state_t st = pa_context_get_state(c);
    if (st == PA_CONTEXT_READY) {
        PASourceList *lst = (PASourceList *)userdata;
        pa_context_get_source_info_list(c, pa_source_cb, lst);
    }
}

static PASourceList enum_pa_sources() {
    PASourceList lst;
    pa_mainloop *ml = pa_mainloop_new();
    if (!ml) return lst;
    pa_mainloop_api *api = pa_mainloop_get_api(ml);
    pa_context *ctx = pa_context_new(api, "sehn-menu");
    if (!ctx) { pa_mainloop_free(ml); return lst; }
    pa_context_set_state_callback(ctx, pa_ctx_cb, &lst);
    pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    int ret = 0;
    for (int i = 0; i < 1000 && !lst.done; i++) {
        pa_mainloop_iterate(ml, 0, &ret);
        usleep(1000);
    }
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return lst;
}

// ── menu builder ──────────────────────────────────────────────────────────────

static MenuItem make_sep() {
    MenuItem m; m.type = ItemType::Separator; return m;
}
static MenuItem make_action(ActionID id, const char *label,
                             bool checked = false, bool disabled = false,
                             const char *sublabel = nullptr, int payload = 0) {
    MenuItem m;
    m.type     = ItemType::Action;
    m.action   = id;
    m.label    = label;
    m.checked  = checked;
    m.disabled = disabled;
    m.payload  = payload;
    if (sublabel) m.sublabel = sublabel;
    return m;
}
static MenuItem make_submenu(const char *label, std::vector<MenuItem> children) {
    MenuItem m;
    m.type     = ItemType::Submenu;
    m.label    = label;
    m.children = std::move(children);
    return m;
}

static std::vector<MenuItem> build_menu(AppState *state) {
    auto vdevs = enum_video_devices();
    auto asrcs = enum_pa_sources();

    // ── File ─────────────────────────────────────────────────────────────────
    std::vector<MenuItem> file_items;
    file_items.push_back(make_action(ActionID::OpenOutputDir, "Open output folder"));
    {
        std::vector<MenuItem> fmt;
        fmt.push_back(make_action(ActionID::SaveFormatJPEG, "JPEG",
                                  state->save_format == "jpeg"));
        fmt.push_back(make_action(ActionID::SaveFormatPNG,  "PNG",
                                  state->save_format == "png"));
        file_items.push_back(make_submenu("Save format", std::move(fmt)));
    }
    file_items.push_back(make_sep());
    file_items.push_back(make_action(ActionID::Quit, "Quit"));

    // ── Camera ───────────────────────────────────────────────────────────────
    std::vector<MenuItem> cam_items;
    {
        std::vector<MenuItem> cams;
        for (int i = 0; i < (int)vdevs.size(); i++) {
            std::string lbl = vdevs[i].path + "  " + vdevs[i].name;
            cams.push_back(make_action(ActionID::CameraDevice, lbl.c_str(),
                                       state->device == vdevs[i].path,
                                       false, nullptr, i));
        }
        if (cams.empty())
            cams.push_back(make_action(ActionID::Noop, "No cameras found", false, true));
        cam_items.push_back(make_submenu("Select camera", std::move(cams)));
    }
    {
        std::vector<MenuItem> fmt;
        fmt.push_back(make_action(ActionID::FormatMJPEG, "MJPEG",
                                  state->v4l2_format == "mjpeg"));
        fmt.push_back(make_action(ActionID::FormatYUYV,  "YUYV",
                                  state->v4l2_format == "yuyv"));
        cam_items.push_back(make_submenu("Pixel format", std::move(fmt)));
    }
    {
        std::vector<MenuItem> fps;
        fps.push_back(make_action(ActionID::FPS15, "15 fps", state->framerate == 15));
        fps.push_back(make_action(ActionID::FPS30, "30 fps", state->framerate == 30));
        fps.push_back(make_action(ActionID::FPS60, "60 fps", state->framerate == 60));
        cam_items.push_back(make_submenu("Frame rate", std::move(fps)));
    }

    // ── Audio ─────────────────────────────────────────────────────────────────
    std::vector<MenuItem> audio_items;
    {
        std::vector<MenuItem> srcs;
        srcs.push_back(make_action(ActionID::AudioNone, "None",
                                   state->audio_device.empty()));
        for (int i = 0; i < (int)asrcs.names.size(); i++) {
            srcs.push_back(make_action(ActionID::AudioDevice,
                                       asrcs.descs[i].c_str(),
                                       state->audio_device == asrcs.names[i],
                                       false, nullptr, i));
        }
        if (asrcs.names.empty())
            srcs.push_back(make_action(ActionID::Noop, "No sources found", false, true));
        audio_items.push_back(make_submenu("Audio source", std::move(srcs)));
    }

    // ── Controls ──────────────────────────────────────────────────────────────
    std::vector<MenuItem> ctrl_items;
    ctrl_items.push_back(make_action(ActionID::ToggleAutofocus, "Autofocus",
                                     state->autofocus));
    {
        std::vector<MenuItem> exp;
        exp.push_back(make_action(ActionID::ExposureAuto,   "Auto",
                                  state->exposure_mode == "auto"));
        exp.push_back(make_action(ActionID::ExposureManual, "Manual",
                                  state->exposure_mode == "manual"));
        ctrl_items.push_back(make_submenu("Exposure", std::move(exp)));
    }
    {
        std::vector<MenuItem> wb;
        wb.push_back(make_action(ActionID::WBAuto,   "Auto",
                                 state->wb_mode == "auto"));
        wb.push_back(make_action(ActionID::WBManual, "Manual",
                                 state->wb_mode == "manual"));
        ctrl_items.push_back(make_submenu("White balance", std::move(wb)));
    }
    {
        std::vector<MenuItem> rot;
        rot.push_back(make_action(ActionID::RotateReset, "None",
                                  state->rotate_deg == 0));
        rot.push_back(make_action(ActionID::Rotate90,  "90°  CW",
                                  state->rotate_deg == 90));
        rot.push_back(make_action(ActionID::Rotate180, "180°",
                                  state->rotate_deg == 180));
        rot.push_back(make_action(ActionID::Rotate270, "270° CW",
                                  state->rotate_deg == 270));
        rot.push_back(make_sep());
        rot.push_back(make_action(ActionID::FlipH, "Flip horizontal",
                                  state->flip_horizontal));
        rot.push_back(make_action(ActionID::FlipV, "Flip vertical",
                                  state->flip_vertical));
        ctrl_items.push_back(make_submenu("Transform", std::move(rot)));
    }

    // ── View ──────────────────────────────────────────────────────────────────
    std::vector<MenuItem> view_items;
    view_items.push_back(make_action(ActionID::ToggleFullscreen, "Fullscreen",
                                     state->fullscreen));
    view_items.push_back(make_sep());
    {
        std::vector<MenuItem> zm;
        zm.push_back(make_action(ActionID::ZoomFit,  "Fit",
                                 state->zoom_mode == ZoomMode::Fit));
        zm.push_back(make_action(ActionID::ZoomFill, "Fill",
                                 state->zoom_mode == ZoomMode::Fill));
        zm.push_back(make_action(ActionID::Zoom100,  "100%",
                                 state->zoom_mode == ZoomMode::Percent &&
                                 state->zoom == 1.0f));
        view_items.push_back(make_submenu("Zoom", std::move(zm)));
    }

    // ── Top-level ────────────────────────────────────────────────────────────
    std::vector<MenuItem> top;
    top.push_back(make_submenu("File",     std::move(file_items)));
    top.push_back(make_submenu("Camera",   std::move(cam_items)));
    top.push_back(make_submenu("Audio",    std::move(audio_items)));
    top.push_back(make_submenu("Controls", std::move(ctrl_items)));
    top.push_back(make_submenu("View",     std::move(view_items)));
    return top;
}

// ── action dispatcher ─────────────────────────────────────────────────────────

static std::vector<VideoDevice> g_vdevs;
static PASourceList              g_asrcs;

static void dispatch(AppState *state, const MenuItem &item) {
    switch (item.action) {
        case ActionID::OpenOutputDir:
            system(("xdg-open " + state->output_dir + " &").c_str());
            break;
        case ActionID::SaveFormatJPEG: state->save_format = "jpeg"; break;
        case ActionID::SaveFormatPNG:  state->save_format = "png";  break;
        case ActionID::Quit:           state->running = false;       break;

        case ActionID::CameraDevice:
            if (item.payload >= 0 && item.payload < (int)g_vdevs.size()) {
                state->device = g_vdevs[item.payload].path;
                state->notification = "Camera change takes effect on restart";
                state->notification_until = time(nullptr) + 3;
            }
            break;

        case ActionID::FormatMJPEG:
            state->v4l2_format = "mjpeg";
            state->notification = "Format change takes effect on restart";
            state->notification_until = time(nullptr) + 3;
            break;
        case ActionID::FormatYUYV:
            state->v4l2_format = "yuyv";
            state->notification = "Format change takes effect on restart";
            state->notification_until = time(nullptr) + 3;
            break;

        case ActionID::FPS15:
            state->framerate = 15;
            state->notification = "FPS change takes effect on restart";
            state->notification_until = time(nullptr) + 3;
            break;
        case ActionID::FPS30:
            state->framerate = 30;
            state->notification = "FPS change takes effect on restart";
            state->notification_until = time(nullptr) + 3;
            break;
        case ActionID::FPS60:
            state->framerate = 60;
            state->notification = "FPS change takes effect on restart";
            state->notification_until = time(nullptr) + 3;
            break;

        case ActionID::AudioNone:   state->audio_device = ""; break;
        case ActionID::AudioDevice:
            if (item.payload >= 0 && item.payload < (int)g_asrcs.names.size())
                state->audio_device = g_asrcs.names[item.payload];
            break;

        case ActionID::ToggleAutofocus:
            state->autofocus = !state->autofocus;
            camera_apply_controls(state);
            break;
        case ActionID::ExposureAuto:
            state->exposure_mode = "auto";
            camera_apply_controls(state);
            break;
        case ActionID::ExposureManual:
            state->exposure_mode = "manual";
            camera_apply_controls(state);
            break;
        case ActionID::WBAuto:
            state->wb_mode = "auto";
            camera_apply_controls(state);
            break;
        case ActionID::WBManual:
            state->wb_mode = "manual";
            camera_apply_controls(state);
            break;
        case ActionID::FlipH:        state->flip_horizontal = !state->flip_horizontal; break;
        case ActionID::FlipV:        state->flip_vertical   = !state->flip_vertical;   break;
        case ActionID::Rotate90:     state->rotate_deg = 90;  break;
        case ActionID::Rotate180:    state->rotate_deg = 180; break;
        case ActionID::Rotate270:    state->rotate_deg = 270; break;
        case ActionID::RotateReset:  state->rotate_deg = 0;   break;

        case ActionID::ToggleFullscreen:
            state->fullscreen = !state->fullscreen;
            break;
        case ActionID::ZoomFit:
            state->zoom_mode = ZoomMode::Fit;
            break;
        case ActionID::ZoomFill:
            state->zoom_mode = ZoomMode::Fill;
            break;
        case ActionID::Zoom100:
            state->zoom_mode = ZoomMode::Percent;
            state->zoom = 1.0f;
            break;
        default: break;
    }
}

// ── drawing helpers ───────────────────────────────────────────────────────────

static bool g_open = false;
bool menu_is_open() { return g_open; }

static int text_width(Display *dpy, const char *s) {
    if (!g_font || !s || !*s) return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, g_font, (const FcChar8 *)s, (int)strlen(s), &ext);
    return ext.width;
}

static void xft_draw_string_at(Display *dpy, XftDraw *draw, uint32_t color,
                                int x, int y, const char *text) {
    if (!draw || !g_font || !text || !*text) return;
    XftColor xc;
    XRenderColor rc = {
        (unsigned short)(((color >> 16) & 0xff) * 257),
        (unsigned short)(((color >>  8) & 0xff) * 257),
        (unsigned short)(( color        & 0xff) * 257),
        0xffff
    };
    XftColorAllocValue(dpy,
                       DefaultVisual(dpy, DefaultScreen(dpy)),
                       DefaultColormap(dpy, DefaultScreen(dpy)),
                       &rc, &xc);
    XftDrawStringUtf8(draw, &xc, g_font, x, y,
                      (const FcChar8 *)text, (int)strlen(text));
    XftColorFree(dpy,
                 DefaultVisual(dpy, DefaultScreen(dpy)),
                 DefaultColormap(dpy, DefaultScreen(dpy)),
                 &xc);
}

static int items_height(const std::vector<MenuItem> &items) {
    int h = MENU_PAD_Y;
    for (auto &it : items)
        h += (it.type == ItemType::Separator) ? MENU_SEP_H : MENU_ITEM_H;
    h += MENU_PAD_Y;
    return h;
}

static int items_width(Display *dpy, const std::vector<MenuItem> &items) {
    int w = MENU_MIN_W;
    for (auto &it : items) {
        if (it.type == ItemType::Separator || it.type == ItemType::Header) continue;
        int tw = text_width(dpy, it.label.c_str()) + MENU_PAD_X * 2 + 20;
        if (!it.sublabel.empty())
            tw += text_width(dpy, it.sublabel.c_str()) + MENU_PAD_X;
        if (it.type == ItemType::Submenu) tw += 14;
        if (tw > w) w = tw;
    }
    return w;
}

static void fill_round(Display *dpy, Window win, GC gc, uint32_t col,
                        int x, int y, int w, int h, int r) {
    XSetForeground(dpy, gc, col);
    if (r <= 0) { XFillRectangle(dpy, win, gc, x, y, w, h); return; }
    XFillRectangle(dpy, win, gc, x+r,   y,     w-r*2, h);
    XFillRectangle(dpy, win, gc, x,     y+r,   w,     h-r*2);
    XFillArc(dpy, win, gc, x,       y,       r*2, r*2, 90*64,  90*64);
    XFillArc(dpy, win, gc, x+w-r*2, y,       r*2, r*2, 0,      90*64);
    XFillArc(dpy, win, gc, x,       y+h-r*2, r*2, r*2, 180*64, 90*64);
    XFillArc(dpy, win, gc, x+w-r*2, y+h-r*2, r*2, r*2, 270*64, 90*64);
}

static void draw_arrow(Display *dpy, Window win, GC gc, uint32_t col,
                        int x, int y, int sz) {
    XSetForeground(dpy, gc, col);
    XPoint pts[3] = {
        { (short)x,      (short)(y - sz/2) },
        { (short)x,      (short)(y + sz/2) },
        { (short)(x+sz), (short)y          }
    };
    XFillPolygon(dpy, win, gc, pts, 3, Convex, CoordModeOrigin);
}

static void draw_check(Display *dpy, Window win, GC gc, uint32_t col, int x, int y) {
    XSetForeground(dpy, gc, col);
    XDrawLine(dpy, win, gc, x,   y+4, x+3, y+7);
    XDrawLine(dpy, win, gc, x+3, y+7, x+8, y);
    XDrawLine(dpy, win, gc, x,   y+5, x+3, y+8);
    XDrawLine(dpy, win, gc, x+3, y+8, x+8, y+1);
}

// ── hittest ───────────────────────────────────────────────────────────────────

static int hittest_items(const std::vector<MenuItem> &items, int my) {
    int y = MENU_PAD_Y;
    for (int i = 0; i < (int)items.size(); i++) {
        int row_h = (items[i].type == ItemType::Separator) ? MENU_SEP_H : MENU_ITEM_H;
        if (my >= y && my < y + row_h) {
            if (items[i].type == ItemType::Separator ||
                items[i].type == ItemType::Header    ||
                items[i].disabled)
                return -1;
            return i;
        }
        y += row_h;
    }
    return -1;
}

static int item_y(const std::vector<MenuItem> &items, int idx) {
    int y = MENU_PAD_Y;
    for (int i = 0; i < (int)items.size(); i++) {
        if (i == idx) return y;
        y += (items[i].type == ItemType::Separator) ? MENU_SEP_H : MENU_ITEM_H;
    }
    return y;
}

// ── popup window ──────────────────────────────────────────────────────────────

struct PopupCtx {
    Display              *dpy;
    Window                win;
    GC                    gc;
    XftDraw              *draw;
    std::vector<MenuItem> items;
    int                   w, h;
    int                   hover;        // hovered item index, -1 = none
    int                   open_sub;     // index of currently open submenu, -1 = none
    int                   origin_x, origin_y;
};

static Window create_popup(Display *dpy, int screen, int x, int y, int w, int h) {
    XSetWindowAttributes attrs = {};
    attrs.override_redirect  = True;
    attrs.background_pixel   = 0;
    attrs.border_pixel        = 0;
    attrs.colormap            = DefaultColormap(dpy, screen);
    Window win = XCreateWindow(dpy, RootWindow(dpy, screen),
                               x, y, w, h, 0,
                               DefaultDepth(dpy, screen),
                               InputOutput,
                               DefaultVisual(dpy, screen),
                               CWOverrideRedirect | CWBackPixel |
                               CWBorderPixel      | CWColormap,
                               &attrs);
    XSelectInput(dpy, win,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | LeaveWindowMask | KeyPressMask);
    XMapRaised(dpy, win);
    return win;
}

static void draw_popup(PopupCtx &ctx) {
    Display *dpy = ctx.dpy;
    Window   win = ctx.win;
    GC       gc  = ctx.gc;

    uint32_t bg     = current_theme.panel_bg;
    uint32_t border = current_theme.panel_separator;
    uint32_t fg     = current_theme.overlay_text;
    uint32_t fg_dim = 0x888888;
    uint32_t hi_bg  = current_theme.button_bg;
    uint32_t hi_fg  = current_theme.button_text;

    fill_round(dpy, win, gc, bg, 0, 0, ctx.w, ctx.h, MENU_RADIUS);
    XSetForeground(dpy, gc, border);
    XDrawRectangle(dpy, win, gc, 0, 0, ctx.w-1, ctx.h-1);

    int asc = g_font ? g_font->ascent : 12;
    int y   = MENU_PAD_Y;

    for (int i = 0; i < (int)ctx.items.size(); i++) {
        const MenuItem &it = ctx.items[i];

        if (it.type == ItemType::Separator) {
            XSetForeground(dpy, gc, border);
            XDrawLine(dpy, win, gc,
                      MENU_PAD_X,       y + MENU_SEP_H/2,
                      ctx.w-MENU_PAD_X, y + MENU_SEP_H/2);
            y += MENU_SEP_H;
            continue;
        }

        if (it.type == ItemType::Header) {
            xft_draw_string_at(dpy, ctx.draw, fg_dim,
                               MENU_PAD_X, y + asc + 2, it.label.c_str());
            y += MENU_ITEM_H;
            continue;
        }

        bool hov = (i == ctx.hover) || (i == ctx.open_sub);
        if (hov)
            fill_round(dpy, win, gc, hi_bg, 4, y, ctx.w-8, MENU_ITEM_H-2, 4);

        uint32_t text_col = it.disabled ? fg_dim : (hov ? hi_fg : fg);
        int text_y = y + asc + (MENU_ITEM_H - asc) / 2 - 1;

        if (it.checked)
            draw_check(dpy, win, gc,
                       hov ? hi_fg : current_theme.button_bg,
                       MENU_PAD_X - 2, y + (MENU_ITEM_H - 9) / 2);

        xft_draw_string_at(dpy, ctx.draw, text_col,
                           MENU_PAD_X + 12, text_y, it.label.c_str());

        if (!it.sublabel.empty()) {
            int sw = text_width(dpy, it.sublabel.c_str());
            xft_draw_string_at(dpy, ctx.draw, fg_dim,
                               ctx.w - MENU_PAD_X - sw -
                               (it.type == ItemType::Submenu ? 14 : 0),
                               text_y, it.sublabel.c_str());
        }

        if (it.type == ItemType::Submenu)
            draw_arrow(dpy, win, gc, text_col,
                       ctx.w - MENU_PAD_X - 8,
                       y + MENU_ITEM_H / 2, 6);

        y += MENU_ITEM_H;
    }

    XFlush(dpy);
}

// ── popup runner ─────────────────────────────────────────────────────────────

struct MenuLevel {
    PopupCtx ctx;
    int      parent_level = -1;
    int      parent_idx   = -1;
};

static PopupCtx create_popup_ctx(Display *dpy, int screen,
                                  const std::vector<MenuItem> &items,
                                  int x, int y) {
    PopupCtx ctx;
    ctx.dpy      = dpy;
    ctx.items    = items;
    ctx.w        = items_width(dpy, items);
    ctx.h        = items_height(items);
    ctx.hover    = -1;
    ctx.open_sub = -1;

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);
    if (x + ctx.w > sw) x = sw - ctx.w - 4;
    if (y + ctx.h > sh) y = sh - ctx.h - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    ctx.origin_x = x;
    ctx.origin_y = y;
    ctx.win  = create_popup(dpy, screen, x, y, ctx.w, ctx.h);
    ctx.gc   = XCreateGC(dpy, ctx.win, 0, nullptr);
    ctx.draw = XftDrawCreate(dpy, ctx.win,
                             DefaultVisual(dpy, screen),
                             DefaultColormap(dpy, screen));
    draw_popup(ctx);
    return ctx;
}

static void destroy_popup_ctx(PopupCtx &ctx) {
    if (ctx.draw) {
        XftDrawDestroy(ctx.draw);
        ctx.draw = nullptr;
    }
    if (ctx.gc) {
        XFreeGC(ctx.dpy, ctx.gc);
        ctx.gc = 0;
    }
    if (ctx.win != None) {
        XDestroyWindow(ctx.dpy, ctx.win);
        ctx.win = None;
    }
}

static int level_for_window(const std::vector<MenuLevel> &levels, Window win) {
    for (int i = 0; i < (int)levels.size(); i++) {
        if (levels[i].ctx.win == win) return i;
    }
    return -1;
}

static bool pointer_in_ctx(Display *dpy, const PopupCtx &ctx,
                            int *out_x = nullptr, int *out_y = nullptr) {
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (!XQueryPointer(dpy, ctx.win, &root_ret, &child_ret,
                       &root_x, &root_y, &win_x, &win_y, &mask))
        return false;
    if (out_x) *out_x = win_x;
    if (out_y) *out_y = win_y;
    return win_x >= 0 && win_x < ctx.w && win_y >= 0 && win_y < ctx.h;
}

static bool pointer_on_parent_row(Display *dpy,
                                   const std::vector<MenuLevel> &levels,
                                   int level_idx) {
    const MenuLevel &lvl = levels[level_idx];
    if (lvl.parent_level < 0 || lvl.parent_level >= (int)levels.size())
        return false;

    const PopupCtx &parent = levels[lvl.parent_level].ctx;
    int px, py;
    if (!pointer_in_ctx(dpy, parent, &px, &py))
        return false;
    return hittest_items(parent.items, py) == lvl.parent_idx;
}

static void close_levels_from(Display *dpy, std::vector<MenuLevel> &levels,
                               int first) {
    if (first < 0) first = 0;
    if (first >= (int)levels.size()) return;

    for (int i = (int)levels.size() - 1; i >= first; i--)
        destroy_popup_ctx(levels[i].ctx);
    levels.resize(first);

    if (!levels.empty() && first > 0) {
        levels[first - 1].ctx.open_sub = -1;
        draw_popup(levels[first - 1].ctx);
    }
    XFlush(dpy);
}

static void open_submenu(Display *dpy, int screen, std::vector<MenuLevel> &levels,
                          int level_idx, int item_idx) {
    PopupCtx &parent = levels[level_idx].ctx;
    if (parent.open_sub == item_idx && level_idx + 1 < (int)levels.size())
        return;

    close_levels_from(dpy, levels, level_idx + 1);

    const MenuItem &it = parent.items[item_idx];
    int child_w = items_width(dpy, it.children);
    int sw = DisplayWidth(dpy, screen);
    int x = parent.origin_x + parent.w;
    if (x + child_w > sw) x = parent.origin_x - child_w;
    int y = parent.origin_y + item_y(parent.items, item_idx);

    parent.open_sub = item_idx;
    draw_popup(parent);

    MenuLevel child;
    child.ctx = create_popup_ctx(dpy, screen, it.children, x, y);
    child.parent_level = level_idx;
    child.parent_idx = item_idx;
    levels.push_back(child);
}

static bool run_popup(Display *dpy, const std::vector<MenuItem> &items,
                       int screen_x, int screen_y, MenuItem *chosen) {
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    std::vector<MenuLevel> levels;
    levels.reserve(8);

    MenuLevel root_level;
    root_level.ctx = create_popup_ctx(dpy, screen, items, screen_x, screen_y);
    levels.push_back(root_level);

    XGrabPointer(dpy, root, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync,
                 None, None, CurrentTime);
    XGrabKeyboard(dpy, root, True,
                  GrabModeAsync, GrabModeAsync, CurrentTime);

    bool selected = false;
    bool done = false;

    while (!done) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        Window ev_win = ev.xany.window;
        int level_idx = level_for_window(levels, ev_win);

        if (ev.type == Expose && level_idx >= 0) {
            draw_popup(levels[level_idx].ctx);
        }
        else if (ev.type == MotionNotify && level_idx >= 0) {
            PopupCtx &ctx = levels[level_idx].ctx;
            int hit = hittest_items(ctx.items, ev.xmotion.y);
            if (hit != ctx.hover) {
                ctx.hover = hit;
                if (hit >= 0 && ctx.items[hit].type == ItemType::Submenu) {
                    open_submenu(dpy, screen, levels, level_idx, hit);
                } else {
                    close_levels_from(dpy, levels, level_idx + 1);
                    draw_popup(levels[level_idx].ctx);
                }
            }
        }
        else if (ev.type == LeaveNotify && level_idx > 0) {
            bool in_self = pointer_in_ctx(dpy, levels[level_idx].ctx);
            bool in_child = false;
            if (level_idx + 1 < (int)levels.size())
                in_child = pointer_in_ctx(dpy, levels[level_idx + 1].ctx);

            if (!in_self && !in_child &&
                !pointer_on_parent_row(dpy, levels, level_idx)) {
                close_levels_from(dpy, levels, level_idx);
            }
        }
        else if (ev.type == LeaveNotify && level_idx == 0) {
            if (levels.size() == 1) {
                levels[0].ctx.hover = -1;
                draw_popup(levels[0].ctx);
            }
        }
        else if (ev.type == ButtonRelease && level_idx >= 0) {
            PopupCtx &ctx = levels[level_idx].ctx;
            int hit = hittest_items(ctx.items, ev.xbutton.y);
            if (hit >= 0) {
                const MenuItem &it = ctx.items[hit];
                if (it.type == ItemType::Submenu) {
                    open_submenu(dpy, screen, levels, level_idx, hit);
                } else {
                    if (chosen) *chosen = it;
                    selected = true;
                    done = true;
                }
            }
        }
        else if (ev.type == ButtonPress && level_idx < 0) {
            done = true;
        }
        else if (ev.type == KeyPress) {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            if (sym == XK_Escape) done = true;
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    close_levels_from(dpy, levels, 0);
    return selected;
}

// ── public entry point ────────────────────────────────────────────────────────

void menu_show(AppState *state, Display *dpy, Window win, GC /*gc*/, int x, int y) {
    g_open = true;

    g_vdevs = enum_video_devices();
    g_asrcs = enum_pa_sources();

    auto items = build_menu(state);

    Window child;
    int sx, sy;
    XTranslateCoordinates(dpy, win, RootWindow(dpy, DefaultScreen(dpy)),
                          x, y, &sx, &sy, &child);

    MenuItem chosen;
    if (run_popup(dpy, items, sx, sy, &chosen) &&
        chosen.action != ActionID::Noop && !chosen.disabled)
        dispatch(state, chosen);

    g_open = false;
}
