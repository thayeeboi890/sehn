#include "theme.h"
extern "C" {
#include "../subprojects/tomlc99/toml.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "utils.h"


static std::string default_themes_path() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else {
        const char *home = getenv("HOME");
        if (!home) return "";
        base = std::string(home) + "/.config";
    }
    return base + "/sehn/themes.toml";
}

uint32_t theme_parse_color(const char *s) {
    if (!s) return 0;
    // accept "#rrggbb" or "0xrrggbb" or "rrggbb"
    const char *p = s;
    if (p[0] == '#') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    int r,g,b;
    if (sscanf(p, "%2x%2x%2x", &r, &g, &b) == 3) {
        return (uint32_t)((r<<16)|(g<<8)|b);
    }
    return 0;
}

int theme_apply(AppState *state, const char *theme_name) { LOG_FN();
    (void)state; // theme currently affects only drawing colors
    std::string p = default_themes_path();
    if (p.empty()) return -1;

    FILE *fp = fopen(p.c_str(), "r");
    if (!fp) return -1;

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!root) {
        LOG_WARN("failed parsing themes.toml: %s", errbuf);
        return -1;
    }

    toml_table_t *theme = toml_table_in(root, theme_name);
    if (!theme) {
        LOG_WARN("theme '%s' not found in %s", theme_name, p.c_str());
        toml_free(root);
        return -1;
    }

    // ui colors and extended UI settings
    toml_table_t *ui = toml_table_in(theme, "ui");
    if (ui) {
        // helper to read strings
        auto read_str = [&](toml_table_t *t, const char *k, const char *fallback)->std::string{
            toml_datum_t d = toml_string_in(t, k);
            if (d.ok) { std::string s(d.u.s); free(d.u.s); return s; }
            return std::string(fallback);
        };
        // color keys
        std::string bg = read_str(ui, "background", "");
        if (!bg.empty()) current_theme.background = theme_parse_color(bg.c_str());
        std::string fg = read_str(ui, "foreground", "");
        if (!fg.empty()) current_theme.foreground = theme_parse_color(fg.c_str());

        std::string panel_bg = read_str(ui, "panel_bg", "");
        if (!panel_bg.empty()) current_theme.panel_bg = theme_parse_color(panel_bg.c_str());
        std::string panel_sep = read_str(ui, "panel_separator", "");
        if (!panel_sep.empty()) current_theme.panel_separator = theme_parse_color(panel_sep.c_str());
        std::string btn_bg = read_str(ui, "button_bg", "");
        if (!btn_bg.empty()) current_theme.button_bg = theme_parse_color(btn_bg.c_str());
        std::string btn_border = read_str(ui, "button_border", "");
        if (!btn_border.empty()) current_theme.button_border = theme_parse_color(btn_border.c_str());
        std::string btn_text = read_str(ui, "button_text", "");
        if (!btn_text.empty()) current_theme.button_text = theme_parse_color(btn_text.c_str());

        std::string overlay = read_str(ui, "overlay_text", "");
        if (!overlay.empty()) current_theme.overlay_text = theme_parse_color(overlay.c_str());
        std::string rec = read_str(ui, "rec_color", "");
        if (!rec.empty()) current_theme.rec_color = theme_parse_color(rec.c_str());

        // optional numeric values
        toml_datum_t dpad = toml_int_in(ui, "panel_padding");
        if (dpad.ok) current_theme.panel_padding = (int)dpad.u.i;
        toml_datum_t dbtn = toml_int_in(ui, "panel_button_size");
        if (dbtn.ok) current_theme.panel_button_size = (int)dbtn.u.i;
        toml_datum_t ofsz = toml_int_in(ui, "overlay_font_size");
        if (ofsz.ok) current_theme.overlay_font_size = (int)ofsz.u.i;
        toml_datum_t om = toml_int_in(ui, "overlay_margin");
        if (om.ok) current_theme.overlay_margin = (int)om.u.i;
        toml_datum_t dpi = toml_int_in(ui, "dpi_scale");
        if (dpi.ok) current_theme.dpi_scale = (float)dpi.u.i;

        std::string font = read_str(ui, "font", "fixed");
        current_theme.font = font;

        // icons table
        toml_table_t *icons = toml_table_in(ui, "icons");
        if (icons) {
            current_theme.icon_menu = read_str(icons, "menu", "");
            current_theme.icon_mode = read_str(icons, "mode", "");
            current_theme.icon_shutter = read_str(icons, "shutter", "");
        }
    }

    // variants: if user requested a variant like "dark.compact", caller handles splitting and will call apply with base then variant table

    toml_free(root);
    LOG_DEBUG("theme applied: %s", theme_name);
    return 0;
}
