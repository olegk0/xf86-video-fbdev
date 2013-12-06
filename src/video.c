/*

 *  For rk3066 with the modified kernel
 *  Author: olegk0 <olegvedi@gmail.com>
 *
 *  based on XFree86 4.x driver for S3 chipsets (Ani Joshi <ajoshi@unixbox.com>)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include "layer.h"
#include "rk3066.h"
#include "fbdev_priv.h"
#include <sys/mman.h>

#ifdef DEBUG
#define XVDBG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, format, ## args)
#else
#define XVDBG(format, args...)
#endif

#define XVPORTS 1
#define PAGE_MASK    (getpagesize() - 1)

static XF86VideoEncodingRec DummyEncoding[1] = {
   {0, "XV_IMAGE", 1920, 1080, {1, 1}}
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
static Bool XVInitStreams(ScrnInfoPtr pScrn, Bool FlScr, short drw_x, short drw_y, short drw_w,
						short drw_h, short width, short height, int id)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    XVPortPrivPtr XVport = pMxv->XVport;
    CARD32 tres, Nwidth, mode, xres, yres;

    XVDBG("Try setup overlay \n");

    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	XVport->RGA_mode = RK_FORMAT_YCbCr_420_P;
	XVport->IPP_mode = IPP_Y_CBCR_H2V2;//3
        break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	XVport->RGA_mode = RK_FORMAT_YCrCb_422_SP;
	XVport->IPP_mode = IPP_Y_CBCR_H2V1;//2
	break;
    default:
//	mode = RGBX_8888;
	XVport->RGA_mode = RK_FORMAT_RGBX_8888;
	XVport->IPP_mode = IPP_XRGB_8888;
    }

    if(FlScr){// fullscreen
	XVDBG("overlay fullscr\n");
	tres = (overlay->saved_var.yres * width) / overlay->saved_var.xres;
//	tres = (drw_h * width)/drw_w;
	if((tres <= overlay->saved_var.yres)&&(tres >= height)){//[=]
	    XVDBG("overlay wide\n");
	    xres = width;
	    yres = (tres-0)& ~1;
	    XVport->x_drw = 0;
	    XVport->y_drw = (drw_y * yres) / overlay->saved_var.yres;
	}
	else{//[||]
	    tres = (overlay->saved_var.xres * height) / overlay->saved_var.yres;
	    Nwidth = ((drw_w * height) / drw_h)& ~1;// from drw window
	    if(Nwidth > overlay->saved_var.xres_virtual) Nwidth = overlay->saved_var.xres_virtual;
	    tres = tres * width / Nwidth; // scale by x
	    yres = height;
	    xres = (tres-0) & ~1;
	    XVport->x_drw = (drw_x * xres) / overlay->saved_var.xres;
	    XVport->y_drw = 0;
	}
	mode = RGB_888;
    }
    else{
	xres = overlay->saved_var.xres;
	yres = overlay->saved_var.yres;
	mode = RGBX_8888;
    }
//    OvlEnable(pScrn, 0);
    OvlClearBuf(pScrn, 0);
    OvlFlushPg(pScrn, 0, MS_SYNC);
//    XVDBG("IPP_mode %d\n",XVport->IPP_mode);
    XVDBG("overlay try set res - x:%d, y:%d ---- width:%d, heigth:%d, mode:%d\n",xres,yres,width,height,mode);
    if( OvlSetMode(pScrn, xres, yres, mode)) return FALSE;
//    OvlSwDisp(pScrn, 1, TRUE);
//    OvlClearBuf(pScrn, 1);
    usleep(500);
    OvlEnable(pScrn, 1);
//    initRGAreq(pScrn, overlay->var.xres_virtual, yres, inRGAmode, outRGAmode);
//    initIPPreq(pScrn, width, width, height, IPPmode);

    return TRUE;
}

//-----------------------------------------------------------------
int VFillReg(ScrnInfoPtr pScrn, RegionPtr clipBoxes, /*int numbuf*/CARD32 yrgb_addr, CARD32 color)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport = pMxv->XVport;
    OvlHWPtr overlay = pMxv->OvlHW;

    unsigned int x,y,dx,dy,dv,x1,y1;
    CARD32 *buf;

