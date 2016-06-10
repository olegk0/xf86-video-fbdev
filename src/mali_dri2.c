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
#include "disp_hwcursor.h"
#include "mali_dri2.h"

#include <unistd.h>
#include <fcntl.h>

//#undef DEBUG
#ifdef DEBUG
#define DebugMsg(...) {if(mali->debug) ErrorF(__VA_ARGS__);}
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
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;
    UMPBufferInfoPtr umpbuf;
    size_t pitch = ((pPixmap->devKind + 7) / 8) * 8;
    size_t size = pitch * pPixmap->drawable.height;

    HASH_FIND_PTR(mali->HashPixmapToUMP, &pPixmap, umpbuf);

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
        MFREE(umpbuf);
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

    HASH_ADD_PTR(mali->HashPixmapToUMP, pPixmap, umpbuf);

    DebugMsg("MigratePixmapToUMP %p, new buf = %p\n", pPixmap, umpbuf);
    return umpbuf;
}

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr			pScreen = pDraw->pScreen;
    ScrnInfoPtr			pScrn = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr		buffer;
    UMPBufferInfoPtr	privates;
    ump_handle			handle;
    FBDevPtr			pMxv = FBDEVPTR(pScrn);
    RkMaliPtr			mali = pMxv->RkMali;
    Bool				can_use_overlay = TRUE;
    PixmapPtr			pWindowPixmap;

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
    if (pDraw->type == DRAWABLE_PIXMAP)
    {
        if (!(privates = MigratePixmapToUMP((PixmapPtr)pDraw))) {
            ErrorF("MaliDRI2CreateBuffer: MigratePixmapToUMP failed\n");
            MFREE(buffer);
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

    /* The default common values */
    buffer->driverPrivate = NULL;
    buffer->attachment    = attachment;
    buffer->format        = format;
    buffer->flags         = 0;
    buffer->cpp           = pDraw->bitsPerPixel / 8;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;

    /* We are not interested in anything other than back buffer requests ... */
    if (attachment != DRI2BufferBackLeft || pDraw->type != DRAWABLE_WINDOW) {
        /* ... and just return some dummy UMP buffer */
    	buffer->name = UMP_INVALID_SECURE_ID;
        buffer->name     = mali->ump_null_secure_id;
        return buffer;
    }

    if (!(privates = calloc(1, sizeof *privates))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        MFREE(buffer);
        return NULL;
    }
    buffer->driverPrivate = privates;
    privates->size     = pDraw->height * buffer->pitch;
    privates->width    = pDraw->width;
    privates->height   = pDraw->height;
    privates->depth    = pDraw->depth;

    /* Overlay is already used by a different window */
    if (mali->pOverlayWin && mali->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32 && pDraw->bitsPerPixel != 16)
        can_use_overlay = FALSE;


/*    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size) {
        DebugMsg("Not enough space in the offscreen framebuffer (wanted %d for DRI2)\n",
                 privates->size);
        can_use_overlay = FALSE;
    }

    if((pDraw->width+pDraw->x) > overlay->cur_var.xres ||
		(pDraw->height+pDraw->y) > overlay->cur_var.yres)
        can_use_overlay = FALSE;
*/
    if(pDraw->width < 2 || pDraw->height < 2){
     can_use_overlay = FALSE;
    }

    if(mali->OvlPg == ERROR_L){
    	OvlLayoutType ltype;
    	if(mali->HWFullScrFor3D){
    		ltype = SCALE_L;
    		mali->OvlPgUI = OvlAllocLay( UI_L, ALC_FRONT_BACK_FB);
    		if(mali->OvlPgUI != ERROR_L){
    			mali->UIBackMemBuf = OvlGetBufByLay(mali->OvlPgUI, BACK_FB);
    			mali->UIBackMapBuf = OvlMapBufMem(mali->UIBackMemBuf);
    			if(mali->UIBackMapBuf){
    				OvlClrMemPg(mali->UIBackMemBuf);
    				OvlFlipFb( mali->OvlPgUI, BACK_FB, 0);
    			}else
    				OvlFreeLay(mali->OvlPgUI);
    		}
    	}
    	else
    		ltype = ANY_HW_L;

    	if(!mali->HWLayerFor3D){//use IPP/RGA
    		ltype += EMU_L;
    	}

    	mali->OvlPg = OvlAllocLay(ltype, ALC_FRONT_BACK_FB);
    	if(mali->OvlPg == ERROR_L){
    		ERRMSG("Cannot alloc overlay\n");
    		can_use_overlay = FALSE;
    	}else{//init
    		DebugMsg("MaliDRI2CreateBuffer: Alloc ovl:%d",mali->OvlPg);
    		mali->colorKey = HWAclSetColorKey(pScrn);
    		OvlSetupFb(mali->OvlPg, RKL_FORMAT_DEFAULT, pDraw->width, pDraw->height);
    		OvlEnable(mali->OvlPg, 1, 0);
    		mali->FrontMemBuf = OvlGetBufByLay(mali->OvlPg, FRONT_FB);
    		mali->BackMemBuf = OvlGetBufByLay(mali->OvlPg, BACK_FB);
    		mali->ump_fb_front_secure_id = OvlGetSidByMemPg(mali->FrontMemBuf);
    		mali->ump_fb_back_secure_id = OvlGetSidByMemPg(mali->BackMemBuf);
    		mali->FrontMapBuf = OvlMapBufMem(mali->FrontMemBuf);
    		mali->BackMapBuf = OvlMapBufMem(mali->BackMemBuf);
    		mali->scr_w = OvlGetXresByLay(UI_L);
			mali->scr_h = OvlGetYresByLay(UI_L);
    	}
    }

    mali->bOverlayWinEnabled = can_use_overlay;
    mali->lstatus++;
    if (can_use_overlay){
        mali->pOverlayWin = (WindowPtr)pDraw;
        buffer->pitch = buffer->cpp * OvlGetVXresByLay(mali->OvlPg);
        privates->handle = UMP_INVALID_MEMORY_HANDLE;
        privates->frame = mali->lstatus & 1;
    	if(privates->frame){
    		privates->addr = mali->FrontMapBuf;
    		buffer->name = mali->ump_fb_front_secure_id;
    	}
    	else{
    		privates->addr = mali->BackMapBuf;
    		buffer->name = mali->ump_fb_back_secure_id;
    	}
    }
    else
    {
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
//        buffer->flags = 0;

    }

end:
    DebugMsg("DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d adr=%p\n",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
             privates->width, privates->height, buffer->cpp, privates->depth,privates->addr);

    return buffer;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    UMPBufferInfoPtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;

    DebugMsg("DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d lstatus=%d\n",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment, mali->lstatus);

    if (buffer != NULL) {
    	if(buffer->driverPrivate != NULL){
    		mali->lstatus--;
    		privates = (UMPBufferInfoPtr)buffer->driverPrivate;
    		if(!privates->pPixmap) {
    			/* If pPixmap != 0, then these are freed in DestroyPixmap */
    			if(privates->handle != UMP_INVALID_MEMORY_HANDLE) {
    				ump_mapped_pointer_release(privates->handle);
    				ump_reference_release(privates->handle);
    			}
    		}
    		if(--privates->refcount <= 0) {
    			DebugMsg("free(privates)\n");
    			MFREE(privates);
    		}
    	}
    	MFREE(buffer);
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
    UMPBufferInfoPtr privates;
    PixmapPtr pScratchPixmap;
    privates = (UMPBufferInfoPtr)pSrcBuffer->driverPrivate;

//    DebugMsg("Enter MaliDRI2CopyRegion    buf_name:%d\n",pSrcBuffer->name);

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
//        That's a normal UMP allocation, not a wrapped framebuffer
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
//        That's a normal UMP allocation, not a wrapped framebuffer
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif

}
//********************************************************
static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    RkMaliPtr mali = pMxv->RkMali;
    int ret;
    Bool Changed=FALSE;

    UMPBufferInfoPtr bufpriv = (UMPBufferInfoPtr)pSrcBuffer->driverPrivate;

//DebugMsg("Expose we:%d  hndl:%d\n",mali->bOverlayWinEnabled ,bufpriv->handle);

    if (!mali->bOverlayWinEnabled && bufpriv->handle != UMP_INVALID_MEMORY_HANDLE) {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, pDstBuffer, pSrcBuffer);
        return;
    }

    if(mali->ovl_h != pDraw->height || mali->ovl_w != pDraw->width){
    	mali->ovl_h = pDraw->height;
    	mali->ovl_w = pDraw->width;
    	ret = OvlSetupFb(mali->OvlPg, RKL_FORMAT_DEFAULT, pDraw->width, pDraw->height);
    	if(mali->HWFullScrFor3D && !mali->HWLayerFor3D && mali->OvlPgUI != ERROR_L){
    		OvlSetIPP_RGADst( mali->OvlPg, mali->UIBackMemBuf);
    	}
        DebugMsg("Change size to w:%d,h:%d ret:%d\n", pDraw->width,pDraw->height, ret);
//        mali->colorKey = HWAclSetColorKey(pScrn);
        Changed = TRUE;
    }

    if(mali->ovl_x != pDraw->x || mali->ovl_y != pDraw->y || Changed){
    	mali->ovl_x = pDraw->x;
    	mali->ovl_y = pDraw->y;
    	if(mali->HWFullScrFor3D)
    		ret = OvlSetupDrw(mali->OvlPg, 0, 0, mali->scr_w, mali->scr_h);
    	else
    		ret = OvlSetupDrw(mali->OvlPg, mali->ovl_x, mali->ovl_y, pDraw->width, pDraw->height);
        DebugMsg("Change pos to x:%d,y:%d ret:%d\n", pDraw->x,pDraw->y, ret);
        Changed = TRUE;
    }

    if(Changed){
    	HWAclFillKeyHelper(pDraw, mali->colorKey, pRegion, FALSE);
    }

    if(bufpriv->frame)
    	OvlFlipFb(mali->OvlPg, FRONT_FB, 0);
    else
    	OvlFlipFb(mali->OvlPg, BACK_FB, 0);

    if(mali->WaitForSync)
    	OvlWaitVSync();
}

