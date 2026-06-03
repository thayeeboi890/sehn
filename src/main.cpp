/* main.cpp

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>

#include "sehn.h"
#include "version.h"
#include "config.h"
#include "cli.h"
#include "camera.h"
#include "ui.h"
#include "signals.h"
#include "list.h"

static void print_version() {
    printf("sehn %s\n", SEHN_VERSION);
    printf("  exif:   %s\n", SEHN_FEAT_EXIF  ? "enabled" : "disabled");
    printf("  curl:   %s\n", SEHN_FEAT_CURL  ? "enabled" : "disabled");
}

static void print_usage() {
    printf(
        "Usage: sehn [options] [device]\n"
        "\n"
        "Options:\n"
        "  -d, --device <path>       camera device (default: /dev/video0)\n"
        "  -o, --output-dir <dir>    save directory\n"
        "  -f, --format <fmt>        capture format: mjpeg, yuyv, nv12, h264\n"
        "  -r, --resolution <WxH>    capture resolution\n"
        "  -R, --framerate <fps>     frames per second\n"
        "  -m, --mode <mode>         photo, burst, video, preview\n"
        "  -F, --fullscreen          start fullscreen\n"
        "  -x, --borderless          no window decorations\n"
        "  -T, --theme <name>        load named theme\n"
        "  -c, --config <file>       config file path\n"
        "      --list-devices        list V4L2 devices and exit\n"
        "      --list-formats        list device formats and exit\n"
        "      --list-controls       list device controls and exit\n"
        "      --print-config        dump resolved config and exit\n"
        "  -q, --quiet               suppress warnings\n"
        "  -V, --verbose             verbose output\n"
        "  -v, --version             print version and exit\n"
        "  -h, --help                print this help and exit\n"
    );
}

int main(int argc, char *argv[]) {

    // 1. start with defaults
    AppState state = make_default_state();

    // 2. load config file (before CLI so CLI can override)
    config_load(&state, nullptr);  // nullptr = use default path

    // 3. parse CLI — overrides config
    cli_parse(argc, argv, &state);

    // 4. early-exit commands
    if (state.mode == Mode::Preview && /* --list-devices flag */ false) {
        return list_devices();
    }

    // 5. open camera
    if (camera_open(&state) < 0) {
        fprintf(stderr, "sehn: failed to open device %s\n",
                state.device.c_str());
        return 1;
    }

    // 6. setup signals (SIGUSR1, SIGUSR2, SIGHUP)
    signals_init(&state);

    // 7. create window and enter main loop
    int ret = ui_run(&state);

    // 8. cleanup
    camera_close(&state);

    return ret;
}
