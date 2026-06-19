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
#include "camera.h"
#include "capture.h"
#include "panel.h"
#include "ui.h"
#include "utils.h"
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cmath>
#include <cstdio>
#include <time.h>
#include <unistd.h>

static void do_action(AppState* state, Action action)
{
    switch (action) {
    case Action::Quit:
        state->running = false;
        break;
    case Action::Capture: {
        if (state->mode == Mode::Burst)
            capture_burst(state);
        else if (state->mode == Mode::Video) {
            if (!state->recording) {
                if (capture_video_start(state) == 0)
                    state->recording = true;
            }
            else {
                state->recording = false;

                // allow capture thread to finish current frame
                usleep(50000);

                capture_video_stop(state);
            }
        }
        else {
            size_t fsz = 0;
            const void* f = camera_next_frame(state, &fsz);
            if (!f)
                break;
            capture_photo(state, f, fsz);
        }
        break;
    }
    case Action::NextMode:
        state->mode = (Mode)(((int)state->mode + 1) % 3);
        break;
    case Action::PrevMode:
        state->mode = (Mode)(((int)state->mode + 2) % 3);
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
        }
        else {
            state->zoom_mode = ZoomMode::Percent;
            state->zoom += 0.1f;
            camera_apply_controls(state);
        }
        break;
    case Action::ZoomOut:
        if (camera_has_zoom()) {
            camera_zoom_rel(-1.0f);
        }
        else {
            state->zoom_mode = ZoomMode::Percent;
            state->zoom = state->zoom > 0.2f ? state->zoom - 0.1f : 0.1f;
            camera_apply_controls(state);
        }
        break;
    case Action::ZoomFit:
        if (camera_has_zoom())
            camera_zoom_reset();
        state->zoom_mode = ZoomMode::Fit;
        state->pan_x = 0;
        state->pan_y = 0;
        break;
    case Action::ZoomFill:
        state->zoom_mode = ZoomMode::Fill;
        break;
    case Action::Zoom100:
        if (camera_has_zoom()) {
            // set to mid-range
            camera_zoom_rel(+0.0f);
        }
        else {
            state->zoom_mode = ZoomMode::Percent;
            state->zoom = 1.0f;
            camera_apply_controls(state);
        }
        break;
    case Action::PanLeft:
        if (camera_has_pan())
            camera_pan_rel(-1, 0);
        else {
            state->pan_x -= 20;
            camera_apply_controls(state);
        }
        break;
    case Action::PanRight:
        if (camera_has_pan())
            camera_pan_rel(+1, 0);
        else {
            state->pan_x += 20;
            camera_apply_controls(state);
        }
        break;
    case Action::PanUp:
        if (camera_has_tilt())
            camera_pan_rel(0, -1);
        else {
            state->pan_y -= 20;
            camera_apply_controls(state);
        }
        break;
    case Action::PanDown:
        if (camera_has_tilt())
            camera_pan_rel(0, +1);
        else {
            state->pan_y += 20;
            camera_apply_controls(state);
        }
        break;
    case Action::FlipHorizontal:
        state->flip_horizontal = !state->flip_horizontal;
        break;
    case Action::FlipVertical:
        state->flip_vertical = !state->flip_vertical;
        break;
    case Action::RotateCW:
        state->rotate_deg = (state->rotate_deg + 90) % 360;
        break;
    case Action::RotateCCW:
        state->rotate_deg = (state->rotate_deg + 270) % 360;
        break;
    case Action::ToggleExposure:
        state->exposure_mode = (state->exposure_mode == "auto") ? "manual" : "auto";
        camera_apply_controls(state);
        break;
    case Action::ExposureUp:
        state->exposure_time += 1000;
        camera_apply_controls(state);
        break;
    case Action::ExposureDown:
        state->exposure_time = state->exposure_time > 1000 ? state->exposure_time - 1000 : 0;
        camera_apply_controls(state);
        break;
    case Action::GainUp:
        state->gain += 10;
        camera_apply_controls(state);
        break;
    case Action::GainDown:
        state->gain = state->gain > 10 ? state->gain - 10 : 0;
        camera_apply_controls(state);
        break;
    case Action::ToggleWB:
        state->wb_mode = (state->wb_mode == "auto") ? "manual" : "auto";
        camera_apply_controls(state);
        break;
    case Action::WBWarmer:
        state->wb_temp += 100;
        camera_apply_controls(state);
        break;
    case Action::WBCooler:
        state->wb_temp -= 100;
        camera_apply_controls(state);
        break;
    case Action::ToggleAutofocus:
        state->autofocus = !state->autofocus;
        camera_apply_controls(state);
        break;
    default:
        break;
    }
}

