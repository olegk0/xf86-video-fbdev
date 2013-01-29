/*
 * For rk3066 with the modified kernel
 * Author: olegk0 <olegvedi@gmail.com>
 *
 * based on XFree86 4.x driver for S3 chipsets (Ani Joshi <ajoshi@unixbox.com>)
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

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <X11/extensions/Xv.h>
#include "fourcc.h"
#include "xf86.h"
#include "xf86xv.h"
#include "os.h"

#include "video.h"
#include "rk3066.h"
#include "fbdev_priv.h"
#include <sys/mman.h>

#define XVPORTS 1
#define PAGE_MASK    (getpagesize() - 1)

static XF86VideoEncodingRec DummyEncoding[1] = {
   {0, "XV_IMAGE", 1280, 720, {1, 1}}
};

static XF86VideoFormatRec Formats[] = {
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor}
};

XF86ImageRec Images[] = {
    XVIMAGE_YUY2,			//16bpp	422	packed
    XVIMAGE_UYVY,                      //16bpp MDP_YCRYCB_H2V1 
    XVIMAGE_I420,			//12bpp	Planar 420
    XVIMAGE_YV12,			//12bpp 
//    XVIMAGE_RGB565,
//    XVIMAGE_RGB888,
};

//-------------------------------------------------------------------
static void clear_buf(unsigned char *buf, CARD32 len)
{
    while(len>0){
	len--;
	buf[len] = 0;
    }
}

static void free_ovl_memory(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->overlay;
    int errno;

xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Try unmap \n");
    if (overlay->fbmem != NULL){
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "fbmem!=NULL \n");
        clear_buf(overlay->fbmem, overlay->fbmem_len);
	errno = munmap(overlay->fbmem, overlay->fbmem_len);
        if(errno == 0)
	    overlay->fbmem = NULL;
//xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Err fbmem unmap:%d \n", errno);
    }
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Unmaped \n");
}

static Bool alloc_ovl_memory(ScrnInfoPtr pScrn, unsigned int  size)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->overlay;
//    unsigned int len, start, offset;
    unsigned int yuv_phy[2];

    if (overlay == NULL)	return FALSE;
    if(overlay->fbmem != NULL)	return TRUE;
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Try mmap \n");
//    if(len < size)	return FALSE;
    overlay->fbmem_len = overlay->fix.smem_len;
    overlay->fbmio_len = (overlay->fbmem_len >> 1) & ~PAGE_MASK;

    overlay->fbmem = mmap( NULL, overlay->fbmem_len, PROT_READ | PROT_WRITE,
					     MAP_SHARED, overlay->ovl_fd, 0);
    if ( -1 == (long)overlay->fbmem ){
        xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Error mmap fb1_mem, fbmem_len:%X offset:0\n",overlay->fbmem_len);
        overlay->fbmem = NULL;
	return FALSE;
    }
//xf86DrvMsg( pScrn->scrnIndex, X_INFO, "mmaped fb1_mem\n");
    overlay->fbmio = overlay->fbmem + overlay->fbmio_len;
    clear_buf(overlay->fbmem, overlay->fbmem_len);
    yuv_phy[0] = overlay->fix.smem_start;  //four y
    yuv_phy[1] = overlay->fix.smem_start + overlay->fbmio_len;  //four uv
    ioctl(overlay->ovl_fd, FBIOSET_YUV_ADDR, &yuv_phy);

xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Mmaped \n");
    return TRUE;
}
//---------------------------------------------------------------------
static void XVDisplayVideoOverlay(ScrnInfoPtr pScrn, int mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
//    XVPortPrivPtr pPriv = pMxv->XVportPrivate;
    OvlHWPtr	overlay = pMxv->overlay;

  msync(overlay->fbmem,overlay->fbmem_len, mode);
}
//----------------------------------------------------
Bool XVInitStreams(ScrnInfoPtr pScrn, char FlScr, short drw_w, short drw_h, short width, short height, int id)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->overlay;
    int tmp;
    CARD32 tres, Nwidth;

xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Try setup overlay \n");
    if( 0 != ioctl(overlay->fb_fd, FBIOGET_VSCREENINFO, &overlay->var)) return FALSE;
    memcpy(&overlay->saved_var, &overlay->var, sizeof(struct fb_var_screeninfo));
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "overlay call aloc\n");
    if(!alloc_ovl_memory(pScrn, 1))	return FALSE;

    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	overlay->var.nonstd = YCrCb_NV12;
        break; 
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	overlay->var.nonstd = YCbCr_422_SP;
	break;
    default:
	overlay->var.nonstd = RGB_888;
    }
//YCrCb_444
//RGB_565
//RGBA_8888
    if(FlScr){// fullscreen
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "overlay fullscr\n");
	tres = (overlay->var.yres * width) / overlay->var.xres;
//	tres = (drw_h * width)/drw_w;
	if((tres <= overlay->var.yres)&&(tres >= height)){//[=]
	    overlay->var.xres = width;
	    overlay->var.yres = tres;
	    overlay->var.yres = (overlay->var.yres>>1)<<1;
	    overlay->offset = (((overlay->var.yres - height)>>1)& ~1)*overlay->var.xres_virtual;
	    overlay->flmmode = 'w';
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "overlay wide\n");
	}
	else{//[||]
	    tres = (overlay->var.xres * height) / overlay->var.yres;
	    Nwidth = ((drw_w * height) / drw_h + 1)& ~1;// from drw window
	    if(Nwidth > overlay->var.xres_virtual) Nwidth = overlay->var.xres_virtual;
	    tres = tres*width / Nwidth; // scale by x
	    overlay->var.yres = height;
	    overlay->var.xres = tres & ~1;
	    overlay->var.xres = (overlay->var.xres>>1)<<1;
	    if(overlay->var.xres > width)
		overlay->offset =  (overlay->var.xres - width) >> 1;
	    else
		overlay->offset =  0;
	    overlay->flmmode = 'n';
	}
	overlay->npixels = width;
	overlay->nlines = height;
    }
    else{
	if(drw_h > height)	overlay->nlines = height;//cliping
	else	overlay->nlines = drw_h;
	if(drw_w > width)	overlay->npixels = width;
	else	overlay->npixels = drw_w;
    }
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "overlay try set res - x:%d, y:%d ---- nlin:%d, npix:%d, offset:%d, mode:%d\n",overlay->var.xres,overlay->var.yres,overlay->nlines,overlay->npixels,overlay->offset,overlay->var.nonstd);
    if( 0 != ioctl(overlay->ovl_fd, FBIOPUT_VSCREENINFO, &overlay->var)) return FALSE;
    tmp = 0;
    ioctl(overlay->fb_fd, FBIOSET_OVERLAY_STATE, &tmp);
    tmp = 1;
    ioctl(overlay->ovl_fd, FBIOSET_ENABLE, &tmp);
    overlay->pixels = (overlay->var.xres_virtual + 10) * overlay->nlines;
// overlay->var.xres * overlay->var.yres;
    overlay->offset = (overlay->offset>>1)<<1;
    return TRUE;
}

void
XVCopyYUV12ToFb(const void *srcy, const void *srcv, const void *srcu, void *dst_Y,
                        void *dst_UV, int srcPitch, int dstPitch, int h, int w)
{
    CARD32 *Dst_UV, *Dst_Y, srcPitchUV;
    const CARD32 *Y;
    const CARD8 *U, *V;
    int i, j;

    srcPitchUV = srcPitch >> 1;
    w = (w & ~3) >> 2;
    h = h & ~1;
    for (j = 0; j < h; j++) {
        Y = srcy;
        Dst_Y = dst_Y;
        i = w;
        while (i > 0) {
            Dst_Y[0] = Y[0];
            Dst_Y++;
            Y++;
            i--;
        }
        dst_Y = (CARD8 *) dst_Y + dstPitch;
        srcy = (const CARD8 *) srcy + srcPitch;

        if(j & 1){
    	    i = w;
    	    V = srcv;
    	    U = srcu;
    	    Dst_UV = dst_UV;
    	    while (i > 0) {
        	Dst_UV[0] = U[0] | (V[0] << 8) | (U[1] << 16) | (V[1] << 24);
        	Dst_UV++;
        	V+=2;
        	U+=2;
        	i--;
    	    }
    	    dst_UV = (CARD8 *) dst_UV + dstPitch;
    	    srcu = (const CARD8 *) srcu + srcPitchUV;
    	    srcv = (const CARD8 *) srcv + srcPitchUV;
	}
    }
}

void
XVCopyPackedToFb(const void *src, void *dst_Y, void *dst_UV, CARD32 dst_offset,
	    int srcPitch, int dstPitch, int h, int w, unsigned char isYUY2)
{
    const CARD32 *Src;
    CARD32 *Dst_Y,*Dst_UV;
    const CARD8 *tmp;
    int i;

    srcPitch <<= 1;
    dst_Y = (CARD8 *) dst_Y + dst_offset;
    dst_UV = (CARD8 *) dst_UV + (dst_offset & ~1);
    w = (w & ~3) >> 2;
    while (h > 0) {
            Dst_Y = dst_Y;
            Dst_UV = dst_UV;
            Src = src;
            i = w;
	if(isYUY2)
            while (i > 0) {
		tmp = (CARD8 *) Src;
                Dst_Y[0] = tmp[0] | (tmp[2] << 8) | (tmp[4] << 16) | (tmp[6] << 24);
                Dst_UV[0] = tmp[1] | (tmp[3] << 8) | (tmp[5] << 16) | (tmp[7] << 24);
                Dst_UV++;
                Dst_Y++;
                Src+=2;
		i--;
            }
	else//UYVY
            while (i > 0) {
		tmp = (CARD8 *) Src;
                Dst_UV[0] = tmp[0] | (tmp[2] << 8) | (tmp[4] << 16) | (tmp[6] << 24);
                Dst_Y[0] = tmp[1] | (tmp[3] << 8) | (tmp[5] << 16) | (tmp[7] << 24);
                Dst_UV++;
                Dst_Y++;
                Src+=2;
		i--;
            }
        src = (const CARD8 *) src + srcPitch;
        dst_Y = (CARD8 *) dst_Y + dstPitch;
        dst_UV = (CARD8 *) dst_UV + dstPitch;
	h--;
    }
}

static int XVPutImage(ScrnInfoPtr pScrn,
          short src_x, short src_y, short drw_x, short drw_y,
          short src_w, short src_h, short drw_w, short drw_h,
          int image, char *buf, short width, short height,
          Bool sync, RegionPtr clipBoxes, pointer data )
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr pPriv = pMxv->XVportPrivate;
    CARD32 x1, x2, y1, y2;
    CARD32 dst_offset;
    CARD32 offset2=0, offset3=0, offset_c;
    CARD32 srcPitch, dstPitch;
//    CARD32 top, left, npixels, nlines;
    BoxRec dstBox;
    CARD32 tmp;
    OvlHWPtr	overlay = pMxv->overlay;
    char isYUY2 = 0, FlScr=0;
    
   /* Clip */
   x1 = src_x;
   x2 = src_x + src_w;
   y1 = src_y;
   y2 = src_y + src_h;

   dstBox.x1 = drw_x;
   dstBox.x2 = drw_x + drw_w;
   dstBox.y1 = drw_y;
   dstBox.y2 = drw_y + drw_h;

    if(!xf86XVClipVideoHelper(&dstBox, &x1, &x2, &y1, &y2, clipBoxes, width, height))
	return Success;
    if((clipBoxes->extents.x1 == 0)||(clipBoxes->extents.y1 == 0))
	FlScr = 1;
    if(pPriv->videoStatus == 0){
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "video params  drw_x=%d,drw_y=%d,drw_w=%d,drw_h=%d,src_x=%d,src_y=%d,src_w=%d,src_h=%d,width=%d,height=%d\n",drw_x,drw_y,drw_w,drw_h,src_x,src_y,src_w,src_h,width, height);
xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "video params clipBoxes x1=%d,x2=%d,y1=%d,y2=%d\n",clipBoxes->extents.x1,clipBoxes->extents.x2,clipBoxes->extents.y1,clipBoxes->extents.y2);
	if(!XVInitStreams(pScrn, FlScr, drw_w, drw_h, width, height, image))
	    return BadAlloc;
	pPriv->videoStatus = CLIENT_VIDEO_INIT;
    }
