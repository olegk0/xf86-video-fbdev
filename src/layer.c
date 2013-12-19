/*
 *  For rk3066
 *  Author: olegk0 <olegvedi@gmail.com>
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
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86xv.h"
#include "os.h"

#include "layer.h"
#include "rk3066.h"
#include "fbdev_priv.h"
#include "mali_dri2.h"
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>

#define HW_TIMEOUT 100

#define FbByLay(layout) (overlay->OvlLay[layout].OvlFb)
#define MBufByLay(layout) (FbByLay(layout)->OvlMemPg)

#ifdef DEBUG
#define OVLDBG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, format, ## args)
#else
#define OVLDBG(format, args...)
#endif

#ifdef DEBUG
void DdgPrintRGA(ScrnInfoPtr pScrn, struct rga_req *RGA_req)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    OVLDBG("src.format:%d\n",RGA_req->src.format);
    OVLDBG("src.act_w:%d\n",RGA_req->src.act_w);
    OVLDBG("src.act_h:%d\n",RGA_req->src.act_h);

    OVLDBG("src.yrgb_addr:0x%X\n",RGA_req->src.yrgb_addr);
    OVLDBG("src.uv_addr:0x%X\n",RGA_req->src.uv_addr);
    OVLDBG("src.v_addr:0x%X\n",RGA_req->src.v_addr);

    OVLDBG("src.vir_w:%d\n",RGA_req->src.vir_w);
    OVLDBG("src.vir_h:%d\n",RGA_req->src.vir_h);
//Dst
    OVLDBG("dst.vir_w:%d\n",RGA_req->dst.vir_w);
    OVLDBG("dst.vir_h:%d\n",RGA_req->dst.vir_h);
    OVLDBG("dst.x_offset:%d\n",RGA_req->dst.x_offset);
    OVLDBG("dst.y_offset:%d\n",RGA_req->dst.y_offset);
    OVLDBG("dst.act_w:%d\n",RGA_req->dst.act_w);
    OVLDBG("dst.act_h:%d\n",RGA_req->dst.act_h);//1/2 

    OVLDBG("dst.format:%d\n",RGA_req->dst.format);
    OVLDBG("dst.yrgb_addr:0x%X\n",RGA_req->dst.yrgb_addr);

    OVLDBG("clip.xmax:%d\n",RGA_req->clip.xmax);
    OVLDBG("clip.ymax:%d\n",RGA_req->clip.ymax);
}
#endif

static Bool LayIsUIfb(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(FbByLay(layout)->FbType == UIL)
	return TRUE;
    else
	return FALSE;
}

//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------

OvlMemPgPtr OvlGetBufByLay(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout < OVLs && layout >= 0)
	return MBufByLay(layout);
    else
	return NULL;
}

//--------------------------------------------------------------------------------


//++++++++++++++++++++++++++++++++++++IPP++++++++++++++++++++++++++++++++++++++++++
int ovlInitIPPHW(ScrnInfoPtr pScrn)
{
    return open(FB_DEV_IPP, O_RDWR);
}
//-------------------------------------------------------------
static int ovlIppBlit(ScrnInfoPtr pScrn, struct rk29_ipp_req *ipp_req)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret, timeout = 0;

    while(pthread_mutex_trylock(&overlay->ippmutex) ==  EBUSY){
	timeout++;
	if(timeout > HW_TIMEOUT){
	    OVLDBG("Timeout ipp\n");
	    return -1;
	}
    }
    ret = ioctl(overlay->fd_IPP, IPP_BLIT_SYNC, ipp_req);
    pthread_mutex_unlock(&overlay->ippmutex);
    return ret;
}
//-----------------------------------------------------------------------
static void ovlIppInitReg(ScrnInfoPtr pScrn, struct rk29_ipp_req *IPP_req, uint32_t SrcYAddr, int SrcFrmt, int Src_w, int Src_h,
		uint32_t DstYAddr, int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    memset(&IPP_req, 0, sizeof(struct rk29_ipp_req));

    IPP_req->src0.w = Src_w;
    IPP_req->src0.h = Src_h;
    IPP_req->src_vir_w = Src_vir;

    IPP_req->src0.fmt = SrcFrmt;
    IPP_req->dst0.fmt = IPP_req->src0.fmt;
    IPP_req->dst_vir_w = Dst_vir;
    IPP_req->timeout = 100;
    IPP_req->flag = IPP_ROT_0;

    IPP_req->src0.YrgbMst = SrcYAddr;
//    IPP_req->src0.CbrMst = SrcUVAddr;
    IPP_req->dst0.YrgbMst = DstYAddr;
//    IPP_req->dst0.CbrMst = DstUVAddr;
    IPP_req->dst0.w = IPP_req->src0.w;
    IPP_req->dst0.h = IPP_req->src0.h;
}
//++++++++++++++++++++++++++++++++++++RGA++++++++++++++++++++++++++++++++++++++++++
int ovlInitRGAHW(ScrnInfoPtr pScrn)
{
    return open(FB_DEV_RGA, O_RDWR);
}
//-------------------------------------------------------------
static int ovlRgaBlit(ScrnInfoPtr pScrn, struct rga_req *RGA_req, int syncmode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret, timeout = 0;

    while(pthread_mutex_trylock(&overlay->rgamutex) ==  EBUSY){
	timeout++;
	if(timeout > HW_TIMEOUT){
	    OVLDBG("Timeout rga\n");
	    return -1;
	}

    }
    ret = ioctl(overlay->fd_RGA, syncmode, RGA_req);
    pthread_mutex_unlock(&overlay->rgamutex);
    return ret;
}
//------------------------------------------------------------------------------
static void ovlRgaInitReg(ScrnInfoPtr pScrn, struct rga_req *RGA_req, uint32_t SrcYAddr, int SrcFrmt, int DstFrmt,
		uint32_t DstYAddr, int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    memset(RGA_req, 0, sizeof(struct rga_req));
//Src
    RGA_req->src.format = SrcFrmt;//    = 0x1,;
    RGA_req->src.act_w = Drw_w;
    RGA_req->src.act_h = Drw_h;

    RGA_req->src.yrgb_addr = SrcYAddr;
//    RGA_req->src.uv_addr  = SrcUVAddr;
//    RGA_req->src.v_addr   = SrcVAddr;

    RGA_req->src.vir_w = Src_vir;
    RGA_req->src.vir_h = overlay->cur_var.yres;
//Dst
    RGA_req->dst.vir_w = Dst_vir;
    RGA_req->dst.vir_h = overlay->cur_var.yres;
    RGA_req->dst.x_offset = Drw_x;
    RGA_req->dst.y_offset = Drw_y;
    RGA_req->dst.act_w = RGA_req->src.act_w;
    RGA_req->dst.act_h = RGA_req->src.act_h;//1/2 

    RGA_req->dst.format = DstFrmt;
    RGA_req->dst.yrgb_addr = DstYAddr;

    RGA_req->clip.xmax = Dst_vir-1;
    RGA_req->clip.ymax = overlay->cur_var.yres-1;
}
//---------------------------------------------------------------
static void ovlRgaDrwAdd(ScrnInfoPtr pScrn, struct rga_req *RGA_req, int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

//Src
    RGA_req->src.act_w = Drw_w;
    RGA_req->src.act_h = Drw_h;
    RGA_req->src.vir_w = Src_vir;

    RGA_req->dst.x_offset = Drw_x;
    RGA_req->dst.y_offset = Drw_y;
    RGA_req->dst.act_w = RGA_req->src.act_w;
    RGA_req->dst.act_h = RGA_req->src.act_h;//1/2 
//    RGA_req->clip.xmax = overlay->cur_var.xres-1;
//    RGA_req->clip.ymax = overlay->cur_var.yres-1;
}
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
static void ovlSelHwMods(ScrnInfoPtr pScrn, int mode, OvlLayPg layout, Bool Src)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint8_t		IPP_mode;
    uint8_t		RGA_mode;

    switch(mode) {
    case RGBX_8888:
    case RGBA_8888:
	RGA_mode = RK_FORMAT_RGBX_8888;
	IPP_mode = IPP_XRGB_8888;
	break;
    case RGB_888:
	RGA_mode = RK_FORMAT_RGB_888;
	IPP_mode = 0;//TODO: add support to ipp
	break;
    case RGB_565:
	RGA_mode = RK_FORMAT_RGB_565;
	IPP_mode = IPP_RGB_565;
	break;
    case YCrCb_NV12_SP:
	RGA_mode = RK_FORMAT_YCbCr_420_SP;
	IPP_mode = IPP_Y_CBCR_H2V2;//nearest suitable
        break;
    case YCbCr_422_SP:
	RGA_mode = RK_FORMAT_YCrCb_422_SP;
	IPP_mode = IPP_Y_CBCR_H2V1;//nearest suitable
	break;
    case YCrCb_NV12_P:
	RGA_mode = RK_FORMAT_YCbCr_420_P;
	IPP_mode = IPP_Y_CBCR_H2V2;
        break;
    case YCbCr_422_P:
	RGA_mode = RK_FORMAT_YCrCb_422_P;
	IPP_mode = IPP_Y_CBCR_H2V1;
	break;
    case YCrCb_444:
	break;

    default:
	RGA_mode = RK_FORMAT_RGBX_8888;
	IPP_mode = IPP_XRGB_8888;
    }
    if(layout >= 0 && layout < OVLs){
	if(Src){
	    overlay->OvlLay[layout].IPP_req.src0.fmt = IPP_mode;
	    overlay->OvlLay[layout].RGA_req.src.format = RGA_mode;
	}
	else{
	    overlay->OvlLay[layout].RGA_req.dst.format = RGA_mode;
	}
	overlay->OvlLay[layout].IPP_req.dst0.fmt = overlay->OvlLay[layout].IPP_req.src0.fmt;
    }
}
//*******************************************************************************
int OvlPanBufSync(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint32_t tmp[2];

    if(layout < OVLs && layout >= 0){
	tmp[0] = PMemPg->offset;
	tmp[1] = tmp[0];
	if(!LayIsUIfb(pScrn, layout))
	    return ioctl(FbByLay(layout)->fd, FBIOSET_FBMEM_OFFS_SYNC, &tmp);
    }
	return -1;
}
//--------------------------------------------------------------------------------
int OvlSetColorKey(ScrnInfoPtr pScrn, uint32_t color)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    return ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOSET_COLORKEY, &color);
}
//------------------------------------------------------------------
int OvlWaitSync(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint32_t tmp=0;

    if(!pMxv->WaitForSync)
	return -1;

    return ioctl(FbByLay(layout)->fd, FBIO_WAITFORVSYNC, &tmp);
}
//--------------------------------------------------------------------------------
int OvlCpBufToDisp(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    OvlLayPg t;

    OVLDBG("OvlCpBufToDisp OvlPg:%d\n",layout);
//	overlay->OvlLay[layout].ResChange = FALSE;
    if(layout < OVLs && layout >= 0){
	overlay->OvlLay[layout].RGA_req.src.yrgb_addr = PMemPg->phadr_mem;
	return ovlRgaBlit(pScrn, &overlay->OvlLay[layout].RGA_req, RGA_BLIT_SYNC);
    }
    OVLDBG("OvlCpBufToDisp Error\n");
    return -1;
}
//--------------------------------------------------------------------------------
int ovlDrwClrRect(ScrnInfoPtr pScrn, OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint8_t *buf, *mbuf;
    unsigned int x,y,v_h,v_w,bpp,mode;
    Bool YUV420fl=FALSE,YUV=FALSE;


    mode = overlay->OvlLay[layout].var.nonstd;
    if(MBufByLay(layout)->fb_mem != NULL){

	switch(mode){//TODO
	case RGBX_8888:
        case RGBA_8888:
	    bpp = 4;
	case RGB_888:
	    bpp = 3;
	case RGB_565:
	    bpp = 2;
	    return -1;//TODO not tested
	    break;
	case YCrCb_NV12_SP:
//	    bpp = 1;//average 0.5 for color and 1 for lum 
	    YUV420fl = TRUE;
	case YCbCr_422_SP:
	    YUV = TRUE;
	    bpp = 1;//average 1 for color and 1 for lum 
	    break;
	default:
	    return -1;
	}

	v_w = overlay->OvlLay[layout].var.xres_virtual;
	buf = MBufByLay(layout)->fb_mem + (Drw_y*v_w+Drw_x)*bpp;
	if(YUV)
	    mbuf = buf + MBufByLay(layout)->offset_mio;
	while(Drw_h>0){
	    memset(buf,0,Drw_w*bpp);
	    buf =buf + v_w*bpp;
	    if(YUV){
		if(!(YUV420fl && (Drw_h&1))){
			memset(mbuf,0,Drw_w*bpp);
			mbuf =mbuf + v_w*bpp;
		}
	    }
	    Drw_h--;
	}
    }
    else
	return -1;
}

//------------------------------------------------------------
int OvlClearBuf(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint32_t tmp[2];

    if(PMemPg != NULL && PMemPg->MemPgType != UIFB_MEM/* !UI fb*/)
	return -1;
    tmp[0] = PMemPg->offset;
    tmp[1] = overlay->MaxPgSize;
    return ioctl(overlay->OvlFb[MasterOvlFB].fd, FBIOSET_FBMEM_CLR, &tmp);
}
//------------------------------------------------------------
int OvlUnMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    int ret = -1;

    if(PMemPg != NULL && PMemPg->MemPgType != UIFB_MEM/* !UI fb*/){
	if(PMemPg->fb_mem == NULL)
	    ret = 0;
	else{
	    ret = munmap(PMemPg->fb_mem, overlay->MaxPgSize);
	    if(ret == 0)
		PMemPg->fb_mem = NULL;
	}
    }
    return ret;
}
//----------------------------------------------------------------------
void * OvlMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    if(PMemPg != NULL){
	if(PMemPg->fb_mem == NULL)
	    PMemPg->fb_mem = mmap( NULL, overlay->MaxPgSize, PROT_READ | PROT_WRITE,
					MAP_SHARED, overlay->OvlFb[MasterOvlFB].fd, PMemPg->offset);
	return PMemPg->fb_mem;
    }
    return NULL;
}
//---------------------------------------------------------------------
int OvlFlushPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, int mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(PMemPg->fb_mem != NULL)
	return msync(PMemPg->fb_mem,overlay->MaxPgSize, mode);
    else
	return -1;
}
//--------------------------------------------------------------------
int ovlSetMode(ScrnInfoPtr pScrn, unsigned short xres, unsigned short yres, unsigned char mode, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
//    OvlFbPg FbPg = FbByLay(layout);
    int ret=0;

    if(layout >= OVLs || layout < 0)
	return -1;
    if(!LayIsUIfb(pScrn, layout)){/*TODO !UIL*/
	if((xres > overlay->OvlLay[layout].var.xres_virtual)||(yres > overlay->OvlLay[layout].var.yres_virtual)) return -1;
//    if((xres > overlay->cur_var.xres)||(yres > overlay->cur_var.yres)) return -1;
	if(mode>0)
	    overlay->OvlLay[layout].var.nonstd = mode;
	if(xres>0)
	    overlay->OvlLay[layout].var.xres = xres;
	if(yres>0)
	    overlay->OvlLay[layout].var.yres = yres;
	ret = ioctl(FbByLay(layout)->fd, FBIOPUT_VSCREENINFO, &overlay->OvlLay[layout].var);

	if(ret == 0){
	    ovlSelHwMods(pScrn, overlay->OvlLay[layout].var.nonstd, layout, DST_MODE);
	    overlay->OvlLay[layout].ResChange = FALSE;
	}
    }
    else
	ovlSelHwMods(pScrn, overlay->cur_var.nonstd, layout, DST_MODE);
    return ret;
}
//----------------------------------------------------------------------------------
int OvlSetupFb(ScrnInfoPtr pScrn, int SrcFrmt, int DstFrmt, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout >= OVLs || layout < 0)
	return -1;
    ovlRgaInitReg(pScrn, &overlay->OvlLay[layout].RGA_req, /*SrcYAddr*/0, 0, 0,
	FbByLay(layout)->OvlMemPg->phadr_mem, 0, 0, 0, 0, overlay->OvlLay[layout].var.xres_virtual/*TODO SRC*/, overlay->OvlLay[layout].var.xres_virtual);