//    if(clipBoxes->extents.x1 > 2) x1 = clipBoxes->extents.x1-2;
//    else x1 = 0;
//    if(clipBoxes->extents.y1 > 2) y1 = clipBoxes->extents.y1-2;
//    else y1 = 0;

/*    buf = (CARD32 *)overlay->fb_mem[numbuf];
    dv = clipBoxes->extents.x1 + clipBoxes->extents.y1 * overlay->var.xres_virtual;
    buf = (CARD32 *) buf + dv;
    dx = clipBoxes->extents.x2 - clipBoxes->extents.x1;// + 4;
    dy = clipBoxes->extents.y2 - clipBoxes->extents.y1;// + 4;
    for(y = 0;y <= dy;y++){
        for(x = 0;x <= dx;x++)
	    buf[x] = color;
	buf = (CARD32 *) buf + overlay->var.xres_virtual;
    }
    OvlFlushPg(pScrn, numbuf, MS_ASYNC);*/
    return 0;

}
//---------------------------------------------------
static void
XVCopyPackedToFb(const void *src, void *dst_Y, void *dst_UV,/* CARD32 dst_offset,
	    int srcPitch,*/ int dstPitch, int h, int w, unsigned char isYUY2)
{
    const CARD32 *Src;
    CARD16 *Dst_Y,*Dst_UV;
    const CARD8 *tmp;
    int i, srcPitch;

    srcPitch = w << 1;
//    dst_Y = (CARD8 *) dst_Y + dst_offset;
//    dst_UV = (CARD8 *) dst_UV + (dst_offset & ~1);
//    w = (w & ~3) >> 2;
    w = w >> 1;
    while (h > 0) {
            Dst_Y = dst_Y;
            Dst_UV = dst_UV;
            Src = src;
            i = w;
	if(isYUY2)
            while (i > 0) {
		tmp = (CARD8 *) Src;
                Dst_Y[0] = tmp[0] | (tmp[2] << 8);// | (tmp[4] << 16) | (tmp[6] << 24);
                Dst_UV[0] = tmp[1] | (tmp[3] << 8);// | (tmp[5] << 16) | (tmp[7] << 24);
                Dst_UV++;
                Dst_Y++;
                Src++;
		i--;
            }
	else//UYVY
            while (i > 0) {
		tmp = (CARD8 *) Src;
                Dst_UV[0] = tmp[0] | (tmp[2] << 8);// | (tmp[4] << 16) | (tmp[6] << 24);
                Dst_Y[0] = tmp[1] | (tmp[3] << 8);// | (tmp[5] << 16) | (tmp[7] << 24);
                Dst_UV++;
                Dst_Y++;
                Src++;
		i--;
            }
        src = (const CARD8 *) src + srcPitch;
        dst_Y = (CARD8 *) dst_Y + dstPitch;
        dst_UV = (CARD8 *) dst_UV + dstPitch;
	h--;
    }
}
//-----------------------------------------------------------------
static int XVPutImage(ScrnInfoPtr pScrn,
          short src_x, short src_y, short drw_x, short drw_y,
          short src_w, short src_h, short drw_w, short drw_h,
          int image, char *buf, short width, short height,
          Bool sync, RegionPtr clipBoxes, pointer data )
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport= pMxv->XVport;
    OvlHWPtr overlay = pMxv->OvlHW;
    CARD32 x1, x2, y1, y2;
    CARD32 drw_offset,offset, offset2=0, offset3=0;
    BoxRec dstBox;
    CARD32 tmp, dstPitch;
    Bool isYUY2 = FALSE, FlScr = FALSE, ClipEq = FALSE;
    
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
	FlScr = TRUE;
    if(FlScr != XVport->FlScr){
	XVport->videoStatus = 0;
	XVport->FlScr = FlScr;
    }
    if(REGION_EQUAL(pScrn->pScreen, &XVport->clip, clipBoxes))
//    if(XVport->clip.extents.x1 == clipBoxes->extents.x1 && XVport->clip.extents.x2 == clipBoxes->extents.x2 &&
//	XVport->clip.extents.y1 == clipBoxes->extents.y1 && XVport->clip.extents.y2 == clipBoxes->extents.y2)
	ClipEq = TRUE;
//    else
    if((XVport->w_src != src_w) || (XVport->h_src != src_h)){
	XVport->videoStatus = 0;
	XVport->w_src = src_w;
	XVport->h_src = src_h;
    }
