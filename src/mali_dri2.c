/*
 * Copyright (C) 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Initially derived from "mali_dri2.c" which is a part of xf86-video-mali,
 * even though there is now hardly any original line of code remaining.
 *
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#include <sys/ioctl.h>

#include "xorgVersion.h"
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "layer.h"
#include "disp_hwcursor.h"
#include "mali_dri2.h"

#include <unistd.h>
#include <fcntl.h>

#undef DEBUG
#ifdef DEBUG
#define DebugMsg(...) ErrorF(__VA_ARGS__)
#else
#define DebugMsg(...)
#endif

#define HAVE_LIBUMP_CACHE_CONTROL 1
/*
 * The code below is borrowed from "xserver/dix/window.c"
 */

/* Migrate pixmap to UMP buffer */
static UMPBufferInfoPtr
MigratePixmapToUMP(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *self = RK30_MALI_DRI2(pScrn);
    UMPBufferInfoPtr umpbuf;
    size_t pitch = ((pPixmap->devKind + 7) / 8) * 8;
    size_t size = pitch * pPixmap->drawable.height;

    HASH_FIND_PTR(self->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("MigratePixmapToUMP %p, already exists = %p\n", pPixmap, umpbuf);
        return umpbuf;
    }

    /* create the UMP buffer */
    umpbuf = calloc(1, sizeof(UMPBufferInfoRec));
    if (!umpbuf) {
        ErrorF("MigratePixmapToUMP: calloc failed\n");
        return NULL;
    }
    umpbuf->refcount = 1;
    umpbuf->pPixmap = pPixmap;
    umpbuf->handle = ump_ref_drv_allocate(size, UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR);
    if (umpbuf->handle == UMP_INVALID_MEMORY_HANDLE) {
        ErrorF("MigratePixmapToUMP: ump_ref_drv_allocate failed\n");
        free(umpbuf);
        return NULL;
    }
    umpbuf->size = size;
    umpbuf->addr = ump_mapped_pointer_get(umpbuf->handle);
    umpbuf->depth = pPixmap->drawable.depth;
    umpbuf->width = pPixmap->drawable.width;
    umpbuf->height = pPixmap->drawable.height;

    /* copy the pixel data to the new location */
    if (pitch == pPixmap->devKind) {
        memcpy(umpbuf->addr, pPixmap->devPrivate.ptr, size);
    } else {
        int y;
        for (y = 0; y < umpbuf->height; y++) {
            memcpy(umpbuf->addr + y * pitch, 
                   pPixmap->devPrivate.ptr + y * pPixmap->devKind,
                   pPixmap->devKind);
        }
    }

    umpbuf->BackupDevKind = pPixmap->devKind;
    umpbuf->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

    pPixmap->devKind = pitch;
    pPixmap->devPrivate.ptr = umpbuf->addr;

    HASH_ADD_PTR(self->HashPixmapToUMP, pPixmap, umpbuf);

    DebugMsg("MigratePixmapToUMP %p, new buf = %p\n", pPixmap, umpbuf);
    return umpbuf;
}

static void UpdateOverlay(ScreenPtr pScreen, DrawablePtr   pDraw);

typedef UMPBufferInfoRec MaliDRI2BufferPrivateRec;
typedef UMPBufferInfoPtr MaliDRI2BufferPrivatePtr;

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr                pScreen  = pDraw->pScreen;
    ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr           buffer;
    MaliDRI2BufferPrivatePtr privates;
    ump_handle               handle;
    Rk30MaliDRI2 *private = RK30_MALI_DRI2(pScrn);
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    Bool                     can_use_overlay = TRUE;
    PixmapPtr                pWindowPixmap;

    if (!(buffer = calloc(1, sizeof *buffer))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        return NULL;
    }

    if (pDraw->type == DRAWABLE_WINDOW &&
        (pWindowPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw)))
    {
        DebugMsg("win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d, screen_x=%d, screen_y=%d)\n",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,
                 pWindowPixmap->screen_x, pWindowPixmap->screen_y);
    }

    /* If it is a pixmap, just migrate this pixmap to UMP buffer */