//    if(DstFrmt)
	ovlSetMode(pScrn, 0 , 0/*TODO*/, DstFrmt, layout);
    if(!SrcFrmt)
	SrcFrmt = overlay->OvlLay[layout].var.nonstd;
    ovlSelHwMods(pScrn, SrcFrmt, layout, SRC_MODE);
    return 0;
}
//--------------------------------------------------------------------------------
int OvlSetupBufDrw(ScrnInfoPtr pScrn, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int SrcPitch, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    SFbioDispSet pt;
    OvlFbPg FbPg;

    if(layout >= OVLs || layout < 0)
	return -1;
    if(SrcPitch){
	ovlRgaDrwAdd(pScrn, &overlay->OvlLay[layout].RGA_req, Drw_w, Drw_h, Drw_x, Drw_y, SrcPitch);
	return 0;
    }
    return -1;
//    OvlClearBuf(pScrn, overlay->OvlFb[overlay->OvlLay[layout].OvlFb].OvlMemPg);
}
//--------------------------------------------------------------------------------
int OvlSetupDrw(ScrnInfoPtr pScrn, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int Src_w, int Src_h, OvlLayPg layout, Bool BlackBorder)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    SFbioDispSet pt;
    int nSrc_h;
//    OvlFbPg FbPg;

    if(layout >= OVLs || layout < 0)
	return -1;

    if(BlackBorder && (overlay->OvlLay[layout].var.xres_virtual > (Src_h+1))){
	ovlDrwClrRect(pScrn, layout, 0, Src_h, Src_w, 2);
	nSrc_h = Src_h+2;
    }
    else
	nSrc_h = Src_h;

    pt.poffset_x = Drw_x;
    pt.poffset_y = Drw_y;
    pt.ssize_w = Src_w;
    pt.ssize_h = nSrc_h;
    pt.scale_w = (Src_w*PANEL_SIZE_X)/Drw_w;
    pt.scale_h = (Src_h*PANEL_SIZE_Y)/Drw_h;
    ioctl(FbByLay(layout)->fd, FBIOSET_DISP_PSET, &pt);
