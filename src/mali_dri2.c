/*
 * Adapted for rk3066 olegk0 <olegvedi@gmail.com>
 *
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
#include "rk3066.h"
#include "disp_hwcursor.h"
#include "mali_dri2.h"

#include <unistd.h>
#include <fcntl.h>

//#undef DEBUG
#ifdef DEBUG
#define DebugMsg(...) ErrorF(__VA_ARGS__)
#else
#define DebugMsg(...)
#endif

//#define HAVE_LIBUMP_CACHE_CONTROL 0

/* Migrate pixmap to UMP buffer */
static UMPBufferInfoPtr
MigratePixmapToUMP(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliPtr rk_3d = FBDEVPTR(pScrn)->Rk30Mali;
    UMPBufferInfoPtr umpbuf;
    size_t pitch = ((pPixmap->devKind + 7) / 8) * 8;
    size_t size = pitch * pPixmap->drawable.height;

    HASH_FIND_PTR(rk_3d->HashPixmapToUMP, &pPixmap, umpbuf);

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

    HASH_ADD_PTR(rk_3d->HashPixmapToUMP, pPixmap, umpbuf);

    DebugMsg("MigratePixmapToUMP %p, new buf = %p\n", pPixmap, umpbuf);
    return umpbuf;
}

//static void UpdateOverlay(ScreenPtr pScreen, DrawablePtr   pDraw);

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
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Rk30MaliPtr rk_3d = pMxv->Rk30Mali;
    OvlHWPtr overlay = pMxv->OvlHW;
    Bool                     can_use_overlay = TRUE;
    PixmapPtr                pWindowPixmap;

    if (pDraw->type == DRAWABLE_WINDOW &&
        (pWindowPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw)))
    {
        DebugMsg("win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d, screen_x=%d, screen_y=%d)\n",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,
                 pWindowPixmap->screen_x, pWindowPixmap->screen_y);
    }

    if(attachment != DRI2BufferBackLeft && rk_3d->buf_back != NULL){
	DebugMsg("DRI2 return NullBuffer\n");
	return rk_3d->buf_back;
    }

    if (!(buffer = calloc(1, sizeof *buffer))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        return NULL;
    }


    /* If it is a pixmap, just migrate this pixmap to UMP buffer */
    if (pDraw->type == DRAWABLE_PIXMAP)
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

    /* The drawable must be a window for using hardware overlays */
    if (pDraw->type != DRAWABLE_WINDOW){
        ErrorF("Unexpected pDraw->type (%d) in MaliDRI2CreateBuffer\n", pDraw->type);
        return NULL;
    }

    if (!(privates = calloc(1, sizeof *privates))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        free(buffer);
        return NULL;
    }

    /* We could not get framebuffer secure id */
    if (rk_3d->ump_fb_secure_id1 == UMP_INVALID_SECURE_ID)
        can_use_overlay = FALSE;

    /* Overlay is already used by a different window */
    if (rk_3d->pOverlayWin && rk_3d->pOverlayWin != (void *)pDraw)
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
    /* Stride must be 8 bytes aligned for Mali400 */
//    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;
    buffer->pitch         = ((buffer->cpp * overlay->cur_var.xres_virtual + 7) / 8) * 8;
    privates->size     = pDraw->height * buffer->pitch;
    privates->width    = pDraw->width;
    privates->height   = pDraw->height;
    privates->depth    = pDraw->depth;

/*    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size) {
        DebugMsg("Not enough space in the offscreen framebuffer (wanted %d for DRI2)\n",
                 privates->size);
        can_use_overlay = FALSE;
    }
*/
/*
    if((pDraw->width+pDraw->x) > overlay->cur_var.xres ||
		(pDraw->height+pDraw->y) > overlay->cur_var.yres)
        can_use_overlay = FALSE;
*/
    if(pDraw->width < 2 || pDraw->height < 2){
     can_use_overlay = FALSE;
    }

    if(attachment != DRI2BufferBackLeft){
//     can_use_overlay = FALSE;
	free(privates);
	buffer->driverPrivate = NULL;
	buffer->name = UMP_INVALID_SECURE_ID;
	if(rk_3d->buf_back == NULL)
	    rk_3d->buf_back = buffer;
	goto end;
    }

    if(rk_3d->OvlPg == ERRORL){
	rk_3d->OvlPg = OvlAllocLay(pScrn, ANYL);
	if(rk_3d->OvlPg == ERRORL)
	    can_use_overlay = FALSE;
	else{//init
	    OvlSetupFb(pScrn, 0, 0, rk_3d->OvlPg);
	    OvlEnable(pScrn, rk_3d->OvlPg, 1);
	}
    }

    rk_3d->bOverlayWinEnabled = can_use_overlay;

    if (can_use_overlay)
        rk_3d->pOverlayWin = (WindowPtr)pDraw;
