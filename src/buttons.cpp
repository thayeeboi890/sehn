/* buttons.cpp

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

#include "buttons.h"
#include "camera.h"
#include "utils.h"
#include <X11/Xlib.h>
#include <algorithm>

void buttons_init(AppState *state) {
    (void)state; // no-op for now
}

// Handle non-PTZ button events here. Returns true if the event was handled.
bool buttons_handle_button_event(AppState *state, XButtonEvent *ev) {
    int btn = ev->button;

    // Right click -> toggle panel/menu
    if (btn == 3) {
        state->panel_visible = !state->panel_visible;
        return true;
    }

    // Middle click -> reset to Fit (keeps zoom middle-click behavior)
    if (btn == 2) {
        state->zoom_mode = ZoomMode::Fit;
        return true;
    }

    // Mouse wheel
    if (btn == 4 || btn == 5) {
        if (ev->state & ControlMask) {
            // Ctrl + wheel -> exposure
            if (btn == 4) state->exposure_time += 1000;
            else state->exposure_time = state->exposure_time > 1000 ? state->exposure_time - 1000 : 0;
            camera_apply_controls(state);
            return true;
        }

        // Zoom behavior
        if (camera_has_zoom()) {
            if (btn == 4) camera_zoom_rel(+2.0f);
            else camera_zoom_rel(-2.0f);
        } else {
            if (btn == 4) {
                state->zoom *= 1.25f;
                if (state->zoom > 10.0f) state->zoom = 10.0f;
            } else {
                state->zoom /= 1.25f;
                if (state->zoom < 0.1f) state->zoom = 0.1f;
            }
            camera_apply_controls(state);
        }
        return true;
    }

    return false;
}
