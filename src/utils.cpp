/* utils.cpp

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

#include "utils.h"
#include <cstdio>
#include <cstdarg>

static bool g_verbose = false;
static bool g_quiet   = false;

void log_init(AppState *state) {
    g_verbose = state->verbose;
    g_quiet   = state->quiet;
}

void sehn_log(LogLevel level, const char *fmt, ...) {
    // filter by level
    if (level == LogLevel::Info  && g_quiet)   return;
    if (level == LogLevel::Warn  && g_quiet)   return;
    if (level == LogLevel::Debug && !g_verbose) return;

    FILE *out = (level == LogLevel::Error ||
                 level == LogLevel::Warn) ? stderr : stdout;

    // prefix
    switch (level) {
        case LogLevel::Warn:  fprintf(out, "sehn: warning: "); break;
        case LogLevel::Error: fprintf(out, "sehn: error: ");   break;
        case LogLevel::Debug: fprintf(out, "sehn: debug: ");   break;
        default: fprintf(out, "sehn: ");                       break;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}