//    OvlClearBuf(pScrn, overlay->OvlFb[overlay->OvlLay[layout].OvlFb].OvlMemPg);
}
//----------------------------------------------------------------------------------
/*int OvlSwDisp(ScrnInfoPtr pScrn, int disp, Bool clear)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    OvlMemPg MemPg = overlay->OvlFb[FbPg].OvlMemPg;
    uint32_t tmp[2];

    if(pg < overlay->OvlMemPgs && pg >= 0 && layout < OVLs && layout >= 0){
	tmp[0] = overlay->OvlMemPg[MemPg].offset;
	tmp[1] = tmp[0];
	if(!LayIsUIfb(pScrn, layout))
	    return ioctl(overlay->OvlFb[FbByLay(layout)].fd, FBIOSET_FBMEM_OFFS_SYNC, &tmp);
    }

    switch(disp){
    case 1:
	overlay->ShadowPg = 1;
	break;
    case 2:
	overlay->var.yoffset = overlay->var.yres;
	overlay->ShadowPg = 0;
	break;
    default:
	if(overlay->ShadowPg){
	    overlay->var.yoffset = overlay->var.yres;
	    overlay->ShadowPg = 0;
	}
	else
	    overlay->ShadowPg = 1;
    }
    ret = OvlSync(pScrn);
    ret = ioctl(overlay->fd_o1, FBIOPAN_DISPLAY, &overlay->var);
//    if(ret) return ret;

//TODO    if(!disp && clear) OvlClearBuf(pScrn, overlay->ShadowPg, FBO1);
    return ret;
}
*/
//---------------------------------------------------------------------
int OvlEnable(ScrnInfoPtr pScrn, OvlLayPg layout, int enable)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout >= OVLs || layout < 0)
	return -1;
    if(LayIsUIfb(pScrn, layout))/*TODO !UIL*/
	return -1;
    
    return ioctl(FbByLay(layout)->fd, FBIOSET_ENABLE, &enable);
}
//---------------------------------------------------------------------
int OvlResetFB(ScrnInfoPtr pScrn, OvlLayPg layout)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    if(layout >= OVLs || layout < 0)
	return -1;
