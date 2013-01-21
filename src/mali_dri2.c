/*
 * Portions based on "mali_dri2.c" from xf86-video-mali
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

#include "xorgVersion.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "mali_dri2.h"

typedef struct
{
    ump_handle   handle;
    size_t       size;
    void *       addr;
    int          depth;
    size_t       width;
    size_t       height;
} MaliDRI2BufferPrivateRec, *MaliDRI2BufferPrivatePtr;

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr                pScreen  = pDraw->pScreen;
    ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
    PixmapPtr                pPixmap  = NULL;
    DRI2Buffer2Ptr           buffer   = calloc(1, sizeof *buffer);
    MaliDRI2BufferPrivatePtr privates = calloc(1, sizeof *privates);
    ump_handle               handle;
    size_t                   size;

    if (pDraw->type == DRAWABLE_WINDOW) {
        pPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw);
    } else {
        ErrorF("Unexpected pDraw->type (%d) in MaliDRI2CreateBuffer\n", pDraw->type);
        return NULL;
    }

    /* initialize buffer info to default values */
    buffer->attachment    = attachment;
    buffer->driverPrivate = privates;
    buffer->format        = format;
    buffer->flags         = 0;
    buffer->cpp           = pPixmap->drawable.bitsPerPixel / 8;
    buffer->pitch         = PixmapBytePad(pDraw->width, pDraw->depth);

    /* allocate UMP buffer */
    size   = pDraw->height * buffer->pitch;
    handle = ump_ref_drv_allocate(size, UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR |
                                        UMP_REF_DRV_CONSTRAINT_USE_CACHE);
    if (handle == UMP_INVALID_MEMORY_HANDLE) {
        ErrorF("invalid UMP handle, bufsize=%d\n", (int)size);
    }

    privates->size   = size;
    privates->handle = handle;
    privates->addr   = ump_mapped_pointer_get(handle);
    privates->width  = pDraw->width;
    privates->height = pDraw->height;
    privates->depth  = pDraw->depth;

    buffer->name     = ump_secure_id_get(handle);
    buffer->flags    = 0; /* offset */

    ErrorF("MaliDRI2CreateBuffer attachment=%d %p, format=%d, cpp=%d, depth=%d\n",
           attachment, buffer, format, buffer->cpp, privates->depth);
    return buffer;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    MaliDRI2BufferPrivatePtr privates;
    ScreenPtr pScreen = pDraw->pScreen;

    ErrorF("Destroying attachment %d for drawable %p\n", buffer->attachment, pDraw);

    if (buffer != NULL) {
        privates = (MaliDRI2BufferPrivatePtr)buffer->driverPrivate;
        ump_mapped_pointer_release(privates->handle);
        ump_reference_release(privates->handle);
        free(privates);
        free(buffer);
    }
}

static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    GCPtr pGC;
    RegionPtr copyRegion;
    ScreenPtr pScreen = pDraw->pScreen;
    MaliDRI2BufferPrivatePtr privates;
    PixmapPtr pScratchPixmap;
    privates = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;

    if (privates->depth != pDraw->depth) {
        ErrorF("MaliDRI2CopyRegion: privates->depth != pDraw->depth (%d vs. %d)\n",
               privates->depth, pDraw->depth);
        return;
    }

    //ErrorF("MaliDRI2CopyRegion dstbuf=%p srcbuf=%p, depth=%d\n", pDstBuffer, pSrcBuffer, privates->depth);

    ump_cache_operations_control(UMP_CACHE_OP_START);
    ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_CPU);
    ump_cache_operations_control(UMP_CACHE_OP_FINISH);

    pGC = GetScratchGC(pDraw->depth, pScreen);
    pScratchPixmap = GetScratchPixmapHeader(pScreen, privates->width, privates->height,
                                            privates->depth, pSrcBuffer->cpp * 8,
                                            pSrcBuffer->pitch, privates->addr);
    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);
    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDraw, pGC);
    (*pGC->ops->CopyArea)(pScratchPixmap, pDraw, pGC, 0, 0,
                          pDraw->width, pDraw->height, 0, 0);
    FreeScratchPixmapHeader(pScratchPixmap);
    FreeScratchGC(pGC);

    ump_cache_operations_control(UMP_CACHE_OP_START);
    ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_MALI);
    ump_cache_operations_control(UMP_CACHE_OP_FINISH);
}

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen)
{
    int drm_fd;
    DRI2InfoRec info;

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return FALSE;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
        ErrorF("SunxiMaliDRI2_Init: drmOpen failed!\n");
        return FALSE;
    }

    if (ump_open() != UMP_OK) {
        drmClose(drm_fd);
        ErrorF("SunxiMaliDRI2_Init: ump_open() != UMP_OK\n");
        return FALSE;
    }

    info.version = 3;

    info.driverName = "sunxi-mali";
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
        SunxiMaliDRI2 *private = calloc(1, sizeof(SunxiMaliDRI2));
        private->drm_fd = drm_fd;
        return private;
    }
}

void SunxiMaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *private = SUNXI_MALI_DRI2(pScrn);
    if (private) {
        drmClose(private->drm_fd);
    }
    DRI2CloseScreen(pScreen);
}
