/* config.cpp

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

#include "config.h"
extern "C" {
#include "../subprojects/tomlc99/toml.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include "utils.h"

// resolve ~/.config/sehn/sehnrc.toml via XDG
static std::string default_config_path() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg)
        base = xdg;
    else {
        const char *home = getenv("HOME");
        if (!home) return "";
        base = std::string(home) + "/.config";
    }
    return base + "/sehn/sehnrc.toml";
}

static std::string toml_str(toml_table_t *t, const char *key,
                              const std::string &fallback) {
    toml_datum_t d = toml_string_in(t, key);
    if (d.ok) {
        std::string s(d.u.s);
        free(d.u.s);
        return s;
    }
    return fallback;
}

static int toml_int(toml_table_t *t, const char *key, int fallback) {
    toml_datum_t d = toml_int_in(t, key);
    return d.ok ? (int)d.u.i : fallback;
}

static bool toml_bool(toml_table_t *t, const char *key, bool fallback) {
    toml_datum_t d = toml_bool_in(t, key);
    return d.ok ? (bool)d.u.b : fallback;
}

int config_load(AppState *state, const char *path) {
    std::string p = path ? path : default_config_path();
    if (p.empty()) return -1;

    FILE *fp = fopen(p.c_str(), "r");
    if (!fp) return -1;  // non-fatal, no config file is fine

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        LOG_ERROR("config parse error: %s", errbuf);
        return -2;
    }

    // ── [camera] ─────────────────────────────────────────────────────────────
    toml_table_t *cam = toml_table_in(root, "camera");
    if (cam) {
        state->device      = toml_str(cam,  "device",     state->device);
        state->v4l2_format = toml_str(cam,  "format",     state->v4l2_format);
        state->framerate   = (uint32_t)toml_int(cam, "framerate", state->framerate);
        state->autofocus   = toml_bool(cam, "autofocus",  state->autofocus);
        state->exposure_mode = toml_str(cam, "exposure",  state->exposure_mode);
        state->wb_mode     = toml_str(cam,  "wb",         state->wb_mode);
        state->wb_temp     = toml_int(cam,  "wb_temp",    state->wb_temp);
        state->gain        = toml_int(cam,  "gain",       state->gain);

        // resolution is "WxH" string
        std::string res = toml_str(cam, "resolution", "");
        if (!res.empty()) {
            unsigned w = 0, h = 0;
            if (sscanf(res.c_str(), "%ux%u", &w, &h) == 2) {
                state->width  = w;
                state->height = h;
            }
        }
    }

    // ── [capture] ────────────────────────────────────────────────────────────
    toml_table_t *cap = toml_table_in(root, "capture");
    if (cap) {
        state->output_dir        = toml_str(cap,  "output_dir",       state->output_dir);
        state->filename_pattern  = toml_str(cap,  "filename",         state->filename_pattern);
        state->save_format       = toml_str(cap,  "save_format",      state->save_format);
        state->video_format      = toml_str(cap,  "video_format",     state->video_format);
        state->jpeg_quality      = toml_int(cap,  "jpeg_quality",     state->jpeg_quality);
        state->png_compression   = toml_int(cap,  "png_compression",  state->png_compression);
        state->burst_count       = toml_int(cap,  "burst_count",      state->burst_count);
        state->burst_interval_ms = toml_int(cap,  "burst_interval",   state->burst_interval_ms);
    }

    // ── [ui] ─────────────────────────────────────────────────────────────────
    toml_table_t *ui = toml_table_in(root, "ui");
    if (ui) {
        state->fullscreen      = toml_bool(ui, "fullscreen",    state->fullscreen);
        state->borderless      = toml_bool(ui, "borderless",    state->borderless);
        state->panel_visible   = toml_bool(ui, "panel",         state->panel_visible);
        state->panel_width     = toml_int(ui,  "panel_width",   state->panel_width);
        state->overlay_visible = toml_bool(ui, "overlay",       state->overlay_visible);
        state->hide_pointer    = toml_bool(ui, "hide_pointer",  state->hide_pointer);
        state->win_w           = toml_int(ui,  "window_width",  state->win_w);
        state->win_h           = toml_int(ui,  "window_height", state->win_h);

        std::string mode = toml_str(ui, "mode", "");
        if      (mode == "photo")   state->mode = Mode::Photo;
        else if (mode == "burst")   state->mode = Mode::Burst;
        else if (mode == "video")   state->mode = Mode::Video;
        else if (mode == "preview") state->mode = Mode::Preview;
    }

    toml_free(root);
    return 0;
}

int config_apply_theme(AppState *state, const char *theme_name) {
    std::string p = default_config_path();
    // themes live in the same dir as sehnrc.toml
    size_t slash = p.rfind('/');
    if (slash == std::string::npos) return -1;
    p = p.substr(0, slash) + "/themes.toml";

    FILE *fp = fopen(p.c_str(), "r");
    if (!fp) return -1;

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!root) return -1;

    toml_table_t *theme = toml_table_in(root, theme_name);
    if (!theme) {
        LOG_WARN("theme '%s' not found", theme_name);
        toml_free(root);
        return -1;
    }

    // re-use config_load sections by building a fake root with just this theme
    // simplest approach: just parse the same keys
    toml_table_t *cam = toml_table_in(theme, "camera");
    toml_table_t *cap = toml_table_in(theme, "capture");
    toml_table_t *ui  = toml_table_in(theme, "ui");

    if (cam) {
        state->device      = toml_str(cam, "device",     state->device);
        state->v4l2_format = toml_str(cam, "format",     state->v4l2_format);
        state->framerate   = (uint32_t)toml_int(cam, "framerate", state->framerate);
    }
    if (cap) {
        state->save_format = toml_str(cap, "save_format", state->save_format);
        state->jpeg_quality = toml_int(cap, "jpeg_quality", state->jpeg_quality);
    }
    if (ui) {
        state->fullscreen    = toml_bool(ui, "fullscreen", state->fullscreen);
        state->borderless    = toml_bool(ui, "borderless", state->borderless);
        state->overlay_visible = toml_bool(ui, "overlay",  state->overlay_visible);
    }

    toml_free(root);
    return 0;
}

void config_print(const AppState *state) {
    printf("# sehn resolved config\n\n");

    printf("[camera]\n");
    printf("device      = \"%s\"\n", state->device.c_str());
    printf("format      = \"%s\"\n", state->v4l2_format.c_str());
    printf("resolution  = \"%ux%u\"\n", state->width, state->height);
    printf("framerate   = %u\n",        state->framerate);
    printf("autofocus   = %s\n",        state->autofocus ? "true" : "false");
    printf("exposure    = \"%s\"\n",    state->exposure_mode.c_str());
    printf("wb          = \"%s\"\n",    state->wb_mode.c_str());
    printf("wb_temp     = %d\n",        state->wb_temp);
    printf("gain        = %d\n\n",      state->gain);

    printf("[capture]\n");
    printf("output_dir      = \"%s\"\n", state->output_dir.c_str());
    printf("filename        = \"%s\"\n", state->filename_pattern.c_str());
    printf("save_format     = \"%s\"\n", state->save_format.c_str());
    printf("video_format    = \"%s\"\n", state->video_format.c_str());
    printf("jpeg_quality    = %d\n",     state->jpeg_quality);
    printf("png_compression = %d\n",     state->png_compression);
    printf("burst_count     = %d\n",     state->burst_count);
    printf("burst_interval  = %d\n\n",   state->burst_interval_ms);

    printf("[ui]\n");
    printf("mode          = \"%s\"\n", state->mode == Mode::Photo   ? "photo"   :
                                        state->mode == Mode::Burst   ? "burst"   :
                                        state->mode == Mode::Video   ? "video"   : "preview");
    printf("fullscreen    = %s\n",  state->fullscreen      ? "true" : "false");
    printf("borderless    = %s\n",  state->borderless      ? "true" : "false");
    printf("panel         = %s\n",  state->panel_visible   ? "true" : "false");
    printf("panel_width   = %d\n",  state->panel_width);
    printf("overlay       = %s\n",  state->overlay_visible ? "true" : "false");
    printf("hide_pointer  = %s\n",  state->hide_pointer    ? "true" : "false");
    printf("window_width  = %d\n",  state->win_w);
    printf("window_height = %d\n",  state->win_h);
}
