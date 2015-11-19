/*
 * Adapted for rk3066 olegk0 <olegvedi@gmail.com>
 *
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86Cursor.h"

#include "disp_hwcursor.h"
#include "fbdev_priv.h"
#include "layer.h"

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#define CURSOR_BUF_SIZE	256
static unsigned char cursor_buf[CURSOR_BUF_SIZE] = {
0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xd5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x55, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x55, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x45, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0xd5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x54, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x50, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x40, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0xd5, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x54, 0xff, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x50, 0xfd, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x40, 0xf5, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x00, 0xd5, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x00, 0x54, 0xff, 0xff, 0xff, 0xff,
0x05, 0x00, 0x00, 0x50, 0xfd, 0xff, 0xff, 0xff,
0x05, 0x00, 0x00, 0x40, 0xf5, 0xff, 0xff, 0xff,
0x05, 0x00, 0x00, 0x00, 0xd5, 0xff, 0xff, 0xff,
0x05, 0x00, 0x40, 0x55, 0x55, 0xff, 0xff, 0xff,
0x05, 0x00, 0x50, 0x55, 0x55, 0xfd, 0xff, 0xff,
0x05, 0x00, 0x50, 0xfd, 0xff, 0xff, 0xff, 0xff,
0x05, 0x14, 0x40, 0xf5, 0xff, 0xff, 0xff, 0xff,
0x05, 0x15, 0x40, 0xf5, 0xff, 0xff, 0xff, 0xff,
0x45, 0x55, 0x00, 0xf5, 0xff, 0xff, 0xff, 0xff,
0x55, 0x5f, 0x00, 0xd5, 0xff, 0xff, 0xff, 0xff,
0xd5, 0x5f, 0x01, 0xd4, 0xff, 0xff, 0xff, 0xff,
0xf5, 0x7f, 0x01, 0x54, 0xff, 0xff, 0xff, 0xff,
0xfd, 0x7f, 0x05, 0x50, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0x05, 0x50, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0x15, 0x55, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0x57, 0xd5, 0xff, 0xff, 0xff, 0xff,
};

static void ShowCursor(ScrnInfoPtr pScrn)
{
    Rk30DispHWCPtr ctx = FBDEVPTR(pScrn)->Rk30HWC;
    int en = 1;
    ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_EN, &en);
}

static void HideCursor(ScrnInfoPtr pScrn)
{
    Rk30DispHWCPtr ctx = FBDEVPTR(pScrn)->Rk30HWC;
    int en = 0;
    ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_EN, &en);
}

static void SetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Rk30DispHWCPtr ctx = pMxv->Rk30HWC;
    OvlHWPtr overlay = pMxv->OvlHW;
    struct fbcurpos pos;

	pos.x = x;
	pos.y = y;
    
    if (pos.x < 0)
        pos.x = 0;
    if (pos.y < 0)
        pos.y = 0;

    ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_POS, &pos);

}

static void SetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
	struct fb_image img;
    Rk30DispHWCPtr ctx = FBDEVPTR(pScrn)->Rk30HWC;

    img.bg_color = bg;
    img.fg_color = fg;
    ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_CMAP, &img);
}

static void LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *bits)
{
    Rk30DispHWCPtr ctx = FBDEVPTR(pScrn)->Rk30HWC;
    int i;

    for(i=0;i<CURSOR_BUF_SIZE;i++)
    	cursor_buf[i] = ~(*(bits+i));

   	ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_IMG, &cursor_buf);
}


/*****************************************************************************
 * Support for hardware cursor, which has 32x32 size, 2 bits per pixel,      *
 * four 32-bit ARGB entries in the palette.                                  *
 *****************************************************************************/

void Rk30DispHardwareCursor_Init(ScreenPtr pScreen)
{
    xf86CursorInfoPtr InfoPtr;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    pMxv->Rk30HWC = NULL;
    int fd;

    fd = fbdevHWGetFD(pScrn);
    if (fd <= 0){
    	ERRMSG("DispHardwareCursor_Init fd error");
    	return;
    }

    if (!(InfoPtr = xf86CreateCursorInfoRec())) {
    	ERRMSG("DispHardwareCursor_Init: xf86CreateCursorInfoRec() failed");
        return;
    }

    InfoPtr->ShowCursor = ShowCursor;
    InfoPtr->HideCursor = HideCursor;
    InfoPtr->SetCursorPosition = SetCursorPosition;
    InfoPtr->SetCursorColors = SetCursorColors;
    InfoPtr->LoadCursorImage = LoadCursorImage;
    InfoPtr->MaxWidth = 32;
    InfoPtr->MaxHeight = 32;
//    InfoPtr->RealizeCursor
    InfoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP | HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1;

    if (!xf86InitCursor(pScreen, InfoPtr)) {
    	ERRMSG("DispHardwareCursor_Init: xf86InitCursor(pScreen, InfoPtr) failed");
        xf86DestroyCursorInfoRec(InfoPtr);
        goto err;
    }

    pMxv->Rk30HWC = calloc(1, sizeof(Rk30DispHWCRec));
    if (!pMxv->Rk30HWC) {
    	ERRMSG("DispHardwareCursor_Init: calloc failed");
        xf86DestroyCursorInfoRec(InfoPtr);
        goto err;
    }
    Rk30DispHWCPtr HWC = pMxv->Rk30HWC;

    INFMSG("HWCursor activated");
    HWC->hwcursor = InfoPtr;
    HWC->fb_fd = fd;
    return;
//******************not ok*************
err:
    xf86DestroyCursorInfoRec(InfoPtr);
}

void Rk30DispHardwareCursor_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    if (pMxv->Rk30HWC) {
    	Rk30DispHWCPtr HWC = pMxv->Rk30HWC;
        xf86DestroyCursorInfoRec(HWC->hwcursor);
        MFREE(HWC);
    }
}