//    pitch = pScrn->bitsPerPixel * pScrn->displayWidth >> 3;
//   new_h = ((dstPitch * height) + pitch - 1) / pitch;
    dstPitch = overlay->var.xres_virtual;
    srcPitch = width;
   switch(image) {
   case FOURCC_YV12:
   case FOURCC_I420:
        offset2 = width * height;
        offset3 = ((width >> 1) * (height >> 1)) + offset2;
        break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
//	srcPitch = width << 1; //1px - 2 bytes
	break;
/*   default:
	dstPitch = overlay->var.xres_virtual;
        srcPitch = width;*/
   }
    /* copy data */
    if(!FlScr){ //not fullscreen
	offset_c = overlay->var.xres_virtual * (drw_y & ~1);
	overlay->offset = offset_c + drw_x;
    }
    switch(image) {
    case FOURCC_I420:
        tmp = offset2;
        offset2 = offset3;
        offset3 = tmp;
    case FOURCC_YV12:
/*        top &= ~1;    
        tmp = ((top >> 1) * srcPitch2) + (left >> 2);
        offset2 += tmp;
      offset3 += tmp;
*/
	if(FlScr){ // fullscreen
	    if(overlay->flmmode == 'n')//[||]
		offset_c = overlay->offset;
	    else//[=] w
		offset_c = overlay->offset >> 1;
	}
	else
	    offset_c = (offset_c >> 1)  + drw_x;
//	overlay->offset = overlay->offset;
	offset_c = offset_c & ~1;
        XVCopyYUV12ToFb(buf, buf + offset2, buf + offset3,
		 overlay->fbmem + overlay->offset, overlay->fbmio + offset_c,
		 srcPitch, dstPitch, overlay->nlines, overlay->npixels);
        break; 
    case FOURCC_YUY2:
	isYUY2 = 1;
    case FOURCC_UYVY:
//        buf += (top * srcPitch) + left;
//	if(overlay->fbmem_len < (overlay->pixels << 1))
//overlay->offset = 0;
	overlay->offset = overlay->offset & ~1;
	    XVCopyPackedToFb(buf, overlay->fbmem, overlay->fbmio, overlay->offset, srcPitch, dstPitch,
		     overlay->nlines, overlay->npixels, isYUY2);
        break;
//    default:
    }
    /* update cliplist */
       if(!REGION_EQUAL(pScrn->pScreen, &pPriv->clip, clipBoxes)) {
            REGION_COPY(pScrn->pScreen, &pPriv->clip, clipBoxes);
            /* draw these */
        xf86XVFillKeyHelper(pScrn->pScreen, pPriv->colorKey, clipBoxes);
        }
    XVDisplayVideoOverlay(pScrn, MS_ASYNC);
    pPriv->videoStatus = CLIENT_VIDEO_ON;

    return Success;
}       


