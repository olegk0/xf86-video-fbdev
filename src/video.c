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
#include "fbdev_priv.h"
#include <sys/mman.h>

#ifdef DEBUG
#define XVDBG(format, args...)		{if(XVport->debug) WRNMSG(format, ## args);}
#else
#define XVDBG(format, args...)
#endif

#define XVPORTS 1

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
};

//-------------------------------------------------------------------
static Bool XVInitStreams(ScrnInfoPtr pScrn, short src_w, short src_h, int id)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport = pMxv->XVport;
    int xres, yres;
    OvlLayoutFormatType out_mode;

    XVDBG("setup overlay ");
    XVport->OvlPg = OvlAllocLay(SCALEL, ALC_FRONT_BACK_FB);
    if(XVport->OvlPg == ERRORL)
    	return FALSE;

    XVDBG("alloc overlay - pass:%d",XVport->OvlPg);
    XVport->FrontMemBuf = OvlGetBufByLay(XVport->OvlPg, FRONT_FB);
    XVport->BackMemBuf = OvlGetBufByLay(XVport->OvlPg, BACK_FB);
/*    if(!XVport->FrontMemBuf){
    	goto err;
    }*/
    XVDBG("get buf - pass ");
    XVport->FrontMapBuf = OvlMapBufMem(XVport->FrontMemBuf);
    XVport->BackMapBuf = OvlMapBufMem(XVport->BackMemBuf);
    if(!XVport->FrontMapBuf || !XVport->BackMapBuf){
    	goto err;
    }
    XVport->frame_fl = FALSE;
    XVDBG("map buf - pass ");

    switch(id) {
    case FOURCC_YV12://YVU planar 	needs to be converted into a SemiPlanar format (with HW-RGA or SW)
    case FOURCC_I420://YUV identical to YV12 except that the U and V plane order is reversed
    	out_mode = RK_FORMAT_YCrCb_NV12_SP;//SP disp format
        break;
    case FOURCC_UYVY://packed U0Y0V0Y1 U2Y2V2Y3		needs to unpacking in SemiPlanar
    case FOURCC_YUY2://packed low Y0U0Y1V0 hi
    	out_mode = RK_FORMAT_YCbCr_422_SP;
    	break;
    default:
    	out_mode = RK_FORMAT_DEFAULT;
    }

    XVDBG("FOURCC:%X - rkmode:%X", id, out_mode);

    out_mode = OvlSetupFb(XVport->OvlPg, RK_FORMAT_DEFAULT, out_mode, 0, 0);
    XVDBG("OvlSetupFb ret:%d", out_mode);

    XVport->disp_pitch = OvlGetVXresByLay(XVport->OvlPg);

	if(XVport->disp_pitch<=0)
    	goto err;
    XVDBG("Pitch:%d",XVport->disp_pitch);

	XVport->Uoffset = src_h * src_w;
	XVport->Voffset = XVport->Uoffset + (XVport->Uoffset>>2);

    XVport->colorKey = HWAclSetColorKey(pScrn);

    OvlEnable(XVport->OvlPg, 1);

	XVport->w_drw = 0;
	XVport->h_drw = 0;
	XVport->x_drw = 0;
	XVport->y_drw = 0;

    XVDBG("Setup overlay - pass");
    return TRUE;
err:
    OvlFreeLay(XVport->OvlPg);
    return FALSE;
}

//-----------------------------------------------------------------
static int XVPutImage(ScrnInfoPtr pScrn,
          short src_x, short src_y, short drw_x, short drw_y,
          short src_w, short src_h, short drw_w, short drw_h,
          int image, unsigned char *buf, short width, short height,
          Bool sync, RegionPtr clipBoxes, pointer data, DrawablePtr pDraw)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport= pMxv->XVport;
    CARD32 drw_offset,offset, offset2=0, offset3=0;
    CARD32 tmp, dstPitch;
    Bool ClipEq = TRUE;
    OvlMemPgPtr CurMemBuf;

