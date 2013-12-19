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

    switch(overlay->cur_var.yres){
    case 720:
//	pos.x = (x << 4)/9;
	pos.x = (x*3)/2;
	pos.y = (y*3)/2;
	break;
    default:
	pos.x = x;
	pos.y = y;
    }

    
    if (pos.x < 0)
        pos.x = 0;
    if (pos.y < 0)
        pos.y = 0;

    if (ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_POS, &pos) >= 0) {
//        ctx->cursor_x = pos.x;
//        ctx->cursor_y = pos.y;
    }
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
    ioctl(ctx->fb_fd, FBIOPUT_SET_CURSOR_IMG, bits);
}

/*****************************************************************************
 * Support for hardware cursor, which has 32x32 size, 2 bits per pixel,      *
 * four 32-bit ARGB entries in the palette.                                  *
 *****************************************************************************/

void Rk30DispHardwareCursor_Init(ScreenPtr pScreen, const char *device)
{
    xf86CursorInfoPtr InfoPtr;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    pMxv->Rk30HWC = NULL;

    if(NULL == pMxv->OvlHW){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "DispHardwareCursor_Init: Not found overlay\n");
	return;
    }

    OvlHWPtr	overlay = pMxv->OvlHW;

    if (!(InfoPtr = xf86CreateCursorInfoRec())) {
    	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "DispHardwareCursor_Init: xf86CreateCursorInfoRec() failed\n");
        return;
    }

    InfoPtr->ShowCursor = ShowCursor;
    InfoPtr->HideCursor = HideCursor;
    InfoPtr->SetCursorPosition = SetCursorPosition;
    InfoPtr->SetCursorColors = SetCursorColors;
    InfoPtr->LoadCursorImage = LoadCursorImage;
    InfoPtr->MaxWidth = InfoPtr->MaxHeight = 32;
    InfoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
                     HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1 |
                     HARDWARE_CURSOR_ARGB;

    if (!xf86InitCursor(pScreen, InfoPtr)) {
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "DispHardwareCursor_Init: xf86InitCursor(pScreen, InfoPtr) failed\n");
        xf86DestroyCursorInfoRec(InfoPtr);
        goto err;
    }

    pMxv->Rk30HWC = calloc(1, sizeof(Rk30DispHWCRec));
    if (!pMxv->Rk30HWC) {
    	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "DispHardwareCursor_Init: calloc failed\n");
        xf86DestroyCursorInfoRec(InfoPtr);
        goto err;
    }
    Rk30DispHWCPtr HWC = pMxv->Rk30HWC;

    HWC->fb_fd = open(device, O_RDWR);
    if (HWC->fb_fd < 0) {
    	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "DispHardwareCursor_Init: open: %s failed\n",device);
//	close(HWC->fb_fd);
        free(HWC);
        goto err;
    }
//    private->cursor_enabled = 0;
//    private->cursor_x = -1;
//    private->cursor_y = -1;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Enabled hardware cursor\n");
    HWC->hwcursor = InfoPtr;
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
	close(HWC->fb_fd);
        free(HWC);
	HWC = NULL;
    }
}