/************************************************************************/

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;
    Bool ret;


    if (pWin == mali->pOverlayWin) {
    	mali->lstatus = 0;
        mali->pOverlayWin = NULL;
        mali->ovl_x = 0;
        mali->ovl_y = 0;
        mali->ovl_w = 0;
        mali->ovl_h = 0;
        if(mali->OvlPg != ERROR_L){
    		OvlFreeLay(mali->OvlPg);
    		mali->OvlPg = ERROR_L;
    	}
        if(mali->OvlPgUI != ERROR_L){
    		OvlFreeLay(mali->OvlPgUI);
    		mali->OvlPgUI = ERROR_L;
    	}
        DebugMsg("DestroyWindow %p\n", pWin);
    }

    pScreen->DestroyWindow = mali->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    mali->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static Bool
DestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;
    Bool result;
    UMPBufferInfoPtr umpbuf;
    HASH_FIND_PTR(mali->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("DestroyPixmap %p for migrated UMP pixmap (UMP buffer=%p)\n", pPixmap, umpbuf);

        pPixmap->devKind = umpbuf->BackupDevKind;
        pPixmap->devPrivate.ptr = umpbuf->BackupDevPrivatePtr;

        ump_mapped_pointer_release(umpbuf->handle);
        ump_reference_release(umpbuf->handle);

        HASH_DEL(mali->HashPixmapToUMP, umpbuf);
        DebugMsg("umpbuf->refcount=%d\n", umpbuf->refcount);
        if (--umpbuf->refcount <= 0) {
            DebugMsg("MFREE(umpbuf)\n");
            MFREE(umpbuf);
        }
    }

    pScreen->DestroyPixmap = mali->DestroyPixmap;
    result = (*pScreen->DestroyPixmap) (pPixmap);
    mali->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = DestroyPixmap;


    return result;
}