static void XVStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr pPriv = pMxv->XVportPrivate;
    OvlHWPtr	overlay = pMxv->overlay;
    int tmp;

    REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
//xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Stop video \n");
    clear_buf(overlay->fbmem, overlay->fbmem_len);
    pPriv->videoStatus = 0;

    if (exit) {
//xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Exit video \n");
	free_ovl_memory(pScrn);
	ioctl(overlay->ovl_fd, FBIOPUT_VSCREENINFO, &overlay->saved_var);
//    tmp = 1;
//    ioctl(overlay->fb_fd, FBIOSET_ENABLE, &tmp);
	tmp = 0;
	ioctl(overlay->ovl_fd, FBIOSET_ENABLE, &tmp);
//	pPriv->videoStatus = 0;
    }
}


static int XVQueryImageAttributes(ScrnInfoPtr pScrn, int id,
		  unsigned short *w, unsigned short *h,
		  int *pitches, int *offsets)
{
    int size, tmp;

    *w = (*w + 1) & ~1;
    if(offsets) offsets[0] = 0;
    
    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        *h = (*h + 1) & ~1;
        size = (*w + 3) & ~3;
        if(pitches) pitches[0] = size;
        size *= *h;
        if(offsets) offsets[1] = size;
        tmp = ((*w >> 1) + 3) & ~3;
        if(pitches) pitches[1] = pitches[2] = tmp;
        tmp *= (*h >> 1);
        size += tmp;
        if(offsets) offsets[2] = size;
        size += tmp;
        break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
        size = *w << 1;
        if(pitches) pitches[0] = size;
        size *= *h;
        break;
    } 
        
    return size;
}

