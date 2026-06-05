/* utils.h

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

#include "sehn.h"

// log levels
enum class LogLevel {
    Info,
    Warn,
    Error,
    Debug,
};

// Initialize the logger with the app state so it knows verbose/quiet.
void log_init(AppState *state);

// Log a message at the given level.
// Info:  printed normally, suppressed by --quiet
// Warn:  printed to stderr, suppressed by --quiet
// Error: always printed to stderr
// Debug: only printed with --verbose
void sehn_log(LogLevel level, const char *fmt, ...);

// Convenience macros
#define LOG_INFO(...)  sehn_log(LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  sehn_log(LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) sehn_log(LogLevel::Error, __VA_ARGS__)
#define LOG_DEBUG(...) sehn_log(LogLevel::Debug, __VA_ARGS__)

// Helper to log function entry/exit for debug
#if defined(__GNUC__)
#define LOG_FN() LOG_DEBUG("%s:%d %s", __FILE__, __LINE__, __func__)
#else
#define LOG_FN() LOG_DEBUG("%s %s", __FILE__, __func__)
#endif