void RkMaliDRI2_Init(ScreenPtr pScreen, Bool debug, Bool WaitForSync, Bool HWLayerFor3D, Bool HWFullScrFor3D)
{
    int drm_fd;
    DRI2InfoRec info;
    ump_secure_id ump_id1, ump_id2;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Bool isOverlay = TRUE;

    if (ump_open() != UMP_OK){
    	ERRMSG("Rk30MaliDRI2_Init: Can`t init UMP system!");
    	return;
    }
    pMxv->RkMali = NULL;
    if(pMxv->HWAcl == NULL){
    	ERRMSG("Rk30MaliDRI2_Init: Overlay not found!");
//        return;
        isOverlay = FALSE;
    }

    HWAclPtr hwacl = pMxv->HWAcl;

    if (!xf86LoadKernelModule("mali"))
    	INFMSG("can't load 'mali' kernel module");
    if (!xf86LoadKernelModule("drm"))
    	INFMSG("can't load 'drm' kernel module");
    if (!xf86LoadKernelModule("mali_drm"))
    	INFMSG("can't load 'mali_drm' kernel module");

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
    	ERRMSG("Rk30MaliDRI2_Init: drmOpen failed!");
        return;
    }

    if(!(pMxv->RkMali = calloc(1, sizeof(RkMaliRec) ))){
    	ERRMSG("Rk30MaliDRI2_Init: Mem alloc failed!");
        goto err0;
    }

    RkMaliPtr	mali = pMxv->RkMali;

	/* Try to allocate small dummy UMP buffers to secure id 1 and 2 */
    mali->ump_null_secure_id = UMP_INVALID_SECURE_ID;
   	mali->ump_null_handle = ump_ref_drv_allocate(4096, UMP_REF_DRV_CONSTRAINT_NONE);
    if (mali->ump_null_handle != UMP_INVALID_MEMORY_HANDLE)
    	mali->ump_null_secure_id = ump_secure_id_get(mali->ump_null_handle);

    if (mali->ump_null_secure_id == UMP_INVALID_SECURE_ID) {
    	INFMSG("GET_UMP_SECURE_ID failed");
        goto err1;
    }

    if (isOverlay){
    	INFMSG("HW overlay for 3D activated");
    }
    else
    	INFMSG("HW overlay for 3D not usable");

    info.version = 3;

    info.driverName = "rk30-mali";
    info.deviceName = "/dev/dri/card0";
    info.fd = drm_fd;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;

    if (DRI2ScreenInit(pScreen, &info)){
        /* Wrap the current DestroyWindow function */
        mali->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;
        /* Wrap the current PostValidateTree function */
//        mali->PostValidateTree = pScreen->PostValidateTree;
//        pScreen->PostValidateTree = PostValidateTree;
        /* Wrap the current GetImage function */
//        mali->GetImage = pScreen->GetImage;
//        pScreen->GetImage = GetImage;
        /* Wrap the current DestroyPixmap function */
        mali->DestroyPixmap = pScreen->DestroyPixmap;
        pScreen->DestroyPixmap = DestroyPixmap;

        mali->drm_fd = drm_fd;
        mali->OvlPg = ERROR_L;
        mali->debug = debug;
        mali->WaitForSync = WaitForSync;
		mali->HWLayerFor3D = HWLayerFor3D;
		mali->HWFullScrFor3D = HWFullScrFor3D;

        return;
    }

    ERRMSG("Rk30MaliDRI2_Init: DRI2ScreenInit failed!");
err2:
//    OvlFreeMemPg(pScrn, mali->PMemBuf);
err1:
    MFREE(mali);
err0:
	ump_close();
    drmClose(drm_fd);
}

void RkMaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    if (pMxv->RkMali != NULL) {
    	RkMaliPtr	mali = pMxv->RkMali;

    /* Unwrap functions */
    	pScreen->DestroyWindow    = mali->DestroyWindow;
//    pScreen->PostValidateTree = mali->PostValidateTree;
//    pScreen->GetImage         = mali->GetImage;
    	pScreen->DestroyPixmap    = mali->DestroyPixmap;

    	OvlFreeLay(mali->OvlPg);
    	OvlFreeLay(mali->OvlPgUI);

    	if (mali->ump_null_handle != UMP_INVALID_MEMORY_HANDLE)
    		ump_reference_release(mali->ump_null_handle);

    	DRI2CloseScreen(pScreen);
    	drmClose(mali->drm_fd);
    	MFREE(mali);
    	mali = NULL;
    	ump_close();
    }
}
