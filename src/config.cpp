/* config.cpp

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

#include "config.h"
#include <cstdio>

int config_load(AppState *state, const char *path) {
    (void)state;
    (void)path;
    // TODO: find config file, parse TOML, populate state
    return -1; // file not found is non-fatal, so -1 is fine for now
}

int config_apply_theme(AppState *state, const char *theme_name) {
    (void)state;
    (void)theme_name;
    // TODO: open themes.toml, find table [theme_name], merge into state
    return 0;
}

void config_print(const AppState *state) {
    (void)state;
    // TODO: walk state fields and print as TOML
    printf("# sehn config (not yet implemented)\n");
}
