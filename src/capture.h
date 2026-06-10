/* capture.h

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
#include <cstddef>

// Save the current frame as a still image (JPEG or PNG based on state).
// Returns 0 on success, -1 on failure.
int capture_photo(AppState* state, const void* frame, size_t frame_size);

// Start a video recording session.
// Returns 0 on success, -1 on failure.
int capture_video_start(AppState* state);

// Write a frame to the current video recording.
void capture_video_frame(AppState* state, const void* frame, size_t frame_size);

// Stop and finalize the current video recording.
void capture_video_stop(AppState* state);

// Capture burst_count frames at burst_interval_ms apart and save each one.
// Blocks until all frames are captured.
void capture_burst(AppState* state);

// Build the output filename from state->filename_pattern and state->output_dir.
// Writes into buf up to buf_len bytes.
void capture_build_filename(AppState* state, char* buf, size_t buf_len, const char* ext);