//    OvlClearBuf(pScrn, 1);
    ret = ioctl(FbByLay(layout)->fd, FBIOPUT_VSCREENINFO, &overlay->sav_var);
//    if(ret == 0 && dev == FBUI) //TODO res change by x func
//	ret =  OvlUpdSavMod(pScrn);
    return ret;
}
//------------------------------------------------------------------
OvlMemPgPtr OvlAllocMemPg(ScrnInfoPtr pScrn, OvlMemPgType type)//TODO type?
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    OvlMemPg i;

    for(i=overlay->OvlMemPgs-1;i>=0;i--)
	if(overlay->OvlMemPg[i].MemPgType == BUF_MEM && !overlay->OvlMemPg[i].InUse){
	    overlay->OvlMemPg[i].InUse = TRUE;
	    return &overlay->OvlMemPg[i];
	}
    return NULL;
}
//------------------------------------------------------------------
void OvlFreeMemPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    if(PMemPg != NULL)
	PMemPg->InUse = FALSE;
}
//--------------------------------------------------------------
/*OvlLayPg ovlSwapLay(ScrnInfoPtr pScrn, OvlLayPg pg, OvlLayoutType type)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    OvlLayPg i,t;

    if(overlay->OvlLay[pg].ReqType==ANUL){
	for(i=0;i<OVLs;i++){
	    if(!overlay->OvlLay[i].InUse){
		t = FbByLay(i);
		overlay->OvlLay[i].OvlFb = FbByLay(pg);
		FbByLay(pg) = t;
		OvlSetupFb(pScrn, 0, 0, pg);//TODO  call hw init fb, rga ipp init
		return i;
	    }
	}
    }
    return ERRORL;
}
*/
//------------------------------------------------------------------
OvlLayPg OvlAllocLay(ScrnInfoPtr pScrn, OvlLayoutType type)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    OvlLayPg i,t;

    switch(type){
    case UIL:
    case SCALEL:
    case NOT_SCALEL:
	for(i=0;i<OVLs;i++){
	    if(FbByLay(i)->FbType == type){
		if(!overlay->OvlLay[i].InUse){
		    t = i;
		    break;
		}
		else{
//		    t = ovlSwapLay(pScrn, i, type);
//		    if(t==ERRORL)
			return ERRORL;
		}
	    }
	}
	overlay->OvlLay[t].InUse = TRUE;
	overlay->OvlLay[t].ReqType = type;
	return t;
	break;
    case ANYL:
	for(i=0;i<OVLs;i++){
	    if(!overlay->OvlLay[i].InUse){
		overlay->OvlLay[i].InUse = TRUE;
		overlay->OvlLay[i].ReqType = type;
		return i;
	    }
	}
    }
    return ERRORL;
}
//------------------------------------------------------------------
void OvlFreeLay(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    if(layout < OVLs && layout >= 0){
	overlay->OvlLay[layout].InUse = FALSE;
	overlay->OvlLay[layout].ReqType = ERRORL;
    }
}
//------------------------------------------------------------------
ump_secure_id ovlGetUmpId(ScrnInfoPtr pScrn, OvlMemPg pg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
//    ump_secure_id ump_id = UMP_INVALID_SECURE_ID;

    if(pg >0 && pg < overlay->OvlMemPgs){
	if(overlay->OvlMemPg[pg].ump_fb_secure_id == UMP_INVALID_SECURE_ID){
	    overlay->OvlMemPg[pg].ump_fb_secure_id = pg-1;
	    ioctl(overlay->OvlFb[MasterOvlFB].fd, GET_UMP_SECURE_ID_BUFn, &overlay->OvlMemPg[pg].ump_fb_secure_id);
	}
	return overlay->OvlMemPg[pg].ump_fb_secure_id;
    }
    else
	return UMP_INVALID_SECURE_ID;
}

