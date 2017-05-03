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

    // create the UMP buffer 
    umpbuf = calloc(1, sizeof(UMPBufferInfoRec));
    if (!umpbuf) {
        ErrorF("MigratePixmapToUMP: calloc failed\n");
        return NULL;
    }
    umpbuf->refcount = 1;
    umpbuf->pPixmap = pPixmap;
    umpbuf->MemBuf = OvlAllocMemPg(size, 0);
    if (!umpbuf->MemBuf) {
        ErrorF("MigratePixmapToUMP: OvlAllocMemPg failed\n");
        MFREE(umpbuf);
        return NULL;
    }
//    umpbuf->size = size;
    umpbuf->MapMemBuf = OvlMapBufMem(umpbuf->MemBuf);
    umpbuf->depth = pPixmap->drawable.depth;
    umpbuf->width = pPixmap->drawable.width;
    umpbuf->height = pPixmap->drawable.height;

    // copy the pixel data to the new location 
    if (pitch == pPixmap->devKind) {
        memcpy(umpbuf->MapMemBuf, pPixmap->devPrivate.ptr, size);
    } else {
        int y;
        for (y = 0; y < umpbuf->height; y++) {
            memcpy(umpbuf->MapMemBuf + y * pitch, 
                   pPixmap->devPrivate.ptr + y * pPixmap->devKind,
                   pPixmap->devKind);
        }
    }

    umpbuf->BackupDevKind = pPixmap->devKind;
    umpbuf->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

    pPixmap->devKind = pitch;
    pPixmap->devPrivate.ptr = umpbuf->MapMemBuf;

    HASH_ADD_PTR(mali->HashPixmapToUMP, pPixmap, umpbuf);

    DebugMsg("MigratePixmapToUMP %p, new buf = %p\n", pPixmap, umpbuf);
    return umpbuf;
}

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr		pScreen = pDraw->pScreen;
    ScrnInfoPtr		pScrn = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr	buffer;
    UMPBufferInfoPtr	privates;
    uint32_t		handle;
    FBDevPtr		pMxv = FBDEVPTR(pScrn);
    RkMaliPtr		mali = pMxv->RkMali;
    Bool		can_use_overlay = TRUE;
    PixmapPtr		pWindowPixmap;

    DebugMsg("DRI2CreateBuffer\n");

    if (!(buffer = calloc(1, sizeof *buffer))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        return NULL;
    }

    if (pDraw->type == DRAWABLE_WINDOW &&
        (pWindowPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw)))
    {
/*        DebugMsg("win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d, screen_x=%d, screen_y=%d)\n",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,
                 pWindowPixmap->screen_x, pWindowPixmap->screen_y);
*/
        DebugMsg("win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d) attachment:%d\n",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,attachment);
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
        buffer->name = OvlGetSidByMemPg(privates->MemBuf);

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

    if (pDraw->type != DRAWABLE_WINDOW || attachment != DRI2BufferBackLeft) {
	buffer->name   = 0;
	DebugMsg("DRI2CreateBuffer pix=%p, buf=%p attachment:%d\n", pDraw, buffer, attachment);
	return buffer;
    }

    if (!(privates = calloc(1, sizeof *privates))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        MFREE(buffer);
        return NULL;
    }
    buffer->driverPrivate = privates;
//    privates->size     = pDraw->height * buffer->pitch;
    privates->width    = pDraw->width;
    privates->height   = pDraw->height;
    privates->depth    = pDraw->depth;
    privates->refcount = 1;

    /* Overlay is already used by a different window */
    if (mali->pOverlayWin && mali->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32 && pDraw->bitsPerPixel != 16)
        can_use_overlay = FALSE;


    DebugMsg("can_use_overlay: %d\n", can_use_overlay);

    if (!can_use_overlay) {
        free(buffer);
        return NULL;
    }

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32 && pDraw->bitsPerPixel != 16)
        can_use_overlay = FALSE;

    if(pDraw->width < 2 || pDraw->height < 2){
	can_use_overlay = FALSE;
    }

    if (can_use_overlay){
	if(mali->OvlPg == ERROR_L){
	    OvlLayoutType ltype;
	    if(mali->HWFullScrFor3D){
		ltype = SCALE_L;
		if(mali->OvlPgUI == ERROR_L){
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
	    }
	    else
		ltype = SCALE_L;//ANY_HW_L;

	    mali->OvlPg = OvlAllocLay(ltype, ALC_NONE_FB);
	    if(mali->OvlPg == ERROR_L){
		ERRMSG("Cannot alloc overlay\n");
		can_use_overlay = FALSE;
	    }else{//init
		DebugMsg("MaliDRI2CreateBuffer: Alloc ovl:%d\n",mali->OvlPg);
		mali->colorKey = HWAclSetColorKey(pScrn,mali->OvlPg);
//    		OvlSetupFb(mali->OvlPg, RKL_FORMAT_DEFAULT, pDraw->width, pDraw->height);
//    		OvlEnable(mali->OvlPg, 1, 0);
		mali->EnFl = 0;
		mali->mali_refs = 0;
	    }
	}
    }

    privates->bOverlayWinEnabled = can_use_overlay;
    DebugMsg("can_use_overlay:%d\n",can_use_overlay);
    OvlMemPgPtr	MemBuf;

    if (can_use_overlay)
        buffer->pitch = buffer->cpp * OvlVresByXres(privates->width);


    MemBuf = OvlAllocMemPg(pDraw->height*buffer->pitch, 0);
    if(!MemBuf){
	ErrorF("Failed alloc_gem\n");
	goto err;
    }

    void *MapMemBuf = OvlMapBufMem(MemBuf);


    privates->bOverlayWinEnabled = can_use_overlay;

    if(can_use_overlay)
    {
	mali->pOverlayWin = (void *)pDraw;

	privates->MemBuf = MemBuf;
	privates->MapMemBuf = MapMemBuf;
//get next page
//	MemBuf = mali->MemBuf[mali->buf_pnt];
	buffer->name = OvlGetSidByMemPg(MemBuf);

        DebugMsg("MemBuf :%p Paddr:%X\n", privates->MemBuf, OvlGetPhyAddrMemPg(privates->MemBuf));

	mali->scr_w = OvlGetXresByLay(UI_L);
	mali->scr_h = OvlGetYresByLay(UI_L);

    }

	mali->mali_refs++;

end:
    DebugMsg("DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, name=%d:%d, pitch=%d, w=%d, h=%d, cpp=%d, depth=%d adr=%p refs=%d\n",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,buffer->pitch,
             privates->width, privates->height, buffer->cpp, privates->depth,OvlGetPhyAddrMemPg(MemBuf),
             mali->mali_refs);

    return buffer;
err:
    free(privates);
    free(buffer);
    return NULL;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    UMPBufferInfoPtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;

    DebugMsg("DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d refs=%d name:%d\n",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment, mali->mali_refs,buffer->name);

    if (buffer != NULL) {
	if(buffer->driverPrivate != NULL){
	    privates = (UMPBufferInfoPtr)buffer->driverPrivate;
	    if(!privates->pPixmap) { // win 
/*		if(privates->drm_mem->handle) {
		    ump_mapped_pointer_release(privates->handle);
		    ump_reference_release(privates->handle);
		}
*/
	    if(privates->MemBuf)
		mali->mali_refs--;
	    }

	    if(--privates->refcount <= 0) {
		    if(privates->MemBuf) {
			OvlFreeMemPg(privates->MemBuf);
			privates->MemBuf =0;
			DebugMsg("free(MemBuf)\n");
		    }
		DebugMsg("free(privates)\n");
		free(privates);
	    }
	}
	free(buffer);
    }

    DebugMsg("refs=%d***\n",mali->mali_refs);
}
/*
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

	pGC = GetScratchGC(pDraw->depth, pScreen);

    pScratchPixmap = GetScratchPixmapHeader(pScreen,
    		privates->width, privates->height,
            privates->depth, pSrcBuffer->cpp * 8,
            pSrcBuffer->pitch,
            privates->MapMemBuf);
//            privates->addr + pSrcBuffer->flags);

    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);

    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDraw, pGC);
    (*pGC->ops->CopyArea)((DrawablePtr)pScratchPixmap, pDraw, pGC, 0, 0,
    pDraw->width, pDraw->height, 0, 0);

    FreeScratchPixmapHeader(pScratchPixmap);
	FreeScratchGC(pGC);


}
*/
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

