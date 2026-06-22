#pragma once

namespace resources {

constexpr const char* sehnrc = R"(# TODO: add comments

[camera]
device      = "/dev/video0"
format      = "mjpeg"
resolution  = "1280x720"
framerate   = 30
autofocus   = true
exposure    = "auto"
wb          = "auto"
wb_temp     = 5500
gain        = 0

[capture]
output_dir      = "~/Pictures/sehn"
filename        = "sehn_%Y%m%d_%H%M%S_##"
save_format     = "jpeg"
video_format    = "mkv"
jpeg_quality    = 92
png_compression = 6
burst_count     = 10
burst_interval  = 100

[audio]
device = ""

[ui]
theme         = "dark"
mode          = "photo"
fullscreen    = false
borderless    = false
panel         = true
panel_width   = 64
overlay       = false
hide_pointer  = false
window_width  = 960
window_height = 540
)";

constexpr const char* themes = R"([dark]
[dark.ui]
background = "#000000"
foreground = "#ffffff"
panel_bg = "#1a1a1a"
panel_separator = "#444444"
button_bg = "#222222"
button_border = "#666666"
button_text = "#ffffff"
panel_padding = 8
panel_button_size = 56
overlay_text = "#ffffff"
rec_color = "#ff0000"
overlay_font_size = 12
overlay_margin = 8
font = "fixed"

[dark.ui.icons]
menu = ""
mode = ""
shutter = ""

[dark.variants.compact]
[dark.variants.compact.ui]
panel_padding = 4
panel_button_size = 40
overlay_font_size = 10

[light]
[light.ui]
background = "#ffffff"
foreground = "#000000"
panel_bg = "#f0f0f0"
panel_separator = "#cccccc"
button_bg = "#e0e0e0"
button_border = "#888888"
button_text = "#000000"
panel_padding = 8
panel_button_size = 56
overlay_text = "#000000"
rec_color = "#ff0000"
overlay_font_size = 12
overlay_margin = 8
font = "fixed"

[light.ui.icons]
menu = ""
mode = ""
shutter = ""
)";

constexpr const char* keys = R"([keys]
quit              = ["q", "Escape"]
capture           = ["space", "c"]
next_mode         = ["m"]
prev_mode         = ["M"]
toggle_overlay    = ["o"]
toggle_fullscreen = ["f"]
zoom_in           = ["plus"]
zoom_out          = ["minus"]
zoom_fit          = ["z"]
pan_left          = ["Left"]
pan_right         = ["Right"]
pan_up            = ["Up"]
pan_down          = ["Down"]
flip_horizontal   = ["h"]
flip_vertical     = ["v"]
rotate_cw         = ["r"]
rotate_ccw        = ["R"]
toggle_exposure   = ["e"]
exposure_up       = ["bracketright"]
exposure_down     = ["bracketleft"]
gain_up           = ["braceright"]
gain_down         = ["braceleft"]
toggle_wb         = ["w"]
wb_warmer         = ["period"]
wb_cooler         = ["comma"]
toggle_autofocus  = ["a"]
)";

} // namespace resources