//++++++++++++++++++++++++++++++init/close+++++++++++++++++++++++++
int OvlUpdSavMod(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int ret=-1;
    OvlLayPg i;

    if(pMxv->OvlHW != NULL){
	OvlHWPtr overlay = pMxv->OvlHW;
    	ret = ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOGET_VSCREENINFO, &overlay->cur_var);
//	overlay->cur_var.activate = 0;
	for(i=0;i<OVLs;i++)
	    overlay->OvlLay[i].ResChange = TRUE;
//	overlay->cur_var.yres_virtual = overlay->cur_var.yres << 1;//double buf
//	if(ioctl(overlay->OvlFb[MasterOvlFB].fd, FBIOPUT_VSCREENINFO, &overlay->cur_var)== 0){
//	    memcpy(&overlay->OvlLay[0].var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));
//TODO	    ovlSelHwMods(pScrn, overlay->cur_var.nonstd, MasterOvlFB ?, DST_MODE);
//	    ret = ioctl(overlay->fd_o2, FBIOPUT_VSCREENINFO, &overlay->cur_var);
//	}
	overlay->ResChange = TRUE;
	OVLDBG("****Change res to %dx%d,  ret=%d ***\n", overlay->cur_var.xres, overlay->cur_var.yres, ret);
    }
    return ret;
}
//-------------------------------------------
/*static void CloseOvl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int fd,tmp;

    if(pMxv->OvlHW != NULL){
	OvlReset(pScrn);
	OvlEnable(pScrn, 0);
	fd = open("/dev/fb0", O_RDWR);
	if (fd > 0){
	    tmp=1;
	    ioctl(fd, FBIOSET_ENABLE, &tmp);
	    tmp=0;
	    ioctl(fd, FBIOSET_OVERLAY_STATE, &tmp);
//	    pMxv->OvlHW->cur_var.activate = 1;
//	    ioctl(fd, FBIOPUT_VSCREENINFO, &pMxv->OvlHW->cur_var);
	    close(fd);
        }
	free_ovl_memory(pMxv->OvlHW);
        close(pMxv->OvlHW->fd);
        free(pMxv->OvlHW);
	pMxv->OvlHW = NULL;
    }
}*/
//----------------------------------------------------------------------
void set_ovl_param(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    uint32_t yuv_phy[2],ll;
    int i;

//    overlay->pg_len = (rs + PAGE_MASK) & ~PAGE_MASK;
    for(i=0;i<OVLs;i++){
	ioctl(overlay->OvlFb[i].fd, FBIOGET_FSCREENINFO, &overlay->OvlFb[i].fix);
    }

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "Overlay memory pages\n");
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "--------------------\n");
    ll = overlay->OvlFb[MasterOvlFB].fix.smem_len;
    i = 0;
    while(ll >= overlay->MaxPgSize){
	overlay->OvlMemPgs = i + 1;
	overlay->OvlMemPg[i].InUse = FALSE;
	overlay->OvlMemPg[i].ump_fb_secure_id = UMP_INVALID_SECURE_ID;
	if(i==0){
	    overlay->OvlMemPg[i].offset = 0;
	    overlay->OvlMemPg[i].offset_mio = 0;
	    overlay->OvlMemPg[i].phadr_mem = overlay->OvlFb[UserInterfaceFB].fix.smem_start;
	    overlay->OvlMemPg[i].phadr_mio = 0;
	    overlay->OvlMemPg[i].fb_mem = pMxv->fbmem;
//	    overlay->OvlMemPg[i].MemPgType = BufMem;
	}
	else{
	    overlay->OvlMemPg[i].offset = overlay->MaxPgSize*(i-1);
	    overlay->OvlMemPg[i].phadr_mem = overlay->OvlFb[MasterOvlFB].fix.smem_start+overlay->OvlMemPg[i].offset;
	    overlay->OvlMemPg[i].offset_mio = ((overlay->MaxPgSize/2) & ~PAGE_MASK);
	    overlay->OvlMemPg[i].phadr_mio = overlay->OvlMemPg[i].phadr_mem+overlay->OvlMemPg[i].offset_mio;
	    overlay->OvlMemPg[i].fb_mem = NULL;
	    overlay->OvlMemPg[i].MemPgType = BUF_MEM;
	    overlay->OvlMemPg[i].ump_fb_secure_id = ovlGetUmpId( pScrn, i);
	    OvlClearBuf(pScrn, &overlay->OvlMemPg[i]);
	    ll -= overlay->MaxPgSize;
	}
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "Buf:%d Mem:0x%x Mio:0x%x Offset:%d sID:%d\n", i,
	    overlay->OvlMemPg[i].phadr_mem,overlay->OvlMemPg[i].phadr_mio,
	    overlay->OvlMemPg[i].offset, overlay->OvlMemPg[i].ump_fb_secure_id);
	i++;
    }
