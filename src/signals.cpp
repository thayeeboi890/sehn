/* signals.cpp

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

#include "signals.h"
#include "camera.h"
#include "capture.h"
#include "config.h"
#include "utils.h"
#include <csignal>
#include <cstdio>

static AppState* g_state = nullptr;

static void handle_sigusr1(int)
{
    if (g_state)
        g_state->sig_capture = true;
}

static void handle_sigusr2(int)
{
    if (g_state)
        g_state->sig_next_mode = true;
}

static void handle_sighup(int)
{
    if (g_state)
        g_state->sig_reload_config = true;
}

void signals_init(AppState *state) {
    g_state = state;

    struct sigaction sa = {};
    sa.sa_flags = SA_RESTART;  // restart interrupted syscalls automatically
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = handle_sigusr1;
    sigaction(SIGUSR1, &sa, nullptr);

    sa.sa_handler = handle_sigusr2;
    sigaction(SIGUSR2, &sa, nullptr);

    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa, nullptr);
}

void signals_dispatch(AppState* state)
{
    if (state->sig_capture) {
        state->sig_capture = false;
        size_t fsz = 0;
        const void* f = camera_next_frame(state, &fsz);
        if (f)
            capture_photo(state, f, fsz);
    }

    if (state->sig_next_mode) {
        state->sig_next_mode = false;
        state->mode = (Mode)(((int)state->mode + 1) % 3);
        printf("sehn: mode -> %d\n", (int)state->mode);
    }

    if (state->sig_reload_config) {
        state->sig_reload_config = false;
        LOG_INFO("reloading config");
        const char* path = state->config_path.empty() ? nullptr : state->config_path.c_str();
        config_load(state, path);
        if (!state->theme.empty())
            config_apply_theme(state, state->theme.c_str());
        camera_apply_controls(state);
    }
}
