/*

 *  For rk3066
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
//    XVIMAGE_RGB,
//    XVIMAGE_RGB888,
};

//-------------------------------------------------------------------
static Bool XVInitStreams(ScrnInfoPtr pScrn, short drw_x, short drw_y, short drw_w,
						short drw_h, short width, short height, int id)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    XVPortPrivPtr XVport = pMxv->XVport;
//    CARD32 tres, Nwidth
    int in_mode=0,out_mode=0, xres, yres;

    XVDBG("Try setup overlay \n");
    XVport->OvlPg = OvlAllocLay(pScrn, SCALEL);
    if(XVport->OvlPg == ERRORL) return FALSE;

    XVDBG("alloc overlay - pass \n");
    XVport->PMemBuf = OvlGetBufByLay(pScrn, XVport->OvlPg);//    XVport->PMemBuf = OvlAllocMemPg(pScrn, BufMem);
    if(XVport->PMemBuf == NULL){
	OvlFreeLay(pScrn, XVport->OvlPg);
	return FALSE;
    }
    XVDBG("get buf - pass \n");
    XVport->fb_mem = OvlMapBufMem(pScrn, XVport->PMemBuf);
    if(XVport->fb_mem == NULL){
	OvlFreeLay(pScrn, XVport->OvlPg);
	return FALSE;
    }
    XVDBG("map buf - pass \n");
//    XVport->mio_offset = XVport->PMemBuf->offset_mio;
    XVport->disp_pitch = overlay->OvlLay[XVport->OvlPg].var.xres_virtual;//TODO

    switch(id) {
    case FOURCC_YV12://YVU planar 	needs to be converted into a SemiPlanar format (with HW-RGA or SW)
    case FOURCC_I420://YUV identical to YV12 except that the U and V plane order is reversed
	in_mode = YCrCb_NV12_P;//SP disp format
	out_mode = YCrCb_NV12_SP;//?
        break;
    case FOURCC_UYVY://packed U0Y0V0Y1 U2Y2V2Y3		needs to unpacking in SemiPlanar
    case FOURCC_YUY2://packed low Y0U0Y1V0 hi
//	in_mode = 0;
	out_mode = YCbCr_422_SP;
	break;
    default:
	out_mode = RGBX_8888;
    }
    
    OvlSetupFb(pScrn, in_mode, out_mode, XVport->OvlPg);
    OvlSetColorKey(pScrn, XVport->colorKey);
    OvlEnable(pScrn, XVport->OvlPg, 1);

    XVDBG("Setup overlay - pass\n");
    return TRUE;
}

//-----------------------------------------------------------------
//convert packed U0Y0V0Y1 U2Y2V2Y3 to SemiPlanar for display
static void
XVCopyPackedToFb(const void *src, void *dst_Y, void *dst_UV,/* CARD32 dst_offset,
	    int srcPitch,*/ int dstPitch, int h, int w)
{
    uint32_t Fvars[4];

    Fvars[0]= (w/*px*/)>>2;//stride; aligned by 4px (4px (8bytes) per pass)
    Fvars[1]= h;//lines;
    Fvars[2]= (w/*px*/)<<1;//SrcPitch;
    Fvars[3]= dstPitch/*px*/;

        asm volatile (
        "up0: \n\t"
        "mov	v5,#0 \n\t"//counter
        "push {%[Yvar],%[Src],%[UVvar]}\n\t"
        "up1: \n\t"
        "ldr	v1,[%[Src]]\n\t"
        "bic	v2,v1,#0xFFFFFF00\n\t"
        "bic	v3,v1,#0xFF00FFFF\n\t"
        "orr	v3,v2,v3,lsr #8\n\t"
        "bic	v1,v1,#0x00FF00FF\n\t"
        "orr	v1,v1,v1,lsl #8\n\t"
        "mov	v6,v1,lsr #16\n\t"
//
        "ldr	v1,[%[Src],#4]\n\t"//4byte skip
        "bic	v2,v1,#0xFF00FF00\n\t"
        "orr	v2,v2,v2,lsr #8\n\t"
        "orr	v3,v3,v2,lsl #16\n\t"
        "str	v3,[%[Yvar]]\n\t"
        "bic	v2,v1,#0xFFFF00FF\n\t"
        "bic	v1,v1,#0x00FFFFFF\n\t"
        "orr	v1,v1,v2,lsl #8\n\t"
        "orr	v6,v6,v1\n\t"
        "str	v6,[%[UVvar]]\n\t"

        "add	%[Src],%[Src],#8 \n\t"//8byte skip
        "add	%[Yvar],%[Yvar],#4 \n\t"//4byte skip
        "add	%[UVvar],%[UVvar],#4 \n\t"//4byte skip
        "add	v5,v5,#1 \n\t"//+1
        "ldr	v1,[%[Fvars],%[Stride]]\n\t"
        "cmp	v5,v1 \n\t"// = stride?
        "bne	up1 \n\t"//if no - jmp up

	"pop {%[Yvar],%[Src],%[UVvar]}\n\t"
        "ldr	v1,[%[Fvars],%[SrcPitch]]\n\t"
        "add	%[Src],%[Src],v1\n\t"
        "ldr	v1,[%[Fvars],%[DstPitch]]\n\t"
        "add	%[Yvar],%[Yvar],v1\n\t"
        "add	%[UVvar],%[UVvar],v1\n\t"

        "ldr	v2,[%[Fvars],%[Lines]]\n\t"
        "subs	v2,v2,#1 \n\t"//-1 and set zero flag if 0
        "str	v2,[%[Fvars],%[Lines]]\n\t"
        "bne	up0 \n\t"//if no - jmp up0
	: : [Src] "r"(src), [Yvar] "r"(dst_Y), [UVvar] "r"(dst_UV), [Fvars] "r"(&Fvars), [Stride] "J"(0),
	 [Lines] "J"(4*1), [SrcPitch] "J"(4*2), [DstPitch] "J"(4*3)
        : "v1","v2","v3","v6","v5","memory"

        );

}
//-----------------------------------------------------------------
static void
XVCopyPlanarToFb(const void *src_Y,const void *src_U,const void *src_V, void *dst_Y, void *dst_UV, int dstPitch, int h, int w)
{
    uint32_t Fvars[4];

    Fvars[0]= (w/*px*/)>>2;//stride;
    Fvars[1]= h;//lines;
    Fvars[2]= (w/*px*/);//SrcPitch;
    Fvars[3]= dstPitch/*px*/;

        asm volatile (
//************Y block
        "ldr	v3,[%[Fvars],%[Lines]]\n\t"
        "ldr	v5,[%[Fvars],%[Stride]]\n\t"
        "up20: \n\t"
        "mov	v6,#0 \n\t"//counter
        "push {%[SrcY],%[Yvar]}\n\t"
        "up21: \n\t"
        "ldr	v1,[%[SrcY]]\n\t"
        "str	v1,[%[Yvar]]\n\t"
        "add	%[SrcY],%[SrcY],#4\n\t"
        "add	%[Yvar],%[Yvar],#4\n\t"

        "add	v6,v6,#1 \n\t"//+1
        "cmp	v6,v5 \n\t"// = stride?
        "bne	up21 \n\t"//if no - jmp up

	"pop {%[SrcY],%[Yvar]}\n\t"
        "ldr	v1,[%[Fvars],%[SrcPitch]]\n\t"
        "add	%[SrcY],%[SrcY],v1\n\t"
        "ldr	v2,[%[Fvars],%[DstPitch]]\n\t"
        "add	%[Yvar],%[Yvar],v2\n\t"

        "subs	v3,v3,#1 \n\t"//-1 and set zero flag if 0
        "bne	up20 \n\t"//if no - jmp up0
//************UV block
        "ldr	v3,[%[Fvars],%[Lines]]\n\t"
        "mov	v3,v3,lsr #1 \n\t"
        "str	v3,[%[Fvars],%[Lines]]\n\t"
        "ldr	v5,[%[Fvars],%[Stride]]\n\t"
        "up30: \n\t"
        "mov	v6,#0 \n\t"//counter
	"push {%[SrcU],%[SrcV],%[UVvar]}\n\t"
        "up31: \n\t"

	"ldrh	v1,[%[SrcU]]\n\t"
	"add	%[SrcU],%[SrcU],#2\n\t"
	"bic	v3,v1,#0xFFFFFF00\n\t"
	"bic	v2,v1,#0xFFFF00FF\n\t"
	"orr	v3,v3,v2,lsl #8\n\t"//v3=0x00uu00uu
	"ldrh	v1,[%[SrcV]]\n\t"
	"add	%[SrcV],%[SrcV],#2\n\t"
	"bic	v2,v1,#0xFFFFFF00\n\t"
	"orr	v3,v3,v2,lsl #8\n\t"//v3=0x00uuvvuu
	"bic	v2,v1,#0xFFFF00FF\n\t"
	"orr	v3,v3,v2,lsl #16\n\t"//v3=0xvvuuvvuu
	"str	v3,[%[UVvar]]\n\t"
	"add	%[UVvar],%[UVvar],#4 \n\t"//4byte skip

        "add	v6,v6,#1 \n\t"//+1
        "cmp	v6,v5 \n\t"// = stride?
        "bne	up31 \n\t"//if no - jmp up

	"pop {%[SrcU],%[SrcV],%[UVvar]}\n\t"
        "ldr	v1,[%[Fvars],%[SrcPitch]]\n\t"
	"add	%[SrcU],%[SrcU],v1,lsr #1\n\t"
	"add	%[SrcV],%[SrcV],v1,lsr #1\n\t"
        "ldr	v2,[%[Fvars],%[DstPitch]]\n\t"
	"add	%[UVvar],%[UVvar],v2\n\t"
        "ldr	v2,[%[Fvars],%[Lines]]\n\t"
        "subs	v2,v2,#1 \n\t"//-1 and set zero flag if 0
        "str	v2,[%[Fvars],%[Lines]]\n\t"
        "bne	up30 \n\t"//if no - jmp up0
	: : [SrcY] "r"(src_Y), [SrcU] "r"(src_U), [SrcV] "r"(src_V), [Yvar] "r"(dst_Y), [UVvar] "r"(dst_UV),
    [Fvars] "r"(&Fvars), [Stride] "J"(0), [Lines] "J"(4*1), [SrcPitch] "J"(4*2), [DstPitch] "J"(4*3)
        : "a1","v1","v2","v3","v5","v6","memory"

        );

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
    Bool ClipEq = FALSE;


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

    if(XVport->videoStatus < CLIENT_VIDEO_INIT){
	XVDBG("video params  drw_x=%d,drw_y=%d,drw_w=%d,drw_h=%d,src_x=%d,src_y=%d,src_w=%d,src_h=%d,width=%d,height=%d, image_id=%X\n",drw_x,drw_y,drw_w,drw_h,src_x,src_y,src_w,src_h,width, height, image);
	XVDBG("video params clipBoxes x1=%d,x2=%d,y1=%d,y2=%d\n",clipBoxes->extents.x1,clipBoxes->extents.x2,clipBoxes->extents.y1,clipBoxes->extents.y2);
	if(!XVInitStreams(pScrn, drw_x, drw_y, drw_w, drw_h, src_w, src_h, image))
	    return BadAlloc;
	XVport->videoStatus = CLIENT_VIDEO_INIT;
//        REGION_COPY(pScrn->pScreen, &XVport->clip, clipBoxes);
    }


    if(REGION_EQUAL(pScrn->pScreen, &XVport->clip, clipBoxes))
//    if(XVport->clip.extents.x1 == clipBoxes->extents.x1 && XVport->clip.extents.x2 == clipBoxes->extents.x2 &&
//	XVport->clip.extents.y1 == clipBoxes->extents.y1 && XVport->clip.extents.y2 == clipBoxes->extents.y2)
	ClipEq = TRUE;
    else
	XVport->videoStatus = CLIENT_VIDEO_CHNG;

    if((XVport->w_src != src_w) || (XVport->h_src != src_h)){
	XVport->videoStatus = CLIENT_VIDEO_CHNG;
	XVport->w_src = src_w;
	XVport->h_src = src_h;
    }

    if(XVport->videoStatus == CLIENT_VIDEO_CHNG){
	OvlSetupDrw(pScrn, drw_x, drw_y, drw_w, drw_h, src_w, src_h, XVport->OvlPg, TRUE);//if  draw directly on the screen
//	OvlSetupDrw(pScrn, drw_x, drw_y, 1920, 1080, src_w, src_h, XVport->OvlPg, FALSE);//if  draw directly on the screen
	XVport->videoStatus = CLIENT_VIDEO_ON;
	XVport->Uoffset = src_h * src_w;
	XVport->Voffset = XVport->Uoffset + (XVport->Uoffset>>2);
    }
/*
    src_h = src_h & ~1;
    src_w = src_w & ~3;
    drw_h = drw_h & ~1;
    drw_w = drw_w & ~1;
*/
    switch(image) {
    case FOURCC_I420:
	XVCopyPlanarToFb(buf,buf+XVport->Uoffset,buf+XVport->Voffset, XVport->fb_mem,
			XVport->fb_mem+XVport->PMemBuf->offset_mio, XVport->disp_pitch, src_h, src_w);
        break; 
    case FOURCC_YV12:
	XVCopyPlanarToFb(buf,buf+XVport->Voffset,buf+XVport->Uoffset, XVport->fb_mem,
			XVport->fb_mem+XVport->PMemBuf->offset_mio, XVport->disp_pitch, src_h, src_w);
        break; 
    case FOURCC_YUY2:
	XVCopyPackedToFb(buf, XVport->fb_mem, XVport->fb_mem+XVport->PMemBuf->offset_mio, XVport->disp_pitch, src_h, src_w);
	break;
    case FOURCC_UYVY:
	XVCopyPackedToFb(buf,  XVport->fb_mem+XVport->PMemBuf->offset_mio,XVport->fb_mem, XVport->disp_pitch, src_h, src_w);
        break;
//    default:
    }

    // update cliplist 
       if(!ClipEq || (XVport->videoStatus != CLIENT_VIDEO_ON)) {
            REGION_COPY(pScrn->pScreen, &XVport->clip, clipBoxes);
            // draw these 
    	    xf86XVFillKeyHelper(pScrn->pScreen, XVport->colorKey, clipBoxes);
        }
    OvlWaitSync(pScrn, XVport->OvlPg);
    XVport->videoStatus = CLIENT_VIDEO_ON;
    return Success;
}