/*    if (pDraw->type == DRAWABLE_PIXMAP)
    {
        if (!(privates = MigratePixmapToUMP((PixmapPtr)pDraw))) {
            ErrorF("MaliDRI2CreateBuffer: MigratePixmapToUMP failed\n");
            free(buffer);
            return NULL;
        }
        privates->refcount++;
        buffer->attachment    = attachment;
        buffer->driverPrivate = privates;
        buffer->format        = format;
        buffer->flags         = 0;
        buffer->cpp           = pDraw->bitsPerPixel / 8;
        buffer->pitch         = ((PixmapPtr)pDraw)->devKind;
        buffer->name = ump_secure_id_get(privates->handle);

        DebugMsg("DRI2CreateBuffer pix=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d\n",
                 pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
                 privates->width, privates->height, buffer->cpp, privates->depth);

        return buffer;
    }
*/
    /* The drawable must be a window for using hardware overlays */
    if (pDraw->type != DRAWABLE_WINDOW){
        ErrorF("Unexpected pDraw->type (%d) in MaliDRI2CreateBuffer\n", pDraw->type);
        return NULL;
    }
//        can_use_overlay = FALSE;

    if (!(privates = calloc(1, sizeof *privates))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        free(buffer);
        return NULL;
    }


    /* We could not allocate disp layer or get framebuffer secure id */
    if (!overlay || private->ump_fb_secure_id1 == UMP_INVALID_SECURE_ID)
        can_use_overlay = FALSE;

    /* Overlay is already used by a different window */
    if (private->pOverlayWin && private->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32)
        can_use_overlay = FALSE;

    /* The default common values */
    buffer->attachment    = attachment;
    buffer->driverPrivate = privates;
    buffer->format        = format;
    buffer->flags         = 0;
    buffer->cpp           = pDraw->bitsPerPixel / 8;

    privates->size   = pDraw->height * buffer->pitch;
    privates->width  = pDraw->width;
    privates->height = pDraw->height;
    privates->depth  = pDraw->depth;

/*    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size) {
        DebugMsg("Not enough space in the offscreen framebuffer (wanted %d for DRI2)\n",
                 privates->size);
        can_use_overlay = FALSE;
    }
*/

if (private->tech_flag < 2){
    if(private->tech_flag == 1)
	can_use_overlay = FALSE;
    private->tech_flag++;
}

    if (can_use_overlay) {
    private->bOverlayWinEnabled = TRUE;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * overlay->var.xres_virtual + 7) / 8) * 8;
    privates->size   = pDraw->height * buffer->pitch;
//	if(pDraw->x == 0 && pDraw->y == 0)
        if( overlay->var.yres != overlay->saved_var.yres || overlay->var.xres != overlay->saved_var.xres)
		OvlSetMode(pScrn, overlay->saved_var.xres, overlay->saved_var.yres, 2);

        /* Use offscreen part of the framebuffer as an overlay */
        privates->handle = UMP_INVALID_MEMORY_HANDLE;
//        privates->addr = disp->framebuffer_addr;
        privates->addr = NULL;//overlay->fb_mem[0];

	if(private->FsGPUDD)// && pDraw->x == 0 && pDraw->y == 0 && overlay->var.yres == pDraw->height)
	    buffer->name = private->ump_fb_secure_id1;
	else
	    buffer->name = private->ump_fb_secure_id2;

//        buffer->flags = disp->gfx_layer_size; /* this is offset */
        buffer->flags = 0;//((pDraw->x + pDraw->y * overlay->var.xres_virtual)<<2);

        private->pOverlayWin = (WindowPtr)pDraw;

