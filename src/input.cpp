/* input.cpp

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

#include "input.h"
#include "capture.h"
#include "camera.h"
#include "panel.h"
#include <X11/keysym.h>
#include <cstdio>

static void do_action(AppState *state, Action action) {
    switch (action) {
        case Action::Quit:
            state->running = false;
            break;
        case Action::Capture: {
            size_t fsz = 0;
            const void *f = camera_next_frame(state, &fsz);
            if (!f) break;
            if (state->mode == Mode::Burst)
                capture_burst(state);
            else if (state->mode == Mode::Video) {
                if (!state->recording) { capture_video_start(state); state->recording = true; }
                else                   { capture_video_stop(state);  state->recording = false; }
            } else {
                capture_photo(state, f, fsz);
            }
            break;
        }
        case Action::NextMode:
            state->mode = (Mode)(((int)state->mode + 1) % 4);
            break;
        case Action::PrevMode:
            state->mode = (Mode)(((int)state->mode + 3) % 4);
            break;
        case Action::TogglePanel:
            state->panel_visible = !state->panel_visible;
            break;
        case Action::ToggleOverlay:
            state->overlay_visible = !state->overlay_visible;
            break;
        case Action::ToggleFullscreen:
            state->fullscreen = !state->fullscreen;
            break;
        case Action::ZoomIn:
            if (camera_has_zoom()) {
                camera_zoom_rel(+1.0f);
            } else {
                state->zoom_mode = ZoomMode::Percent;
                state->zoom += 0.1f;
                camera_apply_controls(state);
            }
            break;
        case Action::ZoomOut:
            if (camera_has_zoom()) {
                camera_zoom_rel(-1.0f);
            } else {
                state->zoom_mode = ZoomMode::Percent;
                state->zoom = state->zoom > 0.2f ? state->zoom - 0.1f : 0.1f;
                camera_apply_controls(state);
            }
            break;
        case Action::ZoomFit:
            state->zoom_mode = ZoomMode::Fit;
            break;
        case Action::ZoomFill:
            state->zoom_mode = ZoomMode::Fill;
            break;
        case Action::Zoom100:
            if (camera_has_zoom()) {
                // set to mid-range
                camera_zoom_rel(+0.0f);
            } else {
                state->zoom_mode = ZoomMode::Percent;
                state->zoom = 1.0f;
                camera_apply_controls(state);
            }
            break;
        case Action::PanLeft:
            if (camera_has_pan()) camera_pan_rel(-1, 0);
            else { state->pan_x -= 20; camera_apply_controls(state); }
            break;
        case Action::PanRight:
            if (camera_has_pan()) camera_pan_rel(+1, 0);
            else { state->pan_x += 20; camera_apply_controls(state); }
            break;
        case Action::PanUp:
            if (camera_has_tilt()) camera_pan_rel(0, -1);
            else { state->pan_y -= 20; camera_apply_controls(state); }
            break;
        case Action::PanDown:
            if (camera_has_tilt()) camera_pan_rel(0, +1);
            else { state->pan_y += 20; camera_apply_controls(state); }
            break;
        case Action::FlipHorizontal:
            // TODO: flip flag on state
            break;
        case Action::FlipVertical:
            // TODO: flip flag on state
            break;
        case Action::RotateCW:
        case Action::RotateCCW:
            // TODO: rotation flag on state
            break;
        case Action::ToggleExposure:
            state->exposure_mode = (state->exposure_mode == "auto") ? "manual" : "auto";
            break;
        case Action::ExposureUp:
            state->exposure_time += 1000;
            break;
        case Action::ExposureDown:
            state->exposure_time = state->exposure_time > 1000
                                   ? state->exposure_time - 1000 : 0;
            break;
        case Action::GainUp:   state->gain += 10; break;
        case Action::GainDown: state->gain = state->gain > 10 ? state->gain - 10 : 0; break;
        case Action::ToggleWB:
            state->wb_mode = (state->wb_mode == "auto") ? "manual" : "auto";
            break;
        case Action::WBWarmer: state->wb_temp += 100; break;
        case Action::WBCooler: state->wb_temp -= 100; break;
        case Action::ToggleAutofocus:
            state->autofocus = !state->autofocus;
            break;
        case Action::ToggleInfo:
            state->overlay_visible = !state->overlay_visible;
            break;
        default:
            break;
    }
}

void input_handle_key(AppState *state, const KeyMap &km, XKeyEvent *ev) {
    // try unshifted first, then shifted — this handles CapsLock/Shift variations
    KeySym sym = XLookupKeysym(ev, 0);
    Action action = keys_lookup(km, sym);
    if (action == Action::Unknown) {
        sym = XLookupKeysym(ev, 1);
        action = keys_lookup(km, sym);
    }
    do_action(state, action);
}

void input_handle_button(AppState *state, XButtonEvent *ev) {
    int hit = panel_hittest(state, ev->x, ev->y);
    switch (hit) {
        case PANEL_SHUTTER: do_action(state, Action::Capture); break;
        case PANEL_MODE:    do_action(state, Action::NextMode); break;
        case PANEL_MENU:    /* TODO: open menu */ break;
        default: break;
    }
}
