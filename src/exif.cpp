/* exif.cpp

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

#include "exif.h"

#ifdef HAVE_LIBEXIF

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-loader.h>

static void set_string(ExifData* ed, ExifIfd ifd, ExifTag tag, const char* val)
{
    ExifEntry* e = exif_entry_new();
    exif_content_add_entry(ed->ifd[ifd], e);
    exif_entry_initialize(e, tag);
    e->format = EXIF_FORMAT_ASCII;
    e->size = strlen(val) + 1;
    e->components = e->size;
    e->data = (unsigned char*)strdup(val);
    exif_entry_unref(e);
}

static void set_short(ExifData* ed, ExifIfd ifd, ExifTag tag, ExifByteOrder bo, uint16_t val)
{
    ExifEntry* e = exif_entry_new();
    exif_content_add_entry(ed->ifd[ifd], e);
    exif_entry_initialize(e, tag);
    exif_set_short(e->data, bo, val);
    exif_entry_unref(e);
}

int exif_write(AppState* state, const char* path)
{
    // load existing JPEG
    ExifLoader* loader = exif_loader_new();
    exif_loader_write_file(loader, path);
    ExifData* ed = exif_loader_get_data(loader);
    exif_loader_unref(loader);

    if (!ed)
        ed = exif_data_new();

    ExifByteOrder bo = exif_data_get_byte_order(ed);

    // ── timestamp ────────────────────────────────────────────────────────────
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y:%m:%d %H:%M:%S", tm);
    set_string(ed, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, ts);
    set_string(ed, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, ts);

    // ── image dimensions ─────────────────────────────────────────────────────
    set_short(ed, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION, bo, (uint16_t)state->width);
    set_short(ed, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION, bo, (uint16_t)state->height);

    // ── software tag ─────────────────────────────────────────────────────────
    set_string(ed, EXIF_IFD_0, EXIF_TAG_SOFTWARE, "sehn");

    // ── save back to JPEG ────────────────────────────────────────────────────
    unsigned char* buf = nullptr;
    unsigned int buf_size = 0;
    exif_data_save_data(ed, &buf, &buf_size);
    exif_data_unref(ed);

    if (!buf)
        return -1;

    // read original JPEG
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        free(buf);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long jpeg_size = ftell(fp);
    rewind(fp);
    unsigned char* jpeg = (unsigned char*)malloc(jpeg_size);
    fread(jpeg, 1, jpeg_size, fp);
    fclose(fp);

    // write EXIF + JPEG back
    fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        free(jpeg);
        return -1;
    }

    // JPEG SOI marker
    fputc(0xFF, fp);
    fputc(0xD8, fp);
    // APP1 marker
    fputc(0xFF, fp);
    fputc(0xE1, fp);
    // APP1 length (big endian, includes the 2 length bytes)
    uint16_t app1_len = (uint16_t)(buf_size + 2);
    fputc(app1_len >> 8, fp);
    fputc(app1_len & 0xFF, fp);
    // EXIF data
    fwrite(buf, 1, buf_size, fp);
    // original JPEG minus SOI
    fwrite(jpeg + 2, 1, jpeg_size - 2, fp);

    fclose(fp);
    free(buf);
    free(jpeg);
    return 0;
}

#endif