static void
XVQueryBestSize(ScrnInfoPtr pScrn, Bool motion,
		 short vidW, short vidH, short drawW, short drawH,
		 unsigned int *retW, unsigned int *retH, pointer data)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->overlay;

    if(drawW > overlay->saved_var.xres)
	*retW = overlay->saved_var.xres;
    else
	*retW = drawW;

    if(drawH > overlay->saved_var.yres)
	*retH = overlay->saved_var.yres;
    else
	*retH = drawW;
}

static int
XVSetPortAttribute(ScrnInfoPtr pScrni,
		    Atom attribute, INT32 value, pointer data)
{
   return BadMatch;
}

static int
XVGetPortAttribute(ScrnInfoPtr pScrni,
		    Atom attribute, INT32 * value, pointer data)
{
  return BadMatch;
}

static XF86VideoAdaptorPtr XVAllocAdaptor(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XF86VideoAdaptorPtr adapt = NULL;
    XVPortPrivPtr pPriv = NULL;
    int i;

	if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
    	    return NULL;

	if(!(pPriv = calloc(1, sizeof(XVPortPrivRec)+(sizeof(DevUnion) * XVPORTS))))
	{
	    free(adapt);//xfree
	    adapt = NULL;
	    pPriv = NULL;
    	    return NULL;
	}
	adapt->pPortPrivates = (DevUnion*)(&pPriv[1]);

	for(i = 0; i < XVPORTS; i++)
    	    adapt->pPortPrivates[i].val = i;

    pPriv->colorKey = 0;
    pPriv->videoStatus = 0;
    pPriv->lastPort = -1;

//    pMxv->adaptor = adapt;
    pMxv->XVportPrivate = pPriv;

    return adapt;
}