//	buffer->pitch         = ((buffer->cpp * overlay->OvlLay[rk_3d->OvlPg].var.xres_virtual + 7) / 8) * 8;
//	rk_3d->bOverlayWinEnabled = TRUE;


// Stride must be 8 bytes aligned for Mali400
//	privates->size   = pDraw->height * buffer->pitch;
//	buffer->flags = 0;

	privates->handle = UMP_INVALID_MEMORY_HANDLE;
	privates->addr = NULL;
	buffer->name = rk_3d->ump_fb_secure_id1;
	privates->handle = ump_handle_create_from_secure_id(buffer->name);
	privates->addr = ump_mapped_pointer_get(privates->handle);
//        privates->addr = NULL;//(void*)0x94000000;
/*    }
    else
    {
//	rk_3d->bOverlayWinEnabled = FALSE;
//        Allocate UMP memory buffer
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
*/
end:
    DebugMsg("DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d adr=%p\n",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
             privates->width, privates->height, buffer->cpp, privates->depth,privates->addr);

    return buffer;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    MaliDRI2BufferPrivatePtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliPtr rk_3d = FBDEVPTR(pScrn)->Rk30Mali;

    DebugMsg("DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d\n",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment);

    if (buffer != NULL) {
    if(buffer->driverPrivate != NULL){
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
    }
	if(buffer != rk_3d->buf_back)
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

//    DebugMsg("Enter MaliDRI2CopyRegion    buf_name:%d\n",pSrcBuffer->name);
/*
#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
//        That's a normal UMP allocation, not a wrapped framebuffer
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_CPU);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif
*/
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
/*
#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
//        That's a normal UMP allocation, not a wrapped framebuffer
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif
*/
}
//**********************************************
#if 0
void Reset_3DOvl(ScreenPtr pScreen, Bool FR)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Rk30MaliPtr rk_3d = pMxv->Rk30Mali;
    OvlHWPtr overlay = pMxv->OvlHW;
    unsigned long tmp[2];
    SFbioDispSet pt;
    int i;

    DebugMsg("Enter Reset_ovl\n");

    if(FR){
	i = 0;
    }
    else{
	pt.poffset_x = 0;
	pt.poffset_y = 0;
	pt.scale_w = 0 ;
	pt.scale_h = 0;
	pt.ssize_w = overlay->cur_var.xres;
	pt.ssize_h = overlay->cur_var.yres;
        ioctl(rk_3d->fd_3d, FBIOSET_DISP_PSET, &pt);
	i = 1;
//	memcpy(&rk_3d->fb_var, &overlay->sav_var, sizeof(struct fb_var_screeninfo));
    }

//    OvlClearBuf(pScrn, 0, FB);
//    OvlClearBuf(pScrn, 1, FB3D);
/*    tmp[0] = 0;
    tmp[1] = 0;
    ioctl(rk_3d->fd_3d, FBIOSET_FBMEM_OFFSET, &tmp);
*/
/*    if(FR){
	OvlResetFB(pScrn, FBUI);
	OvlResetFB(pScrn, FB3D);
    }
    else
	ioctl(rk_3d->fd_3d, FBIOPUT_VSCREENINFO, &overlay->cur_var);
//	ioctl(rk_3d->fd_3d, FBIOPAN_DISPLAY, &overlay->cur_var);

    ioctl(rk_3d->fd_3d, FBIOSET_ENABLE, &i);
*/
//    rk_3d->ovl_cr = FALSE;
}
#endif
//********************************************************
static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Rk30MaliPtr rk_3d = pMxv->Rk30Mali;
    OvlHWPtr overlay = pMxv->OvlHW;
    Bool	Change=FALSE;
    int ret;

    MaliDRI2BufferPrivatePtr bufpriv = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;

//    UpdateOverlay(pScreen, pDraw);

//DebugMsg("Expose we:%d  hndl:%d\n",rk_3d->bOverlayWinEnabled ,bufpriv->handle);

    if (!rk_3d->bOverlayWinEnabled && bufpriv->handle != UMP_INVALID_MEMORY_HANDLE) {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, pDstBuffer, pSrcBuffer);
