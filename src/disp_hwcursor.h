/*
 * Adapted for rk3066 olegk0 <olegvedi@gmail.com>
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DISP_HWCURSOR_H
#define __DISP_HWCURSOR_H

#include "xf86Cursor.h"
#include <inttypes.h>

#define FBIOPUT_SET_CURSOR_EN    0x4609
#define FBIOPUT_SET_CURSOR_IMG    0x460a
#define FBIOPUT_SET_CURSOR_POS    0x460b
#define FBIOPUT_SET_CURSOR_CMAP    0x460c


typedef struct {
    xf86CursorInfoPtr hwcursor;
    int fb_fd;
//    int cursor_enabled;
//    int cursor_x, cursor_y;

} RkDispHWCRec, *RkDispHWCPtr;

void RkDispHardwareCursor_Init(ScreenPtr pScreen);
void RkDispHardwareCursor_Close(ScreenPtr pScreen);

#endif
