#pragma once

#include <cstdint>
#include <string>
#include "sehn.h"

// Theme colors and full styling used by UI modules.
struct Theme {
    uint32_t background;     // fallback background color (0xRRGGBB)
    uint32_t foreground;     // default text color

    // panel styles
    uint32_t panel_bg;       // panel background
    uint32_t panel_separator;// panel separator line
    uint32_t button_bg;      // panel button background
    uint32_t button_border;  // button border
    uint32_t button_text;    // button label color
    int      panel_padding;  // inner padding for panel
    int      panel_button_size; // preferred button size (square)

    // overlay styles
    uint32_t overlay_text;   // overlay text
    uint32_t rec_color;      // recording indicator color
    int      overlay_font_size;
    int      overlay_margin;

    // icons (optional)
    std::string icon_menu;
    std::string icon_mode;
    std::string icon_shutter;

    // scale (for HiDPI support)
    float    dpi_scale;

    std::string font;        // font name
};

// Current loaded theme (defaults provided)
extern Theme current_theme;

// Load and apply a named theme from ~/.config/sehn/themes.toml (or provided dir via XDG).
// Returns 0 on success, -1 on failure.
int theme_apply(AppState *state, const char *theme_name);

// Parse a CSS-style or hex color string such as "#rrggbb" or "0xrrggbb".
// Returns 0 on parse error.
uint32_t theme_parse_color(const char *s);