static Action lookup_key_action(const KeyMap& km, XKeyEvent* ev)
{
    KeySym typed = NoSymbol;
    char buf[32];
    XLookupString(ev, buf, sizeof(buf), &typed, nullptr);
    Action action = keys_lookup(km, typed);
    if (action != Action::Unknown)
        return action;

    KeySym syms[2];
    int sym_count = 0;
    if (ev->state & ShiftMask)
        syms[sym_count++] = XLookupKeysym(ev, 1);
    else
        syms[sym_count++] = XLookupKeysym(ev, 0);

    for (int i = 0; i < sym_count; i++) {
        action = keys_lookup(km, syms[i]);
        if (action != Action::Unknown)
            return action;
    }

    return Action::Unknown;
}

void input_handle_key(AppState* state, const KeyMap& km, XKeyEvent* ev)
{
    Action action = lookup_key_action(km, ev);
    do_action(state, action);
    ui_present_current_frame(state);
}

void input_handle_button(AppState* state, XButtonEvent* ev)
{
    // button press
    int btn = ev->button;
    int hit = panel_hittest(state, ev->x, ev->y);
    if (hit >= 0) {
        switch (hit) {
        case PANEL_SHUTTER:
            do_action(state, Action::Capture);
            ui_present_current_frame(state);
            return;
        case PANEL_MODE:
            do_action(state, Action::NextMode);
            ui_present_current_frame(state);
            return;
        case PANEL_PHOTOS:
            system("xdg-open ~/Pictures/sehn &"); //TODO: replace with proper file reference
            return;
        default:
            return;
        }
    }

    // Do not start any pan/tilt drag on left-click: PTZ via mouse is disabled.
    // Only handle non-PTZ buttons here: right-click (toggle panel), middle-click (zoom fit), wheel
    // and ctrl+wheel.
    if (btn == 3) {
        // toggle menu/panel
        state->panel_visible = !state->panel_visible;
        ui_present_current_frame(state);
        return;
    }

    if (btn == 2) {
        // middle click -> reset to fit
        if (camera_has_zoom())
            camera_zoom_reset();
        state->zoom_mode = ZoomMode::Fit;
        state->pan_x = 0;
        state->pan_y = 0;
        ui_present_current_frame(state);
        return;
    }

    if (btn == 4 || btn == 5) {
        // wheel up/down: immediate action on press
        if (ev->state & ControlMask) {
            // Ctrl+wheel adjusts exposure in manual mode
            if (btn == 4)
                do_action(state, Action::ExposureUp);
            else
                do_action(state, Action::ExposureDown);
        }
        else {
            // Zoom: be more aggressive per tick
            if (camera_has_zoom()) {
                if (btn == 4)
                    camera_zoom_rel(+2.0f);
                else
                    camera_zoom_rel(-2.0f);
            }
            else {
                if (btn == 4) {
                    state->zoom *= 1.25f;
                    if (state->zoom > 10.0f)
                        state->zoom = 10.0f;
                }
                else {
                    state->zoom /= 1.25f;
                    if (state->zoom < 0.1f)
                        state->zoom = 0.1f;
                }
                camera_apply_controls(state);
            }
        }
        ui_present_current_frame(state);
        return;
    }

    // other buttons (including left click) are ignored for pan/tilt purposes
}

