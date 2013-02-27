/*
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

#ifndef __LAYER_H_
#define __LAYER_H_

#include "include/ipp.h"
#include "include/rga.h"

void InitHWAcl(ScreenPtr pScreen);
void CloseHWAcl(ScreenPtr pScreen);

int IppBlit(ScrnInfoPtr pScrn, struct rk29_ipp_req *ipp_req);

int RgaBlit(ScrnInfoPtr pScrn, struct rga_req *RGA_req, int syncmode);

int OvlSync(ScrnInfoPtr pScrn);
int OvlSwDisp(ScrnInfoPtr pScrn, int disp, Bool clear);
void OvlFillBuf(CARD32 *buf, unsigned int len, CARD32 color);
void OvlClearBuf(ScrnInfoPtr pScrn, unsigned char pg);
int OvlSetMode(ScrnInfoPtr pScrn, unsigned short xres, unsigned short yres, unsigned char mode);
int OvlReset(ScrnInfoPtr pScrn);
int OvlEnable(ScrnInfoPtr pScrn, int enable);
int OvlFlushPg(ScrnInfoPtr pScrn, unsigned char pg, int mode);

#endif
