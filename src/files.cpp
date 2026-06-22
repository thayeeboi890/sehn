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
#include "resources.h"
#include "utils.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>

// platform-specific includes for files_find_font
#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#else
    #include <unistd.h> // for Linux/BSD readlink
#endif


namespace fs = std::filesystem;

fs::path get_executable_path() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    return fs::path(buf);
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return fs::canonical(buf);
    return "";
#else
    char buf[1024] = {};
    if (readlink("/proc/self/exe", buf, sizeof(buf) - 1) > 0) {
        return fs::canonical(buf);
    }
    return "";
#endif
}


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

static std::string default_config_dir()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg)
        base = xdg;
    else {
        const char* home = getenv("HOME");
        if (!home)
            return "";
        base = std::string(home) + "/.config";
    }
    return base + "/sehn";
}

static void write_default_file(const std::string& path, const char* content)
{
    std::ofstream out(path);
    if (out.is_open()) {
        out << content;
        LOG_INFO("created default %s", path.c_str());
    } else {
        LOG_WARN("failed to create %s", path.c_str());
    }
}

int files_init_config()
{
    std::string dir = default_config_dir();
    if (dir.empty())
        return -1;

    // create config directory
    if (files_mkdir_p(dir) < 0) {
        LOG_WARN("could not create config dir %s", dir.c_str());
        return -1;
    }

    // write default files if missing
    auto write_if_missing = [&](const std::string& name, const char* content) {
        std::string path = dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            write_default_file(path, content);
    };

    write_if_missing("sehnrc.toml", resources::sehnrc);
    write_if_missing("themes.toml", resources::themes);
    write_if_missing("keys.toml",   resources::keys);

    return 0;
}

int files_init_output_dir(AppState* state)
{
    state->output_dir = files_expand_path(state->output_dir);
    return files_mkdir_p(state->output_dir);
}

std::string files_find_font() {
    fs::path exe_dir = get_executable_path().parent_path();

    if (!exe_dir.empty()) {
        fs::path candidate1 = exe_dir / "share" / "fonts" / "sehn" / "yudit.ttf";
        if (fs::exists(candidate1)) return candidate1.string();

        fs::path candidate2 = exe_dir / ".." / ".." / "share" / "fonts" / "yudit.ttf";
        if (fs::exists(candidate2)) return candidate2.lexically_normal().string();
    }

    fs::path sys_path = fs::path(SEHN_DATADIR) / "fonts" / "sehn" / "yudit.ttf";
    if (fs::exists(sys_path)) return sys_path.string();

    return "";
}