/*        if (sunxi_layer_set_x8r8g8b8_input_buffer(disp, buffer->flags,
                    privates->width, privates->height, buffer->pitch / 4) < 0) {
            ErrorF("Failed to set the source buffer for sunxi disp layer\n");
        }*/
    }
    else {
    private->bOverlayWinEnabled = FALSE;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;
    privates->size   = pDraw->height * buffer->pitch;
        /* Allocate UMP memory buffer */
#ifdef HAVE_LIBUMP_CACHE_CONTROL
        privates->handle = ump_ref_drv_allocate(privates->size,
                                    UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR |
                                    UMP_REF_DRV_CONSTRAINT_USE_CACHE);
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(ump_secure_id_get(privates->handle),
                                      UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
#else
        privates->handle = ump_ref_drv_allocate(privates->size,
                                    UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR);
#endif
        if (privates->handle == UMP_INVALID_MEMORY_HANDLE) {
            ErrorF("Failed to allocate UMP buffer (size=%d)\n",
                   (int)privates->size);
        }
        privates->addr = ump_mapped_pointer_get(privates->handle);
        buffer->name = ump_secure_id_get(privates->handle);
        buffer->flags = 0;
    }

    DebugMsg("DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d\n",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
             privates->width, privates->height, buffer->cpp, privates->depth);

    return buffer;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    MaliDRI2BufferPrivatePtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *drvpriv = RK30_MALI_DRI2(pScrn);

/*    if (drvpriv->pOverlayDirtyDRI2Buf == buffer)
        drvpriv->pOverlayDirtyDRI2Buf = NULL;
*/
    DebugMsg("DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d\n",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment);

    if (buffer != NULL) {
        privates = (MaliDRI2BufferPrivatePtr)buffer->driverPrivate;
        if (!privates->pPixmap) {
            /* If pPixmap != 0, then these are freed in DestroyPixmap */
            if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
                ump_mapped_pointer_release(privates->handle);
                ump_reference_release(privates->handle);
            }
        }
        if (--privates->refcount <= 0) {
            DebugMsg("free(privates)\n");
            free(privates);
        }
        free(buffer);
    }
}

/* Do ordinary copy */
static void MaliDRI2CopyRegion_copy(DrawablePtr pDraw,
                                    RegionPtr pRegion,
                                    DRI2BufferPtr pDstBuffer,
                                    DRI2BufferPtr pSrcBuffer)
{
    GCPtr pGC;
    RegionPtr copyRegion;
    ScreenPtr pScreen = pDraw->pScreen;
    MaliDRI2BufferPrivatePtr privates;
    PixmapPtr pScratchPixmap;
    privates = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_CPU);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif

    pGC = GetScratchGC(pDraw->depth, pScreen);
    pScratchPixmap = GetScratchPixmapHeader(pScreen,
                                            privates->width, privates->height,
                                            privates->depth, pSrcBuffer->cpp * 8,
                                            pSrcBuffer->pitch,
                                            privates->addr + pSrcBuffer->flags);
    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);
    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDraw, pGC);
    (*pGC->ops->CopyArea)((DrawablePtr)pScratchPixmap, pDraw, pGC, 0, 0,
                          pDraw->width, pDraw->height, 0, 0);
    FreeScratchPixmapHeader(pScratchPixmap);
    FreeScratchGC(pGC);

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif
}

static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *drvpriv = RK30_MALI_DRI2(pScrn);
    MaliDRI2BufferPrivatePtr bufpriv = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    UpdateOverlay(pScreen, pDraw);

//DebugMsg("Expose we:%d  hndl:%d\n",drvpriv->bOverlayWinEnabled ,bufpriv->handle);

    if (!drvpriv->bOverlayWinEnabled && bufpriv->handle != UMP_INVALID_MEMORY_HANDLE) {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, pDstBuffer, pSrcBuffer);
