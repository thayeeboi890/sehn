/* ui.h

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

// Create the X11 window, start the frame loop, handle input.
int ui_run(AppState* state);

// Tear down the X11 window and free resources.
void ui_cleanup(AppState* state);

// Present the last-converted RGB frame using the provided source rectangle.
// This is used by input handlers to render software-only pan/zoom while dragging
// without committing hardware PTZ until release.
void ui_present_last_rgb_region(AppState* state, int src_x, int src_y, uint32_t src_w,
                                uint32_t src_h);

// Present the last-converted RGB frame using the current zoom, pan, flip, and rotation state.
void ui_present_current_frame(AppState* state);

// Toggle the right click menu to display over mouse cursor.
void ui_show_menu(AppState *state, int x, int y);

// Flash the window to indicate capture.
void ui_flash(AppState *state);