//    if(!xf86XVClipVideoHelper(&dstBox, &x1, &x2, &y1, &y2, clipBoxes, width, height))
//	return Success;

    if(XVport->videoStatus < CLIENT_VIDEO_INIT){
    	XVDBG("Video init  drw_x=%d,drw_y=%d,drw_w=%d,drw_h=%d,src_x=%d,src_y=%d,src_w=%d,src_h=%d,width=%d,height=%d, image_id=%X",
			drw_x,drw_y,drw_w,drw_h,src_x,src_y,src_w,src_h,width, height, image);
    	if(!XVInitStreams(pScrn, src_w, src_h, image))
    		return BadAlloc;
    	XVport->videoStatus = CLIENT_VIDEO_INIT;
    }


    if(!RegionEqual(&XVport->clip, clipBoxes)){
    	XVport->videoStatus = CLIENT_VIDEO_CHNG;
    	RegionCopy(&XVport->clip, clipBoxes);
    }

    if((XVport->w_drw != drw_w) || (XVport->h_drw != drw_h)||(XVport->x_drw != drw_x) || (XVport->y_drw != drw_y)){
    	XVport->videoStatus = CLIENT_VIDEO_CHNG;
    	XVport->w_drw = drw_w;
    	XVport->h_drw = drw_h;
    	XVport->x_drw = drw_x;
    	XVport->y_drw = drw_y;
    }

    if(XVport->videoStatus == CLIENT_VIDEO_CHNG){
    	ClipEq = FALSE;
    	XVDBG("Video change  drw_x=%d,drw_y=%d,drw_w=%d,drw_h=%d,src_x=%d,src_y=%d,src_w=%d,src_h=%d,width=%d,height=%d, image_id=%X",drw_x,drw_y,drw_w,drw_h,src_x,src_y,src_w,src_h,width, height, image);
    	OvlSetupDrw(XVport->OvlPg, drw_x, drw_y, drw_w, drw_h, src_w, src_h);
//	XVport->videoStatus = CLIENT_VIDEO_ON;
    }

    if(XVport->frame_fl){
    	OvlFlipFb(XVport->OvlPg, BACK_FB, 0);
    	CurMemBuf = XVport->FrontMemBuf;
    }else{
    	OvlFlipFb(XVport->OvlPg, FRONT_FB, 0);
    	CurMemBuf = XVport->BackMemBuf;
    }

   	switch(image) {
   	case FOURCC_I420://YYYY	UU	VV
   		OvlCopyPlanarToFb(CurMemBuf, buf, XVport->Uoffset, XVport->Voffset,	XVport->disp_pitch, src_w, src_h);
   		break;
   	case FOURCC_YV12://YYYY	VV	UU
   		OvlCopyPlanarToFb(CurMemBuf, buf, XVport->Voffset, XVport->Uoffset,	XVport->disp_pitch, src_w, src_h);
   		break;
   	case FOURCC_YUY2://YUYV
   		OvlCopyPackedToFb(CurMemBuf, buf, XVport->disp_pitch, src_w, src_h, FALSE);
   		break;
   	case FOURCC_UYVY:
   		OvlCopyPackedToFb(CurMemBuf, buf, XVport->disp_pitch, src_w, src_h, TRUE);
   		break;
	//    default:
   	}

	if(XVport->videoStatus != CLIENT_VIDEO_ON){
		HWAclFillKeyHelper(pDraw, XVport->colorKey, clipBoxes, TRUE);
    }

	XVport->videoStatus = CLIENT_VIDEO_ON;
    XVport->frame_fl = !XVport->frame_fl;
    return Success;
}


static void XVStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XVPortPrivPtr XVport = pMxv->XVport;
    int tmp;

    XVport->videoStatus = CLIENT_VIDEO_CHNG;
    if (exit) {
    	XVDBG("video exit");
        XVport->videoStatus = 0;
        REGION_EMPTY(pScrn->pScreen, &XVport->clip);
//        OvlSetColorKey(0);
//	OvlEnable(pScrn, XVport->OvlPg, 0);
        OvlFreeLay(XVport->OvlPg);
    }
    else{
    	XVDBG("video stop");
    	XVport->videoStatus = CLIENT_VIDEO_CHNG;
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
    HWAclPtr hwacl = pMxv->HWAcl;
    XVPortPrivPtr XVport = pMxv->XVport;

    XVDBG("QueryBestSize  vidW=%d, vidH=%d, xres=%d, yres=%d",vidW,vidH,hwacl->cur_var.xres,hwacl->cur_var.yres);
    if(drawW > hwacl->cur_var.xres)
    	*retW = hwacl->cur_var.xres;
    else
    	*retW = drawW;

    if(drawH > hwacl->cur_var.yres)
    	*retH = hwacl->cur_var.yres;
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
	    MFREE(adapt);//xfree
	    adapt = NULL;
	    XVport = NULL;
    	    return NULL;
	}
	adapt->pPortPrivates = (DevUnion*)(&XVport[1]);

	for(i = 0; i < XVPORTS; i++)
    	    adapt->pPortPrivates[i].val = i;

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

    	adapt->name = "RkXV";
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
InitXVideo(ScreenPtr pScreen, Bool debug)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    pMxv->XVport = NULL;
    INFMSG("XV:Init begin");

    if(NULL == pMxv->HWAcl){
    	ERRMSG("XV:Not found overlay");
    	return;
    }

    HWAclPtr hwacl = pMxv->HWAcl;

    newAdaptor = XVInitAdaptor(pScreen);

    if (newAdaptor == NULL){
    	ERRMSG("XV:Error init adapter");
    	CloseXVideo(pScreen);
    	return;
    }
    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
    	if(!num_adaptors) {
            num_adaptors = 1;
            adaptors = &newAdaptor;
    	}else{
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
        	ERRMSG("XV:Fail initing screen");
        	CloseXVideo(pScreen);
        }
        else{
        	INFMSG("XV:Init complete");
            XVPortPrivPtr XVport = pMxv->XVport;
            XVport->debug = debug;
        }
    }

    if(newAdaptors)
    	MFREE(newAdaptors);//xfree
}

void
CloseXVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    if(pMxv->XVport != NULL){
    	MFREE(pMxv->XVport);
		pMxv->XVport = NULL;
    }
    INFMSG("XV:Closed");
}
