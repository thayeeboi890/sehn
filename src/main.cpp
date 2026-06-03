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
#include "signals.h"
#include "list.h"

int main(int argc, char *argv[]) {

    // 1. start with defaults
    AppState state = make_default_state();

    // 2. load config file (before CLI so CLI can override)
    config_load(&state, nullptr);

    // 3. parse CLI — overrides config
    cli_parse(argc, argv, &state);

    // 4. apply theme if one was requested
    if (!state.theme.empty())
        config_apply_theme(&state, state.theme.c_str());

    // 5. early-exit commands
    if (state.list_devices_and_exit)
        return list_devices();
    if (state.list_formats_and_exit)
        return list_formats(state.device.c_str());
    if (state.list_controls_and_exit)
        return list_controls(state.device.c_str());
    if (state.print_config_and_exit) {
        config_print(&state);
        return 0;
    }

    // 6. setup signals (SIGUSR1, SIGUSR2, SIGHUP)
    signals_init(&state);

    // 7. TODO: camera_open()  — Round 2
    // 8. TODO: ui_run()       — Round 3

    printf("sehn: stubs ok\n");
    return 0;
}
