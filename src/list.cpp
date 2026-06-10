/* list.cpp

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

#include "list.h"
#include "utils.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long request, void* arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int list_devices()
{
    LOG_FN();
    DIR* d = opendir("/sys/class/video4linux");
    if (!d) {
        LOG_ERROR("cannot open /sys/class/video4linux");
        return 1;
    }

    int found = 0;
    struct dirent* entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.')
            continue;

        char devpath[64];
        snprintf(devpath, sizeof(devpath), "/dev/%s", entry->d_name);

        int fd = open(devpath, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;

        struct v4l2_capability cap = {};
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                printf("%s\n", devpath);
                printf("  card:   %s\n", cap.card);
                printf("  driver: %s\n", cap.driver);
                printf("  bus:    %s\n", cap.bus_info);
                found++;
            }
        }
        close(fd);
    }

    closedir(d);

    if (!found) {
        printf("sehn: no capture devices found\n");
        return 1;
    }
    return 0;
}

int list_formats(const char* device)
{
    LOG_FN();
    int fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERROR("cannot open %s", device);
        return 1;
    }

    struct v4l2_capability cap = {};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }

    printf("%s — %s\n\n", device, cap.card);

    struct v4l2_fmtdesc fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        printf("  [%s] %s\n", (char*)&fmt.pixelformat, fmt.description);

        // enumerate frame sizes for this format
        struct v4l2_frmsizeenum fsize = {};
        fsize.pixel_format = fmt.pixelformat;

        while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
            if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("    %ux%u", fsize.discrete.width, fsize.discrete.height);

                // enumerate framerates for this size
                struct v4l2_frmivalenum fival = {};
                fival.pixel_format = fmt.pixelformat;
                fival.width = fsize.discrete.width;
                fival.height = fsize.discrete.height;

                printf(" @");
                while (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival) == 0) {
                    if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                        printf(" %ufps", fival.discrete.denominator / fival.discrete.numerator);
                    }
                    fival.index++;
                }
                printf("\n");
            }
            fsize.index++;
        }

        fmt.index++;
    }

    close(fd);
    return 0;
}

int list_controls(const char* device)
{
    LOG_FN();
    int fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERROR("cannot open %s", device);
        return 1;
    }

    struct v4l2_capability cap = {};
    xioctl(fd, VIDIOC_QUERYCAP, &cap);
    printf("%s — %s\n\n", device, cap.card);

    struct v4l2_query_ext_ctrl qc = {};
    qc.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

    while (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qc) == 0) {
        if (qc.flags & V4L2_CTRL_FLAG_DISABLED) {
            qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
            continue;
        }

        // get current value for integer/boolean controls
        if (qc.type == V4L2_CTRL_TYPE_INTEGER || qc.type == V4L2_CTRL_TYPE_BOOLEAN ||
            qc.type == V4L2_CTRL_TYPE_MENU) {

            struct v4l2_control ctrl = {};
            ctrl.id = qc.id;
            xioctl(fd, VIDIOC_G_CTRL, &ctrl);

            printf("  %-32s  val=%-6d  min=%-6lld  max=%-6lld  step=%lld\n", qc.name, ctrl.value,
                   qc.minimum, qc.maximum, qc.step);

            // print menu items if it's a menu control
            if (qc.type == V4L2_CTRL_TYPE_MENU) {
                struct v4l2_querymenu qm = {};
                qm.id = qc.id;
                for (qm.index = (uint32_t)qc.minimum; qm.index <= (uint32_t)qc.maximum;
                     qm.index++) {
                    if (xioctl(fd, VIDIOC_QUERYMENU, &qm) == 0)
                        printf("    %u: %s\n", qm.index, qm.name);
                }
            }
        }
        else {
            printf("  %-32s  (type=%u)\n", qc.name, qc.type);
        }

        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    }

    close(fd);
    return 0;
}
