/* keys.cpp

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

#include "keys.h"
#include "utils.h"
#include <X11/keysym.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../subprojects/tomlc99/toml.h"
}

static std::string default_keys_path() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else {
        const char *home = getenv("HOME");
        if (!home) return "";
        base = std::string(home) + "/.config";
    }
    return base + "/sehn/keys.toml";
}

KeyMap keys_defaults() {
    KeyMap km;
    km[XK_q]         = Action::Quit;
    km[XK_Escape]    = Action::Quit;
    km[XK_space]     = Action::Capture;
    km[XK_c]         = Action::Capture;
    km[XK_m]         = Action::NextMode;
    km[XK_M]         = Action::PrevMode;
    km[XK_Tab]       = Action::TogglePanel;
    km[XK_o]         = Action::ToggleOverlay;
    km[XK_f]         = Action::ToggleFullscreen;
    km[XK_plus]      = Action::ZoomIn;
    km[XK_minus]     = Action::ZoomOut;
    km[XK_z]         = Action::ZoomFit;
    km[XK_Z]         = Action::ZoomFill;
    km[XK_asterisk]  = Action::Zoom100;
    km[XK_Left]      = Action::PanLeft;
    km[XK_Right]     = Action::PanRight;
    km[XK_Up]        = Action::PanUp;
    km[XK_Down]      = Action::PanDown;
    km[XK_h]         = Action::FlipHorizontal;
    km[XK_v]         = Action::FlipVertical;
    km[XK_r]         = Action::RotateCW;
    km[XK_R]         = Action::RotateCCW;
    km[XK_e]         = Action::ToggleExposure;
    km[XK_bracketleft]  = Action::ExposureDown;
    km[XK_bracketright] = Action::ExposureUp;
    km[XK_braceleft]    = Action::GainDown;
    km[XK_braceright]   = Action::GainUp;
    km[XK_w]         = Action::ToggleWB;
    km[XK_comma]     = Action::WBCooler;
    km[XK_period]    = Action::WBWarmer;
    km[XK_a]         = Action::ToggleAutofocus;
    km[XK_i]         = Action::ToggleInfo;
    return km;
}

// map action name string -> Action enum
static Action action_from_str(const char *s) {
    if (!s) return Action::Unknown;
    if (strcmp(s, "quit")              == 0) return Action::Quit;
    if (strcmp(s, "capture")           == 0) return Action::Capture;
    if (strcmp(s, "next_mode")         == 0) return Action::NextMode;
    if (strcmp(s, "prev_mode")         == 0) return Action::PrevMode;
    if (strcmp(s, "toggle_panel")      == 0) return Action::TogglePanel;
    if (strcmp(s, "toggle_overlay")    == 0) return Action::ToggleOverlay;
    if (strcmp(s, "toggle_fullscreen") == 0) return Action::ToggleFullscreen;
    if (strcmp(s, "zoom_in")           == 0) return Action::ZoomIn;
    if (strcmp(s, "zoom_out")          == 0) return Action::ZoomOut;
    if (strcmp(s, "zoom_fit")          == 0) return Action::ZoomFit;
    if (strcmp(s, "zoom_fill")         == 0) return Action::ZoomFill;
    if (strcmp(s, "zoom_100")          == 0) return Action::Zoom100;
    if (strcmp(s, "pan_left")          == 0) return Action::PanLeft;
    if (strcmp(s, "pan_right")         == 0) return Action::PanRight;
    if (strcmp(s, "pan_up")            == 0) return Action::PanUp;
    if (strcmp(s, "pan_down")          == 0) return Action::PanDown;
    if (strcmp(s, "flip_horizontal")   == 0) return Action::FlipHorizontal;
    if (strcmp(s, "flip_vertical")     == 0) return Action::FlipVertical;
    if (strcmp(s, "rotate_cw")         == 0) return Action::RotateCW;
    if (strcmp(s, "rotate_ccw")        == 0) return Action::RotateCCW;
    if (strcmp(s, "toggle_exposure")   == 0) return Action::ToggleExposure;
    if (strcmp(s, "exposure_up")       == 0) return Action::ExposureUp;
    if (strcmp(s, "exposure_down")     == 0) return Action::ExposureDown;
    if (strcmp(s, "gain_up")           == 0) return Action::GainUp;
    if (strcmp(s, "gain_down")         == 0) return Action::GainDown;
    if (strcmp(s, "toggle_wb")         == 0) return Action::ToggleWB;
    if (strcmp(s, "wb_warmer")         == 0) return Action::WBWarmer;
    if (strcmp(s, "wb_cooler")         == 0) return Action::WBCooler;
    if (strcmp(s, "toggle_autofocus")  == 0) return Action::ToggleAutofocus;
    if (strcmp(s, "toggle_info")       == 0) return Action::ToggleInfo;
    return Action::Unknown;
}

KeyMap keys_load(AppState *state) { LOG_FN();
    KeyMap km = keys_defaults();

    std::string p = default_keys_path();
    if (!state->config_path.empty()) {
        // derive keys.toml from config path dir
        size_t slash = state->config_path.rfind('/');
        if (slash != std::string::npos)
            p = state->config_path.substr(0, slash) + "/keys.toml";
    }

    FILE *fp = fopen(p.c_str(), "r");
    if (!fp) return km;  // no keys.toml, use defaults

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!root) {
        LOG_ERROR("keys.toml parse error: %s", errbuf);
        return km;
    }

    toml_table_t *keys = toml_table_in(root, "keys");
    if (!keys) { toml_free(root); return km; }

    // walk every key in [keys] table
    for (int i = 0; ; i++) {
        const char *action_name = toml_key_in(keys, i);
        if (!action_name) break;

        Action action = action_from_str(action_name);
        if (action == Action::Unknown) {
            LOG_WARN("unknown action '%s' in keys.toml", action_name);
            continue;
        }

        toml_array_t *arr = toml_array_in(keys, action_name);
        if (!arr) continue;

        // remove existing bindings for this action first
        for (auto it = km.begin(); it != km.end(); ) {
            if (it->second == action) it = km.erase(it);
            else ++it;
        }

        // add new bindings
        for (int j = 0; ; j++) {
            toml_datum_t d = toml_string_at(arr, j);
            if (!d.ok) break;
            KeySym sym = XStringToKeysym(d.u.s);
            free(d.u.s);
            if (sym == NoSymbol) {
                LOG_WARN("unknown keysym in keys.toml");
                continue;
            }
            km[sym] = action;
        }
    }

    toml_free(root);
    return km;
}

Action keys_lookup(const KeyMap &km, KeySym sym) {
    auto it = km.find(sym);
    return it != km.end() ? it->second : Action::Unknown;
}
