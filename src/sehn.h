/* sehn.h

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

#pragma once

#include <string>
#include <cstdint>

// forward declarations
struct CameraState;
struct UIState;
struct Config;

enum class Mode {
    Photo,
    Burst,
    Video,
    Preview,
};

enum class ZoomMode {
    Fit,
    Fill,
    Percent,
};

struct AppState {
    // --- camera ---
    std::string device;      
    int         camera_fd;      
    uint32_t    width;
    uint32_t    height;
    uint32_t    framerate;
    std::string v4l2_format;   

    // --- mode ---
    Mode        mode;
    bool        recording;    
    bool        paused;      

    // --- ui ---
    bool        fullscreen;
    bool        borderless;
    bool        panel_visible;
    bool        overlay_visible;
    bool        hide_pointer;
    int         panel_width;
    int         win_w, win_h;

    // --- zoom/pan ---
    ZoomMode    zoom_mode;
    float       zoom;       
    int         pan_x, pan_y;

    // --- capture ---
    std::string output_dir;
    std::string filename_pattern;
    std::string save_format;    
    int         jpeg_quality;
    int         png_compression;
    int         burst_count;
    int         burst_interval_ms;
    std::string video_format; 

    // --- camera controls ---
    bool        autofocus;
    std::string exposure_mode; 
    int         exposure_time;
    int         gain;
    std::string wb_mode;     
    int         wb_temp;    

    // --- signals ---
    volatile bool sig_capture;
    volatile bool sig_next_mode;
    volatile bool sig_reload_config;

    // --- early exit flags ---
    bool list_devices_and_exit; 
    bool list_formats_and_exit;
    bool list_controls_and_exit;
    bool print_config_and_exit;

    // --- misc ---
    bool        verbose;
    bool        quiet;
    bool        running;   
    std::string theme;        
    std::string config_path; 
};

AppState make_default_state();
