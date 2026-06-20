/* menu.h

Copyright (C) 2026 Santiago Silva.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software and its documentation and
acknowledgment shall be given in the documentation and software packages that
this Software was used.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
#pragma once

#include "sehn.h"
#include <X11/Xlib.h>

// Call once after the window is mapped.
void menu_init(Display *dpy, Window win, const char *font_path);
void menu_cleanup(Display *dpy);

// Show the right-click context menu at (x, y) in window coordinates.
// Runs its own nested event loop until the user dismisses it.
void menu_show(AppState *state, Display *dpy, Window win, GC gc, int x, int y);

// Returns true if the menu is currently visible.
bool menu_is_open();