//        drvpriv->pOverlayDirtyDRI2Buf = NULL;
        return;
    }
    else
    if(!drvpriv->FsGPUDD){
//DebugMsg("Expose put\n");
	drvpriv->rga_pa = OvlPutBufToSrcn(pScrn, overlay->phadr_mem[1], overlay->var.xres_virtual,
				pDraw->width, pDraw->height, pDraw->x, pDraw->y, drvpriv->rga_pa);
}
    /* Mark the overlay as "dirty" and remember the last up to date DRI2 buffer */
//    drvpriv->pOverlayDirtyDRI2Buf = pSrcBuffer;

    /* Activate the overlay */
//    sunxi_layer_set_output_window(disp, pDraw->x, pDraw->y, pDraw->width, pDraw->height);
//    OvlEnable(pScrn, 1);
}

/************************************************************************/

static void UpdateOverlay(ScreenPtr pScreen, DrawablePtr   pDraw)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *self = RK30_MALI_DRI2(pScrn);
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;


    if (!self->pOverlayWin || !overlay)
        return;

    /* Disable overlays if the hardware cursor is not in use */
/*    if (!self->bHardwareCursorIsInUse) {
        if (self->bOverlayWinEnabled) {
            DebugMsg("Disabling overlay (no hardware cursor)\n");
//	    OvlEnable(pScrn, 0);
            self->bOverlayWinEnabled = FALSE;
        }
        return;
    }
*/
    /* If the window is not mapped, make sure that the overlay is disabled */
/*    if (!self->pOverlayWin->mapped)
    {
        if (self->bOverlayWinEnabled) {
            DebugMsg("Disabling overlay (window is not mapped)\n");
//	    OvlEnable(pScrn, 0);
            self->bOverlayWinEnabled = FALSE;
        }
        return;
    }
*/
    /*
     * Walk the windows tree to get the obscured/unobscured status of
     * the window (because we can't rely on self->pOverlayWin->visibility
     * for redirected windows).
     */

//    self->bWalkingAboveOverlayWin = FALSE;
//    self->bOverlayWinOverlapped = FALSE;
/*    FancyTraverseTree(pScreen->root, WindowWalker, self);

//    If the window got overlapped -> disable overlay
    if (self->bOverlayWinOverlapped && self->bOverlayWinEnabled) {
        DebugMsg("Disabling overlay (window is obscured)\n");
        FlushOverlay(pScreen);
        self->bOverlayWinEnabled = FALSE;
//	OvlEnable(pScrn, 0);
        return;
    }
*/
    /* If the window got moved -> update overlay position */
//    if (!self->bOverlayWinOverlapped &&
        if((self->overlay_x != pDraw->x ||
         self->overlay_y != pDraw->y))
    {
        self->overlay_x = pDraw->x;
        self->overlay_y = pDraw->y;

/*        sunxi_layer_set_output_window(disp, self->pOverlayWin->drawable.x,
                                      self->pOverlayWin->drawable.y,
                                      self->pOverlayWin->drawable.width,
                                      self->pOverlayWin->drawable.height);
*/
	OvlClearBuf(pScrn, 0);
        DebugMsg("Move overlay to (%d, %d)\n", self->overlay_x, self->overlay_y);
    }

/*        if((self->overlay_w != pDraw->width ||
         self->overlay_h != pDraw->height))
    {
        self->overlay_w = pDraw->width;
        self->overlay_h = pDraw->height;

	OvlClearBuf(pScrn, 0);
    }
*/
    /* If the window got unobscured -> enable overlay */
/*    if (!self->bOverlayWinOverlapped && !self->bOverlayWinEnabled) {
        DebugMsg("Enabling overlay (window is fully unobscured)\n");
        self->bOverlayWinEnabled = TRUE;
//	OvlEnable(pScrn, 1);
    }
*/
}

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *private = RK30_MALI_DRI2(pScrn);
    Bool ret;


    if (pWin == private->pOverlayWin) {
    private->tech_flag = 0;
	OvlClearBuf(pScrn, 0);
//	OvlEnable(pScrn, 0);
        private->pOverlayWin = NULL;
	private->rga_pa = 0;
	OvlRGAUnlock(pScrn);
        DebugMsg("DestroyWindow %p\n", pWin);
    }

    pScreen->DestroyWindow = private->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    private->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static Bool
DestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *self = RK30_MALI_DRI2(pScrn);
    Bool result;
    UMPBufferInfoPtr umpbuf;
    HASH_FIND_PTR(self->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("DestroyPixmap %p for migrated UMP pixmap (UMP buffer=%p)\n", pPixmap, umpbuf);

        pPixmap->devKind = umpbuf->BackupDevKind;
        pPixmap->devPrivate.ptr = umpbuf->BackupDevPrivatePtr;

        ump_mapped_pointer_release(umpbuf->handle);
        ump_reference_release(umpbuf->handle);

        HASH_DEL(self->HashPixmapToUMP, umpbuf);
        DebugMsg("umpbuf->refcount=%d\n", umpbuf->refcount);
        if (--umpbuf->refcount <= 0) {
            DebugMsg("free(umpbuf)\n");
            free(umpbuf);
        }
    }

    pScreen->DestroyPixmap = self->DestroyPixmap;
    result = (*pScreen->DestroyPixmap) (pPixmap);
    self->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = DestroyPixmap;


    return result;
}

static void EnableHWCursor(ScrnInfoPtr pScrn)
{
    Rk30MaliDRI2 *self = RK30_MALI_DRI2(pScrn);
    Rk30DispHardwareCursor *hwc = RK30_DISP_HWC(pScrn);

    if (!self->bHardwareCursorIsInUse) {
        DebugMsg("EnableHWCursor\n");
        self->bHardwareCursorIsInUse = TRUE;
    }

//    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

/*    if (self->EnableHWCursor) {
        hwc->EnableHWCursor = self->EnableHWCursor;
        (*hwc->EnableHWCursor) (pScrn);
        self->EnableHWCursor = hwc->EnableHWCursor;
        hwc->EnableHWCursor = EnableHWCursor;
    }*/
}

static void DisableHWCursor(ScrnInfoPtr pScrn)
{
    Rk30MaliDRI2 *self = RK30_MALI_DRI2(pScrn);
    Rk30DispHardwareCursor *hwc = RK30_DISP_HWC(pScrn);

    if (self->bHardwareCursorIsInUse) {
        self->bHardwareCursorIsInUse = FALSE;
        DebugMsg("DisableHWCursor\n");
    }

//    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

/*    if (self->DisableHWCursor) {
        hwc->DisableHWCursor = self->DisableHWCursor;
        (*hwc->DisableHWCursor) (pScrn);
        self->DisableHWCursor = hwc->DisableHWCursor;
        hwc->DisableHWCursor = DisableHWCursor;
    }*/
}

Rk30MaliDRI2 *Rk30MaliDRI2_Init(ScreenPtr pScreen, Bool FsGPUDD)
{
    Bool bUseOverlay = TRUE;
    int drm_fd, fd;
    DRI2InfoRec info;
    ump_secure_id ump_id1, ump_id2, ump_id3, ump_id4;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    ump_id1 = ump_id2 = UMP_INVALID_SECURE_ID;

    if (overlay && bUseOverlay) {
        /*
         * Workaround some glitches with secure id assignment (make sure
         * that GET_UMP_SECURE_ID_BUF1 and GET_UMP_SECURE_ID_BUF2 allocate
         * lower id numbers than GET_UMP_SECURE_ID_SUNXI_FB)
         */

/*	fd = open("/dev/fb0", O_RDWR);
        ioctl(fd, GET_UMP_SECURE_ID_BUF1, &ump_id3);
        ioctl(fd, GET_UMP_SECURE_ID_BUF2, &ump_id4);
	close(fd);
*/
        ioctl(overlay->fd, GET_UMP_SECURE_ID_BUF1, &ump_id1);
        ioctl(overlay->fd, GET_UMP_SECURE_ID_BUF2, &ump_id2);

//        ioctl(fd, GET_UMP_SECURE_ID_BUF1, &ump_id1);
//        if (ioctl(disp->fd_fb, GET_UMP_SECURE_ID_SUNXI_FB, &ump_id_fb) < 0 ||
        if (ump_id1 == UMP_INVALID_SECURE_ID || ump_id2 == UMP_INVALID_SECURE_ID) {
            xf86DrvMsg(pScreen->myNum, X_INFO,
                  "GET_UMP_SECURE_ID ioctl failed, overlays can't be used\n");
//            ump_id_fb = UMP_INVALID_SECURE_ID;
        }

    }

    if (!xf86LoadKernelModule("mali"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali' kernel module\n");
    if (!xf86LoadKernelModule("mali_drm"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali_drm' kernel module\n");

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return NULL;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
        ErrorF("Rk30MaliDRI2_Init: drmOpen failed!\n");
        return NULL;
    }

    if (ump_open() != UMP_OK) {
        drmClose(drm_fd);
        ErrorF("Rk30MaliDRI2_Init: ump_open() != UMP_OK\n");
        return NULL;
    }

    if (overlay && ump_id1 != UMP_INVALID_SECURE_ID && ump_id2 != UMP_INVALID_SECURE_ID)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "enabled display controller hardware overlays for DRI2\n");
    else if (bUseOverlay)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays can't be used for DRI2\n");
    else
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays are not used for DRI2\n");

    info.version = 3;

    info.driverName = "rk30-mali";
    info.deviceName = "/dev/dri/card0";
    info.fd = drm_fd;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;


    if (!DRI2ScreenInit(pScreen, &info)) {
        drmClose(drm_fd);
        return NULL;
    }
    else {
//        Rk30DispHardwareCursor *hwc = RK30_DISP_HWC(pScrn);
        Rk30MaliDRI2 *private = calloc(1, sizeof(Rk30MaliDRI2));

        /* Wrap the current DestroyWindow function */
        private->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;
        /* Wrap the current PostValidateTree function */
//        private->PostValidateTree = pScreen->PostValidateTree;
//        pScreen->PostValidateTree = PostValidateTree;
        /* Wrap the current GetImage function */
//        private->GetImage = pScreen->GetImage;
//        pScreen->GetImage = GetImage;
        /* Wrap the current DestroyPixmap function */
        private->DestroyPixmap = pScreen->DestroyPixmap;
        pScreen->DestroyPixmap = DestroyPixmap;

        /* Wrap hardware cursor callback functions */
/*        if (hwc) {
            private->EnableHWCursor = hwc->EnableHWCursor;
            hwc->EnableHWCursor = EnableHWCursor;
            private->DisableHWCursor = hwc->DisableHWCursor;
            hwc->DisableHWCursor = DisableHWCursor;
        }
*/
        private->ump_fb_secure_id1 = ump_id1;
        private->ump_fb_secure_id2 = ump_id2;
        private->drm_fd = drm_fd;
	private->rga_pa = 0;
	private->tech_flag = 0;
	private->FsGPUDD = FsGPUDD;

        return private;
    }
}

void Rk30MaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliDRI2 *private = RK30_MALI_DRI2(pScrn);
//    Rk30DispHardwareCursor *hwc = RK30_DISP_HWC(pScrn);

    /* Unwrap functions */
    pScreen->DestroyWindow    = private->DestroyWindow;
//    pScreen->PostValidateTree = private->PostValidateTree;
//    pScreen->GetImage         = private->GetImage;
    pScreen->DestroyPixmap    = private->DestroyPixmap;

/*    if (hwc) {
        hwc->EnableHWCursor  = private->EnableHWCursor;
        hwc->DisableHWCursor = private->DisableHWCursor;
    }
*/
    drmClose(private->drm_fd);
    DRI2CloseScreen(pScreen);
}