//set default 0-2 PGs - layer`s fb mem
    for(i=0;i<OVLs;i++){
	overlay->OvlMemPg[i].MemPgType = FB_MEM;
	overlay->OvlFb[i].OvlMemPg = &overlay->OvlMemPg[i];
	overlay->OvlFb[i].FbType = i;//        UIL=0,    SCALEL=1,    NotSCALEL=2,
	overlay->OvlLay[i].OvlFb = &overlay->OvlFb[i];
	memcpy(&overlay->OvlLay[i].var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));
	overlay->OvlLay[i].InUse = FALSE;
	overlay->OvlLay[i].ReqType = ERRORL;
	overlay->OvlLay[i].ResChange = FALSE;
	if(i>0){
	    yuv_phy[0] = overlay->OvlMemPg[i].phadr_mem;
	    yuv_phy[1] = overlay->OvlMemPg[i].phadr_mio; //four uv
	    ioctl(overlay->OvlFb[i].fd, FBIOSET_YUV_ADDR, &yuv_phy);
//	ovlSelHwMods(pScrn, overlay->OvlFb[i].var.nonstd, i, DST_MODE);
//	ioctl(overlay->OvlFb[i].fd, FBIOPUT_VSCREENINFO, &overlay->cur_var);
	    ovlSetMode(pScrn,0,0,0,i);
	    OvlEnable(pScrn, i, 0);
	}
    }
    overlay->OvlMemPg[0].MemPgType = UIFB_MEM;
