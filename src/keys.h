/* keys.h

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
#include <X11/Xlib.h>
#include <string>
#include <unordered_map>

// action ids — every bindable action in sehn
enum class Action {
    Unknown,
    Quit,
    Capture,
    NextMode,
    PrevMode,
    TogglePanel,
    ToggleOverlay,
    ToggleFullscreen,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    ZoomFill,
    Zoom100,
    PanLeft,
    PanRight,
    PanUp,
    PanDown,
    FlipHorizontal,
    FlipVertical,
    RotateCW,
    RotateCCW,
    ToggleExposure,
    ExposureUp,
    ExposureDown,
    GainUp,
    GainDown,
    ToggleWB,
    WBWarmer,
    WBCooler,
    ToggleAutofocus,
};

// keybinding table: X11 KeySym -> Action
using KeyMap = std::unordered_map<KeySym, Action>;

// Load keybindings from ~/.config/sehn/keys.toml.
// Falls back to defaults if file not found.
KeyMap keys_load(AppState* state);

// Return the default keybindings.
KeyMap keys_defaults();

// Look up which action a KeySym maps to.
Action keys_lookup(const KeyMap& km, KeySym sym);