static XF86VideoAdaptorPtr
XVInitAdaptor(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XF86VideoAdaptorPtr adapt;

    adapt = XVAllocAdaptor(pScrn);
    if(adapt != NULL){
	adapt->type = XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;

	adapt->name = "RK30";
	adapt->nEncodings = 1;
        adapt->pEncodings = &DummyEncoding[0];
	adapt->nFormats = ARRAY_SIZE(Formats);
	adapt->pFormats = Formats;
	adapt->nPorts = XVPORTS;// nums ports
	adapt->nAttributes = 0;
	adapt->pAttributes = NULL;
	adapt->nImages = ARRAY_SIZE(Images);
	adapt->pImages = Images;

	adapt->PutVideo = NULL;
	adapt->PutStill = NULL;
        adapt->GetVideo = NULL;
	adapt->GetStill = NULL;

	adapt->StopVideo = XVStopVideo;
/* Empty Attrib functions - required anyway */
	adapt->SetPortAttribute = XVSetPortAttribute;
	adapt->GetPortAttribute = XVGetPortAttribute;

	adapt->QueryBestSize = XVQueryBestSize;
	adapt->QueryImageAttributes = XVQueryImageAttributes;
	adapt->PutImage = XVPutImage;
    }
    REGION_NULL(pScreen, &(pMxv->XVportPrivate->clip));

    return adapt;
}