//    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "END: set_ovl_param\n");
}

//------------------------------------------------------------------
Bool ovl_setup_ovl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int tmp,fd;

    overlay->OvlFb[MasterOvlFB].fd = open(FB_DEV_O1, O_RDWR); //main overlay 
    if (overlay->OvlFb[MasterOvlFB].fd < 0) goto err;
    overlay->OvlFb[UserInterfaceFB].fd = open(FB_DEV_UI, O_RDONLY);
    if (overlay->OvlFb[UserInterfaceFB].fd < 0) goto err1;
    overlay->OvlFb[SecondOvlFB].fd = open(FB_DEV_O2, O_RDONLY);
    if (overlay->OvlFb[SecondOvlFB].fd < 0) goto err2;

//    if( 0 != ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOGET_FSCREENINFO, &overlay->OvlFb[UserInterfaceFB].fix)) goto err3;
//    overlay->MaxPgSize = overlay->OvlFb[UserInterfaceFB].fix.smem_len;
    overlay->MaxPgSize = FB_MAXPGSIZE;

    if(OvlUpdSavMod(pScrn) == 0){
	memcpy(&overlay->sav_var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));
/*
	tmp=0;
	ioctl(overlay->fd_o1, FBIOPUT_SET_COLORKEY, &tmp);
	tmp=0;
	ioctl(fd, FBIOPUT_SET_COLORKEY, &tmp);
*/
//	SelHWMods(pScrn, overlay->var.nonstd);
//	ioctl(overlay->fd_o1, FBIOBLANK, FB_BLANK_UNBLANK);

	tmp=1;
	ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOSET_OVERLAY_STATE, &tmp);

        return(TRUE);
    }

err3:
    close(overlay->OvlFb[SecondOvlFB].fd);
err2:
    close(overlay->OvlFb[UserInterfaceFB].fd);
err1:
    close(overlay->OvlFb[MasterOvlFB].fd);
err:
    return FALSE;
}

Bool ovl_init_ovl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int i;

    pMxv->OvlHW = NULL;
    if(!(pMxv->OvlHW = calloc(1, sizeof(OvlHWRec) )))
	goto err;
    OvlHWPtr overlay = pMxv->OvlHW;

    if(!ovl_setup_ovl(pScrn)) goto err1;

    set_ovl_param(pScrn);

    if(overlay->OvlFb[MasterOvlFB].fix.smem_len < overlay->MaxPgSize*4){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error overlay mem block size:%d\n",overlay->OvlFb[MasterOvlFB].fix.smem_len);
	goto err2;
    }

    pthread_mutex_init(&overlay->rgamutex, NULL);
    pthread_mutex_init(&overlay->ippmutex, NULL);

//    OvlReset(pScrn);//TODO

    return TRUE;
err2:
    for(i=0;i<OVLs;i++)
        if(overlay->OvlFb[i].fd>0)
	    close(overlay->OvlFb[i].fd);
err1:
    free(overlay);
    overlay = NULL;
err:
    return FALSE;
}

//----------------------------main init--------------------------
void InitHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay;

    pMxv->OvlHW = NULL;
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Main setup ovl\n");

    if(!ovl_init_ovl(pScrn)){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ovl\n");
	return;
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Main setup ovl - pass\n");

    overlay = pMxv->OvlHW;
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init ipp\n");
    if (!xf86LoadKernelModule("rk29-ipp"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'rk29-ipp' kernel module\n");
    overlay->fd_IPP = ovlInitIPPHW(pScrn);
    if(overlay->fd_IPP <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ipp\n");
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized ipp\n");

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init rga\n");
    overlay->fd_RGA = ovlInitRGAHW(pScrn);
    if(overlay->fd_RGA <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init rga\n");
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized rga\n");
    return;
/*
err:
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Init ovl failed\n");
    if(overlay != NULL){
	if(overlay->fd_IPP > 0)
	    close(overlay->fd_IPP);
	if(overlay->fd_RGA > 0)
	    close(overlay->fd_RGA);

	free_ovl_memory(overlay);
        close(overlay->fd_o1);
        free(overlay);
	overlay = NULL;
    }
*/
}

void CloseHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int fd,i;

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Close\n");
    if(pMxv->OvlHW != NULL){
	OvlHWPtr overlay = pMxv->OvlHW;
	if(overlay->fd_IPP > 0)
	    close(overlay->fd_IPP);
	if(overlay->fd_RGA > 0)
	    close(overlay->fd_RGA);

	OvlReset(pScrn);
	OvlEnable(pScrn, FBO1, 0);
	OvlEnable(pScrn, FBO2, 0);

	while(overlay->OvlMemPgs>0){
	    overlay->OvlMemPgs--;
	    OvlUnMapBufMem(pScrn, &overlay->OvlMemPg[overlay->OvlMemPgs]);
	}
	for(i=0;i<OVLs;i++)
	    if(overlay->OvlFb[i].fd>0)
		close(overlay->OvlFb[i].fd);
        free(overlay);
	overlay = NULL;
    }
}
