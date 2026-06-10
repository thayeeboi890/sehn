/* cli.cpp

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

#include "cli.h"
#include "utils.h"
#include "version.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>

static void print_usage()
{
    printf("Usage: sehn [options] [device]\n"
           "\n"
           "Options:\n"
           "  -d, --device <path>         camera device (default: /dev/video0)\n"
           "  -o, --output-dir <dir>      save directory\n"
           "  -f, --format <fmt>          capture format: mjpeg, yuyv, nv12, h264\n"
           "  -r, --resolution <WxH>      capture resolution\n"
           "  -R, --framerate <fps>       frames per second\n"
           "  -m, --mode <mode>           photo, burst, video, preview\n"
           "  -F, --fullscreen            start fullscreen\n"
           "  -x, --borderless            no window decorations\n"
           "  -T, --theme <name>          load named theme\n"
           "  -c, --config <file>         config file path\n"
           "  -g, --geometry <WxH>        window size\n"
           "      --no-panel              hide panel\n"
           "      --panel-width <px>      panel width in pixels\n"
           "      --overlay               enable info overlay\n"
           "      --no-overlay            disable info overlay\n"
           "      --hide-pointer          hide cursor in viewfinder\n"
           "      --filename <pattern>    filename pattern (strftime + %%c)\n"
           "      --jpeg-quality <1-100>  JPEG quality (default: 92)\n"
           "      --png-compression <0-9> PNG compression (default: 6)\n"
           "      --save-format <fmt>     jpeg or png\n"
           "      --video-format <fmt>    mkv, mp4, avi\n"
           "      --burst-count <n>       frames in burst mode\n"
           "      --burst-interval <ms>   ms between burst frames\n"
           "      --list-devices          list V4L2 devices and exit\n"
           "      --list-formats          list device formats and exit\n"
           "      --list-controls         list device controls and exit\n"
           "      --print-config          dump resolved config and exit\n"
           "  -q, --quiet                 suppress warnings\n"
           "  -V, --verbose               verbose output\n"
           "  -v, --version               print version and exit\n"
           "  -h, --help                  print this help and exit\n");
}

// long-only option ids (above ASCII range to avoid conflicts)
enum {
    OPT_NO_PANEL = 256,
    OPT_PANEL_WIDTH,
    OPT_OVERLAY,
    OPT_NO_OVERLAY,
    OPT_HIDE_POINTER,
    OPT_FILENAME,
    OPT_JPEG_QUALITY,
    OPT_PNG_COMPRESSION,
    OPT_SAVE_FORMAT,
    OPT_VIDEO_FORMAT,
    OPT_BURST_COUNT,
    OPT_BURST_INTERVAL,
    OPT_LIST_DEVICES,
    OPT_LIST_FORMATS,
    OPT_LIST_CONTROLS,
    OPT_PRINT_CONFIG,
};

static const struct option long_opts[] = {
    {"device", required_argument, nullptr, 'd'},
    {"output-dir", required_argument, nullptr, 'o'},
    {"format", required_argument, nullptr, 'f'},
    {"resolution", required_argument, nullptr, 'r'},
    {"framerate", required_argument, nullptr, 'R'},
    {"mode", required_argument, nullptr, 'm'},
    {"fullscreen", no_argument, nullptr, 'F'},
    {"borderless", no_argument, nullptr, 'x'},
    {"theme", required_argument, nullptr, 'T'},
    {"config", required_argument, nullptr, 'c'},
    {"geometry", required_argument, nullptr, 'g'},
    {"quiet", no_argument, nullptr, 'q'},
    {"verbose", no_argument, nullptr, 'V'},
    {"version", no_argument, nullptr, 'v'},
    {"help", no_argument, nullptr, 'h'},
    {"no-panel", no_argument, nullptr, OPT_NO_PANEL},
    {"panel-width", required_argument, nullptr, OPT_PANEL_WIDTH},
    {"overlay", no_argument, nullptr, OPT_OVERLAY},
    {"no-overlay", no_argument, nullptr, OPT_NO_OVERLAY},
    {"hide-pointer", no_argument, nullptr, OPT_HIDE_POINTER},
    {"filename", required_argument, nullptr, OPT_FILENAME},
    {"jpeg-quality", required_argument, nullptr, OPT_JPEG_QUALITY},
    {"png-compression", required_argument, nullptr, OPT_PNG_COMPRESSION},
    {"save-format", required_argument, nullptr, OPT_SAVE_FORMAT},
    {"video-format", required_argument, nullptr, OPT_VIDEO_FORMAT},
    {"burst-count", required_argument, nullptr, OPT_BURST_COUNT},
    {"burst-interval", required_argument, nullptr, OPT_BURST_INTERVAL},
    {"list-devices", no_argument, nullptr, OPT_LIST_DEVICES},
    {"list-formats", no_argument, nullptr, OPT_LIST_FORMATS},
    {"list-controls", no_argument, nullptr, OPT_LIST_CONTROLS},
    {"print-config", no_argument, nullptr, OPT_PRINT_CONFIG},
    {nullptr, 0, nullptr, 0}};

int cli_parse(int argc, char* argv[], AppState* state)
{
    LOG_FN();
    int opt;
    while ((opt = getopt_long(argc, argv, "d:o:f:r:R:m:FxT:c:g:qVvh", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'd':
            state->device = optarg;
            break;
        case 'o':
            state->output_dir = optarg;
            break;
        case 'f':
            state->v4l2_format = optarg;
            break;
        case 'r': {
            unsigned w = 0, h = 0;
            if (sscanf(optarg, "%ux%u", &w, &h) == 2) {
                state->width = w;
                state->height = h;
            }
            else {
                LOG_ERROR("invalid resolution '%s', expected WxH", optarg);
                return 1;
            }
            break;
        }
        case 'R':
            state->framerate = (uint32_t)atoi(optarg);
            break;
        case 'm': {
            if (strcmp(optarg, "photo") == 0)
                state->mode = Mode::Photo;
            else if (strcmp(optarg, "burst") == 0)
                state->mode = Mode::Burst;
            else if (strcmp(optarg, "video") == 0)
                state->mode = Mode::Video;
            else if (strcmp(optarg, "preview") == 0)
                state->mode = Mode::Preview;
            else {
                LOG_ERROR("unknown mode '%s'", optarg);
                return 1;
            }
            break;
        }
        case 'F':
            state->fullscreen = true;
            break;
        case 'x':
            state->borderless = true;
            break;
        case 'T':
            state->theme = optarg;
            break;
        case 'c':
            state->config_path = optarg;
            break;
        case 'g': {
            int w = 0, h = 0;
            if (sscanf(optarg, "%dx%d", &w, &h) == 2) {
                state->win_w = w;
                state->win_h = h;
            }
            else {
                LOG_ERROR("invalid geometry '%s', expected WxH", optarg);
                return 1;
            }
            break;
        }
        case 'q':
            state->quiet = true;
            break;
        case 'V':
            state->verbose = true;
            break;
        case 'v':
            sehn_print_version();
            exit(0);
        case 'h':
            print_usage();
            exit(0);

        case OPT_NO_PANEL:
            state->panel_visible = false;
            break;
        case OPT_PANEL_WIDTH:
            state->panel_width = atoi(optarg);
            break;
        case OPT_OVERLAY:
            state->overlay_visible = true;
            break;
        case OPT_NO_OVERLAY:
            state->overlay_visible = false;
            break;
        case OPT_HIDE_POINTER:
            state->hide_pointer = true;
            break;
        case OPT_FILENAME:
            state->filename_pattern = optarg;
            break;
        case OPT_JPEG_QUALITY:
            state->jpeg_quality = atoi(optarg);
            break;
        case OPT_PNG_COMPRESSION:
            state->png_compression = atoi(optarg);
            break;
        case OPT_SAVE_FORMAT:
            state->save_format = optarg;
            break;
        case OPT_VIDEO_FORMAT:
            state->video_format = optarg;
            break;
        case OPT_BURST_COUNT:
            state->burst_count = atoi(optarg);
            break;
        case OPT_BURST_INTERVAL:
            state->burst_interval_ms = atoi(optarg);
            break;
        case OPT_LIST_DEVICES:
            state->list_devices_and_exit = true;
            break;
        case OPT_LIST_FORMATS:
            state->list_formats_and_exit = true;
            break;
        case OPT_LIST_CONTROLS:
            state->list_controls_and_exit = true;
            break;
        case OPT_PRINT_CONFIG:
            state->print_config_and_exit = true;
            break;

        default:
            LOG_ERROR("unknown option, try --help");
            return 1;
        }
    }

    // positional argument = device path
    if (optind < argc)
        state->device = argv[optind];

    return 0;
}