Bool init_ovl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr		overlay = NULL;
    int tmp;

    if(!(overlay = calloc(1, sizeof(OvlHWRec) )))
	return FALSE;

    overlay->fbmem = NULL;
    overlay->fbmio = NULL;

    overlay->ovl_fd = open("/dev/fb1", O_RDWR);
    if (overlay->ovl_fd < 0) goto err;

    overlay->fb_fd = open("/dev/fb0", O_RDWR);
    if (overlay->ovl_fd < 0) goto err1;

    if( 0 == ioctl(overlay->ovl_fd, FBIOGET_FSCREENINFO, &overlay->fix))
    if(overlay->fix.smem_start != 0)
    if(0 == ioctl(overlay->fb_fd, FBIOGET_VSCREENINFO, &overlay->var)){
	overlay->var.activate = 0;
	memcpy(&overlay->saved_var, &overlay->var, sizeof(struct fb_var_screeninfo));
	ioctl(overlay->ovl_fd, FBIOPUT_VSCREENINFO, &overlay->var);
	ioctl(overlay->ovl_fd, FBIOBLANK, FB_BLANK_UNBLANK);
	tmp=0;
	ioctl(overlay->fb_fd, FBIOSET_OVERLAY_STATE, &tmp);
	tmp=0;
	ioctl(overlay->ovl_fd, FBIOSET_ENABLE, &tmp);
        pMxv->overlay = overlay;
	return TRUE;
    }
    close(overlay->fb_fd);
err1:
    close(overlay->ovl_fd);
err:
    free(overlay);
    return FALSE;
}

void
InitXVideo(ScreenPtr pScreen)
{
   ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(!init_ovl(pScreen)){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Error init ovl\n");
	return;
    }
    newAdaptor = XVInitAdaptor(pScreen);

    if (newAdaptor == NULL){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Error init adapter\n");
	CloseXVideo(pScreen);
	return;
    }
    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
	if(!num_adaptors) {
            num_adaptors = 1;
            adaptors = &newAdaptor;
	} else {
            newAdaptors = malloc((num_adaptors + 1) * sizeof(XF86VideoAdaptorPtr*));//xalloc
            if(newAdaptors) {
		memcpy(newAdaptors, adaptors, num_adaptors * sizeof(XF86VideoAdaptorPtr));
		newAdaptors[num_adaptors] = newAdaptor;
            	adaptors = newAdaptors;
            	num_adaptors++;
	    } 
    	}
    }
    if(num_adaptors){
        if(!xf86XVScreenInit(pScreen, adaptors, num_adaptors)){
	    xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Fail initing screen\n");
	    CloseXVideo(pScreen);
	}
	else
	    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "XV:Init complete\n");
    }
    if(newAdaptors)
	free(newAdaptors);//xfree
}

void
CloseXVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->overlay;

    if(overlay != NULL){
	free_ovl_memory(pScrn);
        close(overlay->ovl_fd);
	close(overlay->fb_fd);
        free(overlay);
	overlay = NULL;
    }
/*    if(pMxv->adaptor != NULL){
	 free(pMxv->adaptor);
	pMxv->adaptor = NULL;
    }*/
    if(pMxv->XVportPrivate != NULL){
	free(pMxv->XVportPrivate);
	pMxv->XVportPrivate = NULL;
    }
xf86DrvMsg( pScrn->scrnIndex, X_INFO, "XV:Closed\n");
}