//	if(XVport->videoStatus == CLIENT_VIDEO_CH)
//	    XVport->videoStatus = 0;

//    if(XVport->videoStatus == 0){
    if(XVport->videoStatus < CLIENT_VIDEO_INIT){
	XVDBG("video params  drw_x=%d,drw_y=%d,drw_w=%d,drw_h=%d,src_x=%d,src_y=%d,src_w=%d,src_h=%d,width=%d,height=%d, image_id=%X\n",drw_x,drw_y,drw_w,drw_h,src_x,src_y,src_w,src_h,width, height, image);
	XVDBG("video params clipBoxes x1=%d,x2=%d,y1=%d,y2=%d\n",clipBoxes->extents.x1,clipBoxes->extents.x2,clipBoxes->extents.y1,clipBoxes->extents.y2);
	if(!XVInitStreams(pScrn, FlScr, drw_x, drw_y, drw_w, drw_h, src_w, src_h, image))
	    return BadAlloc;
	XVport->videoStatus = CLIENT_VIDEO_INIT;
        REGION_COPY(pScrn->pScreen, &XVport->clip, clipBoxes);
    }

    drw_h = drw_h & ~7;
    drw_w = drw_w & ~7;

//	offset = width * height;
	offset = src_w * src_h;
	if(!FlScr){ //not fullscreen
	    drw_offset = drw_w * drw_h;
//    	    offset2 = ((drw_offset)+PAGE_MASK)& ~PAGE_MASK;
    	    offset2 = drw_offset;
    	    offset3 = (((drw_offset >> 2) + offset2));
	    drw_offset = offset2;
	}
	else{
    	    offset2 = offset & ~1;
    	    offset3 = ((offset2 >> 2) + offset2) & ~1;
	}

/*    switch(image) {
    case FOURCC_YV12:
    case FOURCC_I420:
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	break;
    }
*/
    switch(image) {
    case FOURCC_I420:
        tmp = offset2;
        offset2 = offset3;
        offset3 = tmp;
    case FOURCC_YV12:

	memcpy(overlay->fb_mem[3],buf,offset+(offset>>1));
	OvlFlushPg(pScrn, 3, MS_SYNC);
	if(FlScr){ // fullscreen
	    OvlSync(pScrn);
	    OvlChBufFrmt(pScrn, overlay->phadr_mem[3], overlay->phadr_mem[3]+offset2, overlay->phadr_mem[3]+offset3,
				XVport->RGA_mode, overlay->RGA_mode, overlay->phadr_mem[0],
				src_w, src_h, XVport->x_drw, XVport->y_drw, src_w, overlay->var.xres_virtual);
	}
	else
	{
	    OvlScaleBuf(pScrn, overlay->phadr_mem[3], overlay->phadr_mem[3]+offset, XVport->IPP_mode,
		overlay->phadr_mem[2],overlay->phadr_mem[2]+drw_offset, src_w, src_h, drw_w, drw_h, src_w, drw_w);
//	    OvlSync(pScrn);
	    OvlChBufFrmt(pScrn, overlay->phadr_mem[2], overlay->phadr_mem[2]+offset2, overlay->phadr_mem[2]+offset3,
				XVport->RGA_mode, overlay->RGA_mode, overlay->phadr_mem[0],
				drw_w, drw_h, drw_x, drw_y, drw_w, overlay->var.xres_virtual);
	}
        break; 
    case FOURCC_YUY2:
	isYUY2 = TRUE;
    case FOURCC_UYVY:
//	offset = 1920*1080*2;
	dstPitch = (src_w + 7)& ~7;
	offset = dstPitch *src_h;
	XVCopyPackedToFb(buf, overlay->fb_mem[3], overlay->fb_mem[3]+offset, dstPitch, src_h, src_w, isYUY2);
	OvlFlushPg(pScrn, 3, MS_SYNC);
	if(FlScr){ // fullscreen
	    OvlSync(pScrn);
	    OvlChBufFrmt(pScrn, overlay->phadr_mem[3], overlay->phadr_mem[3]+offset, 0,
				XVport->RGA_mode, overlay->RGA_mode, overlay->phadr_mem[0],
				src_w, src_h,XVport->x_drw, XVport->y_drw, dstPitch, overlay->var.xres_virtual);
	}
	else
	{
	    OvlScaleBuf(pScrn, overlay->phadr_mem[3], overlay->phadr_mem[3]+offset, XVport->IPP_mode,
		overlay->phadr_mem[2],overlay->phadr_mem[2]+drw_offset, src_w, src_h, drw_w, drw_h, src_w, dstPitch);
	    OvlSync(pScrn);
	    OvlChBufFrmt(pScrn, overlay->phadr_mem[2], overlay->phadr_mem[2]+drw_offset, 0,
				XVport->RGA_mode, overlay->RGA_mode, overlay->phadr_mem[0],
				drw_w, drw_h, drw_x, drw_y, dstPitch, overlay->var.xres_virtual);
	}

        break;
//    default:
    }