static void XVStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport = pMxv->XVport;
    int tmp;

    XVport->videoStatus = CLIENT_VIDEO_CHNG;
    if (exit) {
	XVDBG("video exit\n");
        XVport->videoStatus = 0;
	REGION_EMPTY(pScrn->pScreen, &XVport->clip);
	OvlClearBuf(pScrn, XVport->PMemBuf);
	OvlUnMapBufMem(pScrn, XVport->PMemBuf);
//	OvlReset(pScrn);
	OvlSetColorKey(pScrn, 0);
	OvlEnable(pScrn, XVport->OvlPg, 0);
	OvlFreeLay(pScrn, XVport->OvlPg);
    }
    else{
	XVDBG("video stop\n");
//TODO	OvlClearBuf(pScrn, 0, FBO1);
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

    if(drawW > overlay->cur_var.xres)
	*retW = overlay->cur_var.xres;
    else
	*retW = drawW;

    if(drawH > overlay->cur_var.yres)
	*retH = overlay->cur_var.yres;
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

    XVport->colorKey = 0x00020202;
    XVport->videoStatus = 0;
    XVport->lastPort = -1;

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
    XVPortPrivPtr XVport = pMxv->XVport;
    REGION_NULL(pScreen, &(XVport->clip));
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

    pMxv->XVport = NULL;
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "XV:Init begin\n");

    if(NULL == pMxv->OvlHW){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "XV:Not found overlay\n");
	return;
    }

    OvlHWPtr	overlay = pMxv->OvlHW;

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

    if(pMxv->XVport != NULL){
	free(pMxv->XVport);
	pMxv->XVport = NULL;
    }
xf86DrvMsg( pScrn->scrnIndex, X_INFO, "XV:Closed\n");
}