//        rk_3d->pOverlayDirtyDRI2Buf = NULL;
        return;
    }

    if(rk_3d->ovl_x != pDraw->x || rk_3d->ovl_y != pDraw->y){
	rk_3d->ovl_x = pDraw->x;
	rk_3d->ovl_y = pDraw->y;
	Change = TRUE;
        DebugMsg("Change pos to x:%d,y:%d\n", pDraw->x,pDraw->y);
    }
    if(rk_3d->ovl_h != pDraw->height || rk_3d->ovl_w != pDraw->width){
	rk_3d->ovl_h = pDraw->height;
	rk_3d->ovl_w = pDraw->width;
	Change = TRUE;
        DebugMsg("Change size to w:%d,h:%d\n", pDraw->width,pDraw->height);
    }

    if(Change)
	OvlSetupBufDrw(pScrn, pDraw->x, pDraw->y, pDraw->width, pDraw->height, overlay->cur_var.xres_virtual, rk_3d->OvlPg);

    ret = OvlCpBufToDisp(pScrn, rk_3d->PMemBuf, rk_3d->OvlPg);
    DebugMsg("OvlCpBufToDisp ret:%d\n", ret);
    OvlWaitSync(pScrn, rk_3d->OvlPg);
//}
}

/************************************************************************/

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliPtr rk_3d = FBDEVPTR(pScrn)->Rk30Mali;
    Bool ret;


    if (pWin == rk_3d->pOverlayWin) {
//	Reset_3DOvl(pScreen, 1);
	rk_3d->lstatus = ST_INIT;
        rk_3d->ovl_cr = FALSE;
        rk_3d->pOverlayWin = NULL;
	rk_3d->ovl_x = 0;
	rk_3d->ovl_y = 0;
	rk_3d->ovl_w = 0;
	rk_3d->ovl_h = 0;
	if(rk_3d->OvlPg != ERRORL){
//TODO clear
	    OvlEnable(pScrn, rk_3d->OvlPg, 0);
	    OvlFreeLay(pScrn, rk_3d->OvlPg);
	    rk_3d->OvlPg = ERRORL;
	}
	if(rk_3d->buf_back){
	    free(rk_3d->buf_back);
	    rk_3d->buf_back = NULL;
	}

        DebugMsg("DestroyWindow %p\n", pWin);
    }

    pScreen->DestroyWindow = rk_3d->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    rk_3d->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static Bool
DestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Rk30MaliPtr rk_3d = FBDEVPTR(pScrn)->Rk30Mali;
    Bool result;
    UMPBufferInfoPtr umpbuf;
    HASH_FIND_PTR(rk_3d->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("DestroyPixmap %p for migrated UMP pixmap (UMP buffer=%p)\n", pPixmap, umpbuf);

        pPixmap->devKind = umpbuf->BackupDevKind;
        pPixmap->devPrivate.ptr = umpbuf->BackupDevPrivatePtr;

        ump_mapped_pointer_release(umpbuf->handle);
        ump_reference_release(umpbuf->handle);

        HASH_DEL(rk_3d->HashPixmapToUMP, umpbuf);
        DebugMsg("umpbuf->refcount=%d\n", umpbuf->refcount);
        if (--umpbuf->refcount <= 0) {
            DebugMsg("free(umpbuf)\n");
            free(umpbuf);
        }
    }

    pScreen->DestroyPixmap = rk_3d->DestroyPixmap;
    result = (*pScreen->DestroyPixmap) (pPixmap);
    rk_3d->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = DestroyPixmap;


    return result;
}