//return;
    UMPBufferInfoPtr bufpriv = (UMPBufferInfoPtr)pSrcBuffer->driverPrivate;

DebugMsg("bufpriv:%p, OverlayWinEnabled:%d, MemBuf:%p name:%d\n"
    ,bufpriv, bufpriv->bOverlayWinEnabled, bufpriv->MemBuf, pSrcBuffer->name);

//    if(!bufpriv || !bufpriv->bOverlayWinEnabled || !bufpriv->MemBuf)
//	return;

//DebugMsg("Expose we:%d  hndl:%d\n",mali->bOverlayWinEnabled ,bufpriv->handle);

//    if (!bufpriv->bOverlayWinEnabled && bufpriv->handle != UMP_INVALID_MEMORY_HANDLE)
/*
     {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, pDstBuffer, pSrcBuffer);
        return;
    }
*/
    if(bufpriv->ovl_h != pDraw->height || bufpriv->ovl_w != pDraw->width || mali->OvlNeedUpdate){
	if(mali->OvlNeedUpdate)
	    mali->OvlNeedUpdate--;
    	bufpriv->ovl_h = pDraw->height;
    	bufpriv->ovl_w = pDraw->width;
    	ret = OvlSetupFb(mali->OvlPg, RKL_FORMAT_DEFAULT, pDraw->width, pDraw->height);
        DebugMsg("Change size to w:%d,h:%d ret:%d\n", pDraw->width,pDraw->height, ret);
//        mali->colorKey = HWAclSetColorKey(pScrn);
        Changed = TRUE;
    }

    if(bufpriv->ovl_x != pDraw->x || bufpriv->ovl_y != pDraw->y || Changed){
    	bufpriv->ovl_x = pDraw->x;
    	bufpriv->ovl_y = pDraw->y;
    	if(mali->HWFullScrFor3D)
    		ret = OvlSetupDrw(mali->OvlPg, 0, 0, mali->scr_w, mali->scr_h);
    	else
    		ret = OvlSetupDrw(mali->OvlPg, bufpriv->ovl_x, bufpriv->ovl_y, pDraw->width, pDraw->height);

        DebugMsg("Change pos to x:%d,y:%d ret:%d\n", pDraw->x,pDraw->y, ret);
        Changed = TRUE;
    }

    if(Changed){
    	HWAclFillKeyHelper(pDraw, mali->colorKey, pRegion, FALSE);
    }

    if(mali->OvlNeedUpdate)
        OvlClrMemPg(bufpriv->MemBuf);

    OvlLayerLinkMemPg(mali->OvlPg, bufpriv->MemBuf);

    if(!mali->EnFl){
	mali->EnFl = 1;
	OvlEnable(mali->OvlPg, 1, 0);
    }

    if(mali->WaitForSync)
    	OvlWaitVSync(mali->OvlPg);