/*    if(!FlScr){
	OvlSwDisp(pScrn, 0, FALSE);
	VClrReg(pScrn, &XVport->clip, overlay->ShadowPg);
    }
*/
    // update cliplist 
       if(!ClipEq || (XVport->videoStatus != CLIENT_VIDEO_ON)) {
	    if(!FlScr){
	OvlClearBuf(pScrn, 0);
        OvlFlushPg(pScrn, 0, MS_ASYNC);
//		OvlSync(pScrn);
//		VFillReg(pScrn, &XVport->clip, overlay->phadr_mem[0], 0);

	    }
//	    else
//		OvlFillBuf((CARD32 *)overlay->fb_mem[0],overlay->pg_len >> 2 , 0x00010101);
//		VFillReg(pScrn, &XVport->clip, 0, 0);
            REGION_COPY(pScrn->pScreen, &XVport->clip, clipBoxes);
            // draw these 
    	    xf86XVFillKeyHelper(pScrn->pScreen, XVport->colorKey, clipBoxes);
        }
    XVport->videoStatus = CLIENT_VIDEO_ON;
    return Success;
}       


static void XVStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport = pMxv->XVport;
    int tmp;

    XVport->videoStatus = CLIENT_VIDEO_CH;
    if (exit) {
	XVDBG("video exit\n");
        XVport->videoStatus = 0;
	REGION_EMPTY(pScrn->pScreen, &XVport->clip);
	OvlReset(pScrn);
	OvlRGAUnlock(pScrn);
	XVport->rga_pa = 0;
    }
    else{
	XVDBG("video stop\n");
	OvlClearBuf(pScrn, 0);
//	OvlClearBuf(pScrn, 1);
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
    OvlHWPtr	overlay = pMxv->OvlHW;

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
    XVPortPrivPtr XVport = NULL;
    int i;

	if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
    	    return NULL;

	if(!(XVport = calloc(1, sizeof(XVPortPrivRec)+(sizeof(DevUnion) * XVPORTS))))
	{
	    free(adapt);//xfree
	    adapt = NULL;
	    XVport = NULL;
    	    return NULL;
	}
	adapt->pPortPrivates = (DevUnion*)(&XVport[1]);

	for(i = 0; i < XVPORTS; i++)
    	    adapt->pPortPrivates[i].val = i;

    XVport->colorKey = 0;//0x00020202;
    XVport->rga_pa = 0;
    XVport->videoStatus = 0;
    XVport->lastPort = -1;

//    pMxv->adaptor = adapt;
    pMxv->XVport = XVport;

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
	adapt->nFormats = LARRAY_SIZE(Formats);
	adapt->pFormats = Formats;
	adapt->nPorts = XVPORTS;// nums ports
	adapt->nAttributes = 0;
	adapt->pAttributes = NULL;
	adapt->nImages = LARRAY_SIZE(Images);
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
    REGION_NULL(pScreen, &(pMxv->XVport->clip));
    }
    return adapt;
}

void
InitXVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(NULL == pMxv->OvlHW){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Not found overlay\n");
	return;
    }

    OvlHWPtr	overlay = pMxv->OvlHW;
/*    if(overlay->fd_IPP <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Not found ipp\n");
	return;
    }
    if(overlay->fd_RGA <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Not found rga\n");
	return;
    }
*/
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

/*    if(pMxv->adaptor != NULL){
	 free(pMxv->adaptor);
	pMxv->adaptor = NULL;
    }*/
    if(pMxv->XVport != NULL){
	free(pMxv->XVport);
	pMxv->XVport = NULL;
    }
xf86DrvMsg( pScrn->scrnIndex, X_INFO, "XV:Closed\n");
}