void Rk30MaliDRI2_Init(ScreenPtr pScreen)
{
    int drm_fd;
    DRI2InfoRec info;
    ump_secure_id ump_id1, ump_id2;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Bool isOverlay = TRUE;

#if DRI2INFOREC_VERSION >= 4
    const char *driverNames[1];
#endif

    pMxv->Rk30Mali = NULL;
    if(pMxv->OvlHW == NULL){
    	xf86DrvMsg(pScreen->myNum, X_ERROR, "Rk30MaliDRI2_Init: Overlay not found!\n");
        return;
    }

    OvlHWPtr overlay = pMxv->OvlHW;

    if (!xf86LoadKernelModule("ump"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'ump' kernel module\n");
    if (!xf86LoadKernelModule("disp_ump"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'disp_ump' kernel module\n");
    if (!xf86LoadKernelModule("mali"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali' kernel module\n");
    if (!xf86LoadKernelModule("drm"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'drm' kernel module\n");
    if (!xf86LoadKernelModule("mali_drm"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali_drm' kernel module\n");

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
        xf86DrvMsg(pScreen->myNum, X_ERROR, "Rk30MaliDRI2_Init: drmOpen failed!\n");
        return;
    }

    if (ump_open() != UMP_OK) {
        xf86DrvMsg(pScreen->myNum, X_ERROR, "Rk30MaliDRI2_Init: ump_open() != UMP_OK\n");
        goto err0;
    }

    if(!(pMxv->Rk30Mali = calloc(1, sizeof(Rk30MaliRec) ))){
        xf86DrvMsg(pScreen->myNum, X_ERROR, "Rk30MaliDRI2_Init: Mem alloc failed!\n");
        goto err0;
    }

    Rk30MaliPtr	rk_3d = pMxv->Rk30Mali;

    rk_3d->PMemBuf = OvlAllocMemPg(pScrn, BUF_MEM);
    if(rk_3d->PMemBuf == NULL){
	xf86DrvMsg(pScreen->myNum, X_INFO, "Alloc fb buf failed\n");
        goto err1;
    }

    ump_id1 = rk_3d->PMemBuf->ump_fb_secure_id;
    if (ump_id1 == UMP_INVALID_SECURE_ID) {
        xf86DrvMsg(pScreen->myNum, X_INFO, "GET_UMP_SECURE_ID failed\n");
        goto err1;
    }

    if (isOverlay){
        xf86DrvMsg(pScreen->myNum, X_INFO, "HW overlay for 3D activated\n");
    }
    else
        xf86DrvMsg(pScreen->myNum, X_INFO, "HW overlay for 3D not usable\n");


    info.version = 3;

    info.driverName = "rk30-mali";
    info.deviceName = "/dev/dri/card0";
    info.fd = drm_fd;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;

/*
#if DRI2INFOREC_VERSION >= 4
Bool USE_PAGEFLIP = TRUE;
    if (USE_PAGEFLIP)
    {
	info.version = 4;
	info.ScheduleSwap = MaliDRI2ScheduleSwap;
	info.GetMSC = NULL;
	info.ScheduleWaitMSC = NULL;
	info.numDrivers = 1;
	info.driverNames = driverNames;
	driverNames[0] = info.driverName;
    }
#endif
*/
    if (DRI2ScreenInit(pScreen, &info)){
        /* Wrap the current DestroyWindow function */
        rk_3d->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;
        /* Wrap the current PostValidateTree function */
//        rk_3d->PostValidateTree = pScreen->PostValidateTree;
//        pScreen->PostValidateTree = PostValidateTree;
        /* Wrap the current GetImage function */
//        rk_3d->GetImage = pScreen->GetImage;
//        pScreen->GetImage = GetImage;
        /* Wrap the current DestroyPixmap function */
        rk_3d->DestroyPixmap = pScreen->DestroyPixmap;
        pScreen->DestroyPixmap = DestroyPixmap;

        rk_3d->ump_fb_secure_id1 = ump_id1;
//        rk_3d->ump_fb_secure_id2 = ump_id2;
        rk_3d->drm_fd = drm_fd;
	rk_3d->ovl_x = 0;
	rk_3d->ovl_y = 0;
	rk_3d->ovl_w = 0;
	rk_3d->ovl_h = 0;
//	rk_3d->lstatus = ST_INIT;
//	rk_3d->ovl_cr = FALSE;
	rk_3d->buf_back = NULL;
	rk_3d->OvlPg = ERRORL;

        return;
    }

    xf86DrvMsg(pScreen->myNum, X_ERROR, "Rk30MaliDRI2_Init: DRI2ScreenInit failed!\n");
err2:
    OvlFreeMemPg(pScrn, rk_3d->PMemBuf);
err1:
    free(rk_3d);
err0:
    drmClose(drm_fd);
}

void Rk30MaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    if (pMxv->Rk30Mali != NULL) {
	OvlHWPtr overlay = pMxv->OvlHW;
	Rk30MaliPtr	rk_3d = pMxv->Rk30Mali;

    /* Unwrap functions */
        pScreen->DestroyWindow    = rk_3d->DestroyWindow;
//    pScreen->PostValidateTree = rk_3d->PostValidateTree;
//    pScreen->GetImage         = rk_3d->GetImage;
        pScreen->DestroyPixmap    = rk_3d->DestroyPixmap;

	OvlFreeMemPg(pScrn, rk_3d->PMemBuf);

//	    Reset_3DOvl(pScreen,1);

	if(rk_3d->buf_back){
	    free(rk_3d->buf_back);
	    rk_3d->buf_back = NULL;
	}
	DRI2CloseScreen(pScreen);
	drmClose(rk_3d->drm_fd);
	free(rk_3d);
	rk_3d = NULL;
    }
}
