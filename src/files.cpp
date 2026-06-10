/* files.cpp

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

#include "files.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

std::string files_expand_path(const std::string& path)
{
    if (path.empty())
        return path;

    // expand ~/
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home)
            return path;
        return std::string(home) + path.substr(1);
    }

    // expand $HOME
    if (path.substr(0, 5) == "$HOME") {
        const char* home = getenv("HOME");
        if (!home)
            return path;
        return std::string(home) + path.substr(5);
    }

    return path;
}

int files_mkdir_p(const std::string& path)
{
    if (path.empty())
        return -1;

    std::string tmp = path;
    // strip trailing slash
    if (tmp.back() == '/')
        tmp.pop_back();

    for (size_t i = 1; i < tmp.size(); i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp.c_str(), 0755) < 0 && errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
            tmp[i] = '/';
        }
    }

    if (mkdir(tmp.c_str(), 0755) < 0 && errno != EEXIST) {
        perror("mkdir");
        return -1;
    }

    return 0;
}

int files_init_output_dir(AppState* state)
{
    state->output_dir = files_expand_path(state->output_dir);
    return files_mkdir_p(state->output_dir);
}
