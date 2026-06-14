/* sehn.cpp

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

#include "sehn.h"

AppState make_default_state()
{
    AppState s{};

    s.device = "/dev/video0";
    s.camera_fd = -1;
    s.width = 1280;
    s.height = 720;
    s.framerate = 30;
    s.v4l2_format = "mjpeg";
    s.mode = Mode::Photo;
    s.recording = false;
    s.paused = false;
    s.fullscreen = false;
    s.borderless = false;
    s.panel_visible = true;
    s.overlay_visible = false;
    s.hide_pointer = false;
    s.flip_horizontal = false;
    s.flip_vertical = false;
    s.rotate_deg = 0;
    s.panel_width = 64;
    s.win_w = 960;
    s.win_h = 540;
    s.zoom_mode = ZoomMode::Percent; /* start 1:1 (no scaling) for sharpness */
    s.zoom = 1.0f;
    s.pan_x = 0;
    s.pan_y = 0;
    s.output_dir = ".";
    s.filename_pattern = "sehn_%Y%m%d_%H%M%S_##";
    s.save_format = "jpeg";
    s.jpeg_quality = 92;
    s.png_compression = 6;
    s.burst_count = 10;
    s.burst_interval_ms = 100;
    s.video_format = "mkv";
    s.audio_device = "";
    s.autofocus = true;
    s.exposure_mode = "auto";
    s.exposure_time = 10000;
    s.gain = 0;
    s.wb_mode = "auto";
    s.wb_temp = 5500;
    s.sig_capture = false;
    s.sig_next_mode = false;
    s.sig_reload_config = false;
    s.list_devices_and_exit = false;
    s.list_formats_and_exit = false;
    s.list_controls_and_exit = false;
    s.print_config_and_exit = false;
    s.verbose = false;
    s.quiet = false;
    s.running = true;
    s.notification_until = 0;
    return s;
}