//    DebugMsg("OvlLayerLinkMemPg");

}

/************************************************************************/

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RkMaliPtr mali = FBDEVPTR(pScrn)->RkMali;
    Bool ret;
    int i;


    if (pWin == mali->pOverlayWin){
	mali->pOverlayWin = NULL;
	mali->OvlNeedUpdate = 3;
    }

    if(mali->mali_refs <= 0) {
	mali->mali_refs = -1;

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

	OvlFreeMemPg(umpbuf->MemBuf);
	umpbuf->MemBuf = 0;

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

//********************************************

void RkMaliDRI2_Init(ScreenPtr pScreen, Bool debug, Bool WaitForSync, Bool HWFullScrFor3D)
{
    int drm_fd,i;
    DRI2InfoRec info;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    Bool isOverlay = TRUE;

    pMxv->RkMali = NULL;
    if(pMxv->HWAcl == NULL){
    	ERRMSG("Rk30MaliDRI2_Init: Overlay not found!");
//        return;
        isOverlay = FALSE;
    }

    HWAclPtr hwacl = pMxv->HWAcl;

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return;

//    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
    if ((drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC)) < 0) {
    	ERRMSG("Rk30MaliDRI2_Init: drmOpen failed!");
        return;
    }

    if(!(pMxv->RkMali = calloc(1, sizeof(RkMaliRec) ))){
    	ERRMSG("Rk30MaliDRI2_Init: Mem alloc failed!");
        goto err0;
    }

    RkMaliPtr	mali = pMxv->RkMali;

	/* Try to allocate small dummy UMP buffers to secure id 1 and 2 */
/*
    mali->null_secure_id = 0;
   	mali->ump_null_handle = ump_ref_drv_allocate(4096, UMP_REF_DRV_CONSTRAINT_NONE);
    if (mali->ump_null_handle != UMP_INVALID_MEMORY_HANDLE)
    	mali->ump_null_secure_id = ump_secure_id_get(mali->ump_null_handle);

    if (mali->ump_null_secure_id == UMP_INVALID_SECURE_ID) {
    	INFMSG("GET_UMP_SECURE_ID failed");
        goto err1;
    }
*/
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
        mali->debug = debug;
        mali->WaitForSync = WaitForSync;
	mali->HWFullScrFor3D = HWFullScrFor3D;
	mali->mali_refs =-1;
	mali->OvlPg = ERROR_L;
	mali->OvlPgUI = ERROR_L;
	mali->OvlNeedUpdate = 3;

        return;
    }

    ERRMSG("Rk30MaliDRI2_Init: DRI2ScreenInit failed!");
err2:
//    OvlFreeMemPg(pScrn, mali->PMemBuf);
err1:
    MFREE(mali);
err0:
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
//	pScreen->PostValidateTree = mali->PostValidateTree;
//    pScreen->GetImage         = mali->GetImage;
    	pScreen->DestroyPixmap    = mali->DestroyPixmap;


    	DRI2CloseScreen(pScreen);
    	drmClose(mali->drm_fd);
    	MFREE(mali);
    	mali = NULL;
    }
}
