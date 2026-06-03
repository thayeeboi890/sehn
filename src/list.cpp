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
#include <cstdio>

int list_devices() {
    // TODO: scan /sys/class/video4linux/, print each device and its name
    printf("(list_devices not yet implemented)\n");
    return 1;
}

int list_formats(const char *device) {
    (void)device;
    // TODO: open device, VIDIOC_ENUM_FMT, VIDIOC_ENUM_FRAMESIZES,
    //       VIDIOC_ENUM_FRAMEINTERVALS, print results
    printf("(list_formats not yet implemented)\n");
    return 1;
}

int list_controls(const char *device) {
    (void)device;
    // TODO: open device, walk VIDIOC_QUERYCTRL, print name/min/max/current
    printf("(list_controls not yet implemented)\n");
    return 1;
}