void input_handle_button_release(AppState* state, XButtonEvent* ev)
{
    // PTZ via mouse is disabled. Clear any drag state and do not commit hardware pan/tilt.
    (void)ev;
    state->mouse_dragging = false;
    state->mouse_button = 0;
}

void input_handle_motion(AppState* state, XMotionEvent* ev)
{
    if (!state->mouse_dragging)
        return;
    int btn = state->mouse_button;
    int dx = ev->x - state->mouse_down_x;
    int dy = ev->y - state->mouse_down_y;

    // compute current source region size for manual zoom
    uint32_t src_w = (uint32_t)((float)state->width / state->zoom);
    uint32_t src_h = (uint32_t)((float)state->height / state->zoom);
    if (src_w > state->width)
        src_w = state->width;
    if (src_h > state->height)
        src_h = state->height;

    // compute destination image size in window (preserve aspect)
    float src_aspect = (float)src_w / (float)src_h;
    float win_aspect = (float)state->win_w / (float)state->win_h;
    int dst_img_w, dst_img_h;
    if (win_aspect > src_aspect) {
        dst_img_h = state->win_h;
        dst_img_w = (int)(dst_img_h * src_aspect);
    }
    else {
        dst_img_w = state->win_w;
        dst_img_h = (int)(dst_img_w / src_aspect);
    }

    if (btn == 1) {
        // pan: map pixel motion to source pixels (drag direction matches pan direction)
        float sx = (float)src_w / (float)dst_img_w;
        int max_x = (int)state->width - (int)src_w;
        int max_y = (int)state->height - (int)src_h;

        if (camera_has_pan() || camera_has_tilt()) {
            // compute new_pan pixel coords for smooth local panning
            int new_pan_x = state->mouse_start_pan_x - (int)roundf(dx * sx);
            int new_pan_y = state->mouse_start_pan_y + (int)roundf(dy * sx);
            if (new_pan_x < 0)
                new_pan_x = 0;
            if (new_pan_x > max_x)
                new_pan_x = max_x;
            if (new_pan_y < 0)
                new_pan_y = 0;
            if (new_pan_y > max_y)
                new_pan_y = max_y;
            // update visual pan immediately for smoothness
            state->pan_x = new_pan_x;
            state->pan_y = new_pan_y;
            LOG_DEBUG("input: software-smooth pan to px=%d py=%d (dx=%d dy=%d)", new_pan_x,
                      new_pan_y, dx, dy);
            // redraw using last converted RGB so movement is immediate
            ui_present_last_rgb_region(state, state->pan_x, state->pan_y, src_w, src_h);
        }
        else {
            int new_pan_x = state->mouse_start_pan_x - (int)roundf(dx * sx);
            int new_pan_y = state->mouse_start_pan_y + (int)roundf(dy * sx);
            if (new_pan_x < 0)
                new_pan_x = 0;
            if (new_pan_x > max_x)
                new_pan_x = max_x;
            if (new_pan_y < 0)
                new_pan_y = 0;
            if (new_pan_y > max_y)
                new_pan_y = max_y;
            state->pan_x = new_pan_x;
            state->pan_y = new_pan_y;
            ui_present_last_rgb_region(state, state->pan_x, state->pan_y, src_w, src_h);
        }
    }
    else if (btn == 2) {
        // zoom drag: vertical movement -> zoom factor (drag up = zoom in)
        float sensitivity = 200.0f;                      // lower = more sensitive
        float delta = 1.0f + (float)(-dy) / sensitivity; // negative dy (up) -> >1
        if (delta < 0.05f)
            delta = 0.05f;
        float z = state->mouse_start_zoom * delta;
        if (z < 0.1f)
            z = 0.1f;
        if (z > 10.0f)
            z = 10.0f;
        if (camera_has_zoom()) {
            // convert vertical movement into hardware zoom steps
            if (dy < -5)
                camera_zoom_rel(+1.0f);
            else if (dy > 5)
                camera_zoom_rel(-1.0f);
        }
        else {
            state->zoom = z;
            camera_apply_controls(state);
        }
    }
}
