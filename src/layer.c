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
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86xv.h"
#include "os.h"
#include "fb.h"

#include "layer.h"
#include "rk3066.c"
#include "fbdev_priv.h"
#include "mali_dri2.h"
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>

#define HW_TIMEOUT 100

#define FbByLay(layout) (overlay->OvlLay[layout].OvlFb)
#define MBufByLay(layout) (FbByLay(layout)->CurMemPg)

#define LayIsUIfb(layout)	(FbByLay(layout)->Type == UIL)

#ifdef DEBUG
#define OVLDBG(format, args...)		{if(overlay->debug) WRNMSG(format, ## args);}
#else
#define OVLDBG(format, args...)
#endif

#ifdef DEBUGRGA
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

//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------

OvlMemPgPtr OvlGetBufByLay(ScrnInfoPtr pScrn, OvlLayPg layout, OvlFbBufType BufType)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout < MAX_OVERLAYs && layout >= 0)
    	return overlay->OvlLay[layout].FbMemPgs[BufType];
//    	return MBufByLay(layout);
    else
    	return NULL;
}

//--------------------------------------------------------------------------------
int OvlGetVXresByLay(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout < MAX_OVERLAYs && layout >= 0)
	return overlay->OvlLay[layout].var.xres_virtual;
    else
	return -1;
}
#ifdef IPP_ENABLE
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
	    OVLDBG("Timeout ipp");
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
#endif
#ifdef RGA_ENABLE
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
    		OVLDBG("Timeout rga");
    		return -EBUSY;
    	}
    	usleep(1);
    }
    ret = ioctl(overlay->fd_RGA, syncmode, RGA_req);
    pthread_mutex_unlock(&overlay->rgamutex);
    return ret;
}
//------------------------------------------------------------------------------
void ovlRgaInitReg(ScrnInfoPtr pScrn, struct rga_req *RGA_req, uint32_t SrcYAddr, int SrcFrmt, int DstFrmt,
		uint32_t DstYAddr, int Src_x, int Src_y, int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir, Bool PhyAdr)
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
    RGA_req->src.x_offset = Src_x;
    RGA_req->src.y_offset = Src_y;
//Dst
    RGA_req->dst.vir_w = Dst_vir;
    RGA_req->dst.vir_h = RGA_req->src.vir_h;
    RGA_req->dst.x_offset = Drw_x;
    RGA_req->dst.y_offset = Drw_y;
    RGA_req->dst.act_w = RGA_req->src.act_w;
    RGA_req->dst.act_h = RGA_req->src.act_h;//1/2 

    RGA_req->dst.format = DstFrmt;
    RGA_req->dst.yrgb_addr = DstYAddr;

    RGA_req->clip.xmax = Dst_vir-1;
    RGA_req->clip.ymax = overlay->cur_var.yres-1;

//    RGA_req->src_trans_mode = 1;

    if(!PhyAdr){
    	RGA_req->mmu_info.mmu_en = 1;
    	RGA_req->mmu_info.mmu_flag = 0b100001;  /* [0] mmu enable [1] src_flush [2] dst_flush [3] CMD_flush [4~5] page size*/
    }
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
//---------------------------------------------------------------
static int ovlBppToRga(int bpp)//BIT per pixel
{
	int ret=-EINVAL;

	switch(bpp){
	case 1:
		ret = RK_FORMAT_BPP1;
		break;
	case 2:
		ret = RK_FORMAT_BPP2;
		break;
	case 4:
		ret = RK_FORMAT_BPP4;
		break;
	case 8:
		ret = RK_FORMAT_BPP8;
		break;
	case 16:
		ret = RK_FORMAT_RGB_565;
		break;
	case 24:
		ret = RK_FORMAT_RGB_888;
		break;
	case 32:
		ret = RK_FORMAT_RGBX_8888;
		break;

	}
	return ret;
}
//--------------------------------------------------------------------------------
int Ovl2dBlt(ScrnInfoPtr pScrn, uint32_t *src_bits, uint32_t *dst_bits, int src_stride, int dst_stride,
		int src_bpp, int dst_bpp, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int SrcFrmt, DstFrmt, ret;

    SrcFrmt = ovlBppToRga(src_bpp);
    DstFrmt = ovlBppToRga(dst_bpp);
	ovlRgaInitReg(pScrn, &overlay->OvlLay[UIL].RGA_req, (uint32_t)src_bits, SrcFrmt, DstFrmt,
			(uint32_t)dst_bits, src_x, src_y, w, h, dst_x, dst_y, src_stride, dst_stride, FALSE);

	ret = ovlRgaBlit(pScrn, &overlay->OvlLay[UIL].RGA_req, RGA_BLIT_SYNC);
	if(ret < 0)
		OVLDBG("rga ret:%d",ret);
	OVLDBG("\n src_x:%d, src_y:%d, w:%d, h:%d, dst_x:%d, dst_y:%d, src_stride:%d, dst_stride:%d",src_x, src_y, w, h, dst_x, dst_y, src_stride, dst_stride);
	return ret;
}
//--------------------------------------------------------------------------------
int OvlCpBufToDisp(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    OvlLayPg t;

    OVLDBG("OvlCpBufToDisp OvlPg:%d\n",layout);
//	overlay->OvlLay[layout].ResChange = FALSE;
    if(layout < MAX_OVERLAYs && layout >= 0){
    	overlay->OvlLay[layout].RGA_req.src.yrgb_addr = PMemPg->phy_addr;
    	return ovlRgaBlit(pScrn, &overlay->OvlLay[layout].RGA_req, RGA_BLIT_SYNC);
    }
    OVLDBG("OvlCpBufToDisp Error");
    return -1;
}
#endif
//++++++++++++++++++++++++++++++++++++USI++++++++++++++++++++++++++++++++++++++++++++
static int ovlInitUSIHW(ScrnInfoPtr pScrn)
{
    return open(FB_DEV_USI, O_RDWR);
}
//-------------------------------------------------------------
static int ovlUSIAllocMem(ScrnInfoPtr pScrn, struct usi_ump_mbs *uum)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
//    int ret;

    if(uum->size < USI_MIN_ALLOC_SIZE)
    	return -EINVAL;
    return ioctl(overlay->fd_USI, USI_ALLOC_MEM_BLK, uum);
}
//-------------------------------------------------------------
static int ovlUSIFreeMem(ScrnInfoPtr pScrn, ump_secure_id	secure_id)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
//    int ret;

    return ioctl(overlay->fd_USI, USI_FREE_MEM_BLK, &secure_id);
}
//-------------------------------------------------------------
static int ovlUSIGetStat(ScrnInfoPtr pScrn, struct usi_ump_mbs_info *uumi)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
//    int ret;
//    struct usi_ump_mbs uum;

    return ioctl(overlay->fd_USI, USI_GET_INFO, uumi);
}
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
static OvlMemPgPtr ovlInitMemPgDef(ScrnInfoPtr pScrn)
{
	OvlMemPgPtr		FbMemPg;

	if(!(FbMemPg = calloc(1, sizeof(*FbMemPg) )))
		return NULL;

	//calloc =( malloc + zero) - there is no need set pointers to null
	FbMemPg->ump_fb_secure_id = UMP_INVALID_SECURE_ID;
	FbMemPg->ump_handle = UMP_INVALID_MEMORY_HANDLE;
	FbMemPg->MemPgType = BUF_MEM;//by def

	return FbMemPg;
}
//-----------------------------------------------------------------------------------
#ifdef RGA_ENABLE
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
    if(layout >= 0 && layout < MAX_OVERLAYs){
    	if(Src){
#ifdef IPP_ENABLE
    		overlay->OvlLay[layout].IPP_req.src0.fmt = IPP_mode;
#endif
    		overlay->OvlLay[layout].RGA_req.src.format = RGA_mode;
	}
	else{
	    overlay->OvlLay[layout].RGA_req.dst.format = RGA_mode;
	}
#ifdef IPP_ENABLE
    	overlay->OvlLay[layout].IPP_req.dst0.fmt = overlay->OvlLay[layout].IPP_req.src0.fmt;
#endif
    }
}
#endif
//*******************************************************************************
/*int OvlPanBufSync(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout)
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
}*/
//--------------------------------------------------------------------------------
int OvlGetUIBpp(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    switch(overlay->cur_var.nonstd){
    case RGB_565:
    	ret = 16;
    	break;
    case RGB_888:
    	ret = 24;
    	break;
    default:
    	ret = 32;
    }

    return ret;
}
//--------------------------------------------------------------------------------
int OvlSetColorKey(ScrnInfoPtr pScrn, uint32_t color)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    return ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOSET_COLORKEY, &color);
}
//------------------------------------------------------------------
/*int OvlWaitSync(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    uint32_t tmp=0;

    if(!pMxv->WaitForSync)
	return -1;

    return ioctl(FbByLay(layout)->fd, FBIO_WAITFORVSYNC, &tmp);
}*/
//--------------------------------------------------------------------------------
int ovlclearbuf(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(PMemPg == NULL || PMemPg->fb_mmap == NULL || PMemPg->MemPgType == UIFB_MEM/* !UI fb*/)
    	return -ENODEV;
	memset(PMemPg->fb_mmap,0,PMemPg->buf_size);
	return 0;
}
//------------------------------------------------------------
int OvlUnMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    int ret = -ENODEV;

    if(PMemPg != NULL && PMemPg->fb_mmap != NULL){
    	ovlclearbuf(pScrn, PMemPg);
    	if(PMemPg->MemPgType == UIFB_MEM){
    		ret = fbdevHWUnmapVidmem(pScrn);
				//munmap(PMemPg->fb_mmap, PMemPg->buf_size);
    	}
    	else{
    		if( PMemPg->ump_handle != UMP_INVALID_MEMORY_HANDLE){
    			ump_mapped_pointer_release(PMemPg->ump_handle);
    			ret = 0;
    		}
    	}
   		PMemPg->fb_mmap = NULL;
    }
    return ret;
}
//----------------------------------------------------------------------
void *OvlMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    if(PMemPg != NULL){
    	if(PMemPg->fb_mmap == NULL){
    		if(PMemPg->MemPgType == UIFB_MEM){
    			PMemPg->fb_mmap =fbdevHWMapVidmem(pScrn);
        				//mmap( NULL, overlay->MaxPgSize, PROT_READ | PROT_WRITE, MAP_SHARED, overlay->OvlFb[UserInterfaceFB].fd, 0);
//        		if(PMemPg->fb_mmap == MAP_FAILED)
//        			PMemPg->fb_mmap = NULL;
        	}else{
        		if( PMemPg->ump_fb_secure_id == UMP_INVALID_SECURE_ID)
        			return NULL;
        		PMemPg->ump_handle = ump_handle_create_from_secure_id(PMemPg->ump_fb_secure_id);
        		if(PMemPg->ump_handle == UMP_INVALID_MEMORY_HANDLE)
        			return NULL;
        		PMemPg->fb_mmap = ump_mapped_pointer_get(PMemPg->ump_handle);
        	}
    		ovlclearbuf(pScrn, PMemPg);
//    		return PMemPg->fb_mmap;
    	}
   		return PMemPg->fb_mmap;
    }
    return NULL;
}

//--------------------------------------------------------------------
int OvlSetModeFb(ScrnInfoPtr pScrn, OvlLayPg layout, unsigned short xres, unsigned short yres, unsigned char mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
//    OvlFbPg FbPg = FbByLay(layout);
    int ret=0;

    if(layout >= MAX_OVERLAYs || layout < 0)
    	return -ENODEV;
    if(!LayIsUIfb(layout)){/*TODO !UIL*/
    	if((xres > overlay->OvlLay[layout].var.xres_virtual)||(yres > overlay->OvlLay[layout].var.yres_virtual)) return -EINVAL;
//    if((xres > overlay->cur_var.xres)||(yres > overlay->cur_var.yres)) return -1;
    	if(mode>0)
    		overlay->OvlLay[layout].var.nonstd = mode;
    	if(xres>0)
    		overlay->OvlLay[layout].var.xres = xres;
    	if(yres>0)
    		overlay->OvlLay[layout].var.yres = yres;
    	ret = ioctl(FbByLay(layout)->fd, FBIOPUT_VSCREENINFO, &overlay->OvlLay[layout].var);

/*    	if(ret == 0){
    		ovlSelHwMods(pScrn, overlay->OvlLay[layout].var.nonstd, layout, DST_MODE);
//    		overlay->OvlLay[layout].ResChange = FALSE;
    	}*/
    }
/*    else
    	ovlSelHwMods(pScrn, overlay->cur_var.nonstd, layout, DST_MODE);
*/
    return ret;
}
//--------------------------------------------------------------------------------
static Bool ovlUpdVarOnChangeRes(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

//    if(overlay->OvlLay[layout].ResChange){
	memcpy(&overlay->OvlLay[layout].var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));
//	overlay->OvlLay[layout].ResChange = FALSE;
	return TRUE;
//    }
    return FALSE;
}
//----------------------------------------------------------------------------------
int OvlSetupFb(ScrnInfoPtr pScrn, OvlLayPg layout, int SrcFrmt, int DstFrmt, unsigned short xres, unsigned short yres)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    if(layout >= MAX_OVERLAYs || layout < 0)
    	return -1;
    ovlUpdVarOnChangeRes(pScrn, layout);
    ret = OvlSetModeFb(pScrn, layout, xres , yres, DstFrmt);
//    if(!ret){
//    	ovlRgaInitReg(pScrn, &overlay->OvlLay[layout].RGA_req, /*SrcYAddr*/0, 0, 0,
//    		FbByLay(layout)->CurMemPg->phy_addr, 0, 0, 0, 0, 0, 0, overlay->OvlLay[layout].var.xres_virtual/*TODO SRC*/, overlay->OvlLay[layout].var.xres_virtual, TRUE);
/*    	if(!SrcFrmt)
    		SrcFrmt = overlay->OvlLay[layout].var.nonstd;
    	ovlSelHwMods(pScrn, SrcFrmt, layout, SRC_MODE);
    }*/
    return ret;
}
//--------------------------------------------------------------------------------
/*int OvlSetupBufDrw(ScrnInfoPtr pScrn, OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int SrcPitch)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    SFbioDispSet pt;
    OvlFbPg FbPg;

    if(layout >= MAX_OVERLAYs || layout < 0)
	return -1;
    if(SrcPitch){
    	ovlRgaDrwAdd(pScrn, &overlay->OvlLay[layout].RGA_req, Drw_w, Drw_h, Drw_x, Drw_y, SrcPitch);
    	return 0;
    }
    return -1;
//    OvlClearBuf(pScrn, overlay->OvlFb[overlay->OvlLay[layout].OvlFb].OvlMemPg);
}*/
//--------------------------------------------------------------------------------
int OvlSetupDrw(ScrnInfoPtr pScrn, OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int Src_w, int Src_h)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    SFbioDispSet pt;
    int ret=0;
//    OvlFbPg FbPg;

    if(layout >= MAX_OVERLAYs || layout < 0)
	return -1;

    pt.poffset_x = Drw_x;
    pt.poffset_y = Drw_y;
    pt.ssize_w = Src_w;
    pt.ssize_h = Src_h;
//    pt.scale_w = (Src_w*PANEL_SIZE_X)/Drw_w;
    pt.scale_w = (Src_w*overlay->OvlLay[layout].var.xres)/Drw_w;
//    pt.scale_h = (Src_h*PANEL_SIZE_Y)/Drw_h;
    pt.scale_h = (Src_h*overlay->OvlLay[layout].var.yres)/Drw_h;
    ret = ioctl(FbByLay(layout)->fd, FBIOSET_DISP_PSET, &pt);
//    OvlClearBuf(pScrn, overlay->OvlFb[overlay->OvlLay[layout].OvlFb].OvlMemPg);
    return ret;
}
//----------------------------------------------------------------------------------
int OvlFbLinkMemPg(ScrnInfoPtr pScrn, OvlFbPtr pfb, OvlMemPgPtr MemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;
    unsigned long tmp[2];

    if(!pfb)
    	return -EINVAL;

	tmp[0] = MemPg->phy_addr;
	tmp[1] = tmp[0] + MemPg->offset_mio;
    ret = ioctl(pfb->fd, RK_FBIOSET_YUV_ADDR, &tmp);
    if(!ret)
    	pfb->CurMemPg = MemPg;
	return ret;
}
//----------------------------------------------------------------------------------
int OvlFlipFb(ScrnInfoPtr pScrn, OvlLayPg layout, OvlFbBufType flip, Bool clrPrev)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret, prev=-1;

    if(layout >= MAX_OVERLAYs || layout < 0)
    	return -ENODEV;
    if(LayIsUIfb(layout))/*TODO !UIL*/
    	return -EINVAL;

    switch(flip){
    case FRONT_FB:
    case BACK_FB:
    	overlay->OvlLay[layout].FbBufUsed = flip;
    	break;
    case NEXT_FB:
    default:
    	prev = overlay->OvlLay[layout].FbBufUsed;
    	if(overlay->OvlLay[layout].FbBufUsed == FRONT_FB)
    		overlay->OvlLay[layout].FbBufUsed = BACK_FB;
    	else
    		overlay->OvlLay[layout].FbBufUsed = FRONT_FB;

    }
    ret = OvlFbLinkMemPg(pScrn, FbByLay(layout) , overlay->OvlLay[layout].FbMemPgs[overlay->OvlLay[layout].FbBufUsed]);

    if(clrPrev && prev >=FRONT_FB)
    	ovlclearbuf(pScrn, overlay->OvlLay[layout].FbMemPgs[prev]);
//TODO sync ?
    return ret;
}

//---------------------------------------------------------------------
int OvlEnable(ScrnInfoPtr pScrn, OvlLayPg layout, int enable)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(layout >= MAX_OVERLAYs || layout < 0)
    	return -ENODEV;
    if(LayIsUIfb(layout))/*TODO !UIL*/
    	return -EINVAL;
    
    return ioctl(FbByLay(layout)->fd, RK_FBIOSET_ENABLE, &enable);
}
//---------------------------------------------------------------------
int OvlResetFB(ScrnInfoPtr pScrn, OvlLayPg layout)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    if(layout >= MAX_OVERLAYs || layout < 0)
	return -1;
//    OvlClearBuf(pScrn, 1);
    ret = ioctl(FbByLay(layout)->fd, FBIOPUT_VSCREENINFO, &overlay->sav_var);
//    if(ret == 0 && dev == FBUI) //TODO res change by x func
//	ret =  OvlUpdSavMod(pScrn);
    return ret;
}
//------------------------------------------------------------------
OvlMemPgPtr OvlAllocMemPg(ScrnInfoPtr pScrn, unsigned long size)//except UI
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    OvlMemPgPtr MemPg;
    struct usi_ump_mbs uum;

    MemPg = ovlInitMemPgDef(pScrn);
    if(MemPg){
    	uum.size = size;
    	if(!ovlUSIAllocMem(pScrn, &uum)){
			MemPg->buf_size = uum.size;
			MemPg->phy_addr = uum.addr;
			MemPg->ump_fb_secure_id = uum.secure_id;
//			MemPg->ump_handle = uum.umh;
			MemPg->offset_mio = ((MemPg->buf_size / 2) & ~PAGE_MASK);
    	}else{
    		MFREE(MemPg);
    	}
    }
    return MemPg;
}
//------------------------------------------------------------------
int OvlFreeMemPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    int ret=0;

    if(PMemPg){
    	ret = OvlUnMapBufMem( pScrn, PMemPg);
    	if(PMemPg->MemPgType != UIFB_MEM)
    		ret |= ovlUSIFreeMem(pScrn, PMemPg->ump_fb_secure_id);
    	MFREE(PMemPg);
    }
    return ret;
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
OvlLayPg OvlAllocLay(ScrnInfoPtr pScrn, OvlLayoutType type, OvlFbBufAllocType FbBufAlloc)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    OvlLayPg i;
    int lay=ERRORL;

    switch(type){
    case UIL:
    case SCALEL:
//    case NOT_SCALEL:
    	for(i=0;i<MAX_OVERLAYs;i++){
    		if(FbByLay(i)->Type == type){
    			if(!overlay->OvlLay[i].InUse){
    				lay = i;
    				break;
    			}
/*    			else{
//		    t = ovlSwapLay(pScrn, i, type);
//		    if(t==ERRORL)
    				lay = ERRORL;
    			}*/
    		}
    	}
    	if(lay == MAX_OVERLAYs)
    		lay = ERRORL;
    	break;
    case ANYL://except UIL
    	for(i=1;i<MAX_OVERLAYs;i++){
    		if(!overlay->OvlLay[i].InUse){
    			lay = i;
    		}
    	}
/*    	break;
    default:
    	lay = ERRORL;*/
    }
    if(lay != ERRORL){
    	if(FbBufAlloc > ALC_NONE_FB){
//front fb first by def
    		if(!overlay->OvlLay[lay].FbMemPgs[FRONT_FB]){
        		if(lay == UIL){//User Interface layer
        			overlay->OvlLay[lay].FbMemPgs[FRONT_FB] = ovlInitMemPgDef(pScrn);
        			if(overlay->OvlLay[lay].FbMemPgs[FRONT_FB]){
        				overlay->OvlLay[lay].FbMemPgs[FRONT_FB]->phy_addr = overlay->OvlFb[UIL].fix.smem_start;
        				overlay->OvlLay[lay].FbMemPgs[FRONT_FB]->buf_size = overlay->OvlFb[UIL].fix.smem_len;
        				overlay->OvlLay[lay].FbMemPgs[FRONT_FB]->MemPgType = UIFB_MEM;
        			}
        		}else{
        			overlay->OvlLay[lay].FbMemPgs[FRONT_FB] = OvlAllocMemPg(pScrn, FB_MAXPGSIZE);//TODO size
        			if(overlay->OvlLay[lay].FbMemPgs[FRONT_FB])
        				overlay->OvlLay[lay].FbMemPgs[FRONT_FB]->MemPgType = FB_MEM;
        		}
    		}
    		if(!overlay->OvlLay[lay].FbMemPgs[FRONT_FB])
    			return ERRORL;

    		OvlFlipFb(pScrn, lay, FRONT_FB, 0);
//and back fb if needed
    		if(FbBufAlloc > ALC_FRONT_FB && !overlay->OvlLay[lay].FbMemPgs[BACK_FB]){
    			overlay->OvlLay[lay].FbMemPgs[BACK_FB] = OvlAllocMemPg(pScrn, FB_MAXPGSIZE);//TODO size
        		if(overlay->OvlLay[lay].FbMemPgs[BACK_FB])
        			overlay->OvlLay[lay].FbMemPgs[BACK_FB]->MemPgType = FB_MEM;
        		else{
        			OvlFreeMemPg(pScrn, overlay->OvlLay[lay].FbMemPgs[FRONT_FB]);
        			overlay->OvlLay[lay].FbMemPgs[FRONT_FB] = NULL;
        			return ERRORL;
        		}
    		}
    	}

		overlay->OvlLay[lay].InUse = TRUE;
		overlay->OvlLay[lay].ReqType = type;
    }

    return lay;
}
//------------------------------------------------------------------
void OvlFreeLay(ScrnInfoPtr pScrn, OvlLayPg layout)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;

    if(layout < MAX_OVERLAYs && layout >= 0){
    	OvlFreeMemPg( pScrn, overlay->OvlLay[layout].FbMemPgs[FRONT_FB]);
    	overlay->OvlLay[layout].FbMemPgs[FRONT_FB] = NULL;
    	OvlFreeMemPg( pScrn, overlay->OvlLay[layout].FbMemPgs[BACK_FB]);
    	overlay->OvlLay[layout].FbMemPgs[BACK_FB] = NULL;
    	MBufByLay(layout) = NULL;
    	OvlEnable(pScrn, layout, 0);
    	overlay->OvlLay[layout].InUse = FALSE;
    	overlay->OvlLay[layout].ReqType = ERRORL;
    }
}
//------------------------------------------------------------------
/*ump_secure_id ovlGetUmpId(ScrnInfoPtr pScrn, OvlMemPg pg)
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
*/
void OvlFillKeyHelper(DrawablePtr pDraw, unsigned int ColorKey, RegionPtr pRegion, Bool DrwOffset)
{
    ScreenPtr pScreen = pDraw->pScreen;
    GCPtr pGC;
	pGC = GetScratchGC(pDraw->depth, pScreen);
    ChangeGCVal pval[2];
    xRectangle *rects;
	BoxPtr pbox = RegionRects(pRegion);
	int i, nbox = RegionNumRects(pRegion);

	pval[0].val = ColorKey;
	pval[1].val = IncludeInferiors;
	(void) ChangeGC(NullClient, pGC, GCForeground|GCSubwindowMode, pval);
	ValidateGC(pDraw, pGC);
	rects = malloc( nbox * sizeof(xRectangle));
	for(i = 0; i < nbox; i++, pbox++)
	{
		rects[i].x = pbox->x1;
		rects[i].y = pbox->y1;
		if(DrwOffset){
			rects[i].x -= pDraw->x;
			rects[i].y -= pDraw->y;
		}
		rects[i].width = pbox->x2 - pbox->x1 + 1;
		rects[i].height = pbox->y2 - pbox->y1 + 1;
	}
	(*pGC->ops->PolyFillRect)(pDraw, pGC, nbox, rects);
	MFREE(rects);
	FreeScratchGC(pGC);
}
//++++++++++++++++++++++++++++++init/close+++++++++++++++++++++++++
int OvlUpdSavMod(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int ret=-1,tmp;
    OvlLayPg i;
    FILE *fp;

    INFMSG("***Res change***");
    if(pMxv->OvlHW != NULL){
    	OvlHWPtr overlay = pMxv->OvlHW;
    	ret = ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOGET_VSCREENINFO, &overlay->cur_var);

//	tmp = resToHDMImodes(overlay->cur_var.xres,overlay->cur_var.yres);
//	ioctl(overlay->OvlFb[UserInterfaceFB].fd, FBIOSET_HDMI_MODE, &tmp);//use HDMI scaling

    	fp = fopen(FB_SYS_HDMI"/mode", "w");
    	if(fp){
    		fprintf(fp,HDMI_MODE_TMPL"\n",1280,720);//bug workarround
			fclose(fp);
    	}
    	usleep(100000);
    	fp = fopen(FB_SYS_HDMI"/mode", "w");
    	if(fp){
    		fprintf(fp,HDMI_MODE_TMPL"\n",1920,1080);
			fclose(fp);
    	}
   		usleep(100000);
    	fp = fopen(FB_SYS_HDMI"/mode", "w");
    	if(fp){
    		fprintf(fp,HDMI_MODE_TMPL"\n",overlay->cur_var.xres,overlay->cur_var.yres);
			fclose(fp);
    	}else
    		ERRMSG("Do not open "FB_SYS_HDMI"/mode");

    	fp = fopen(FB_SYS_HDMI"/scale", "w");
    	if(fp){
    		fprintf(fp,"scalex=100");
    		fprintf(fp,"scaley=100");
			fclose(fp);
    	}
    	overlay->cur_var.vmode |= FB_VMODE_CONUPDATE;
		overlay->cur_var.activate = FB_ACTIVATE_NOW;
		overlay->cur_var.grayscale = 0;
//    	for(i=0;i<MAX_OVERLAYs;i++)
//    		overlay->OvlLay[i].ResChange = TRUE;

    	overlay->ResChange = TRUE;
    	INFMSG("Resolution changed to %dx%d,  ret=%d ***", overlay->cur_var.xres, overlay->cur_var.yres, ret);
    }
    return ret;
}

//----------------------------------------------------------------------
void set_ovl_param(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    uint32_t yuv_phy[2],ll;
    int i;

    for(i=0;i<MAX_OVERLAYs;i++){
    	ioctl(overlay->OvlFb[i].fd, FBIOGET_FSCREENINFO, &overlay->OvlFb[i].fix);
    	overlay->OvlFb[i].CurMemPg = NULL;

    	overlay->OvlLay[i].OvlFb = &overlay->OvlFb[i];
    	memcpy(&overlay->OvlLay[i].var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));
    	overlay->OvlLay[i].InUse = FALSE;
    	overlay->OvlLay[i].ReqType = ERRORL;
//    	overlay->OvlLay[i].ResChange = FALSE;
    	overlay->OvlLay[i].FbBufUsed = FRONT_FB;
    	overlay->OvlLay[i].FbMemPgs[FRONT_FB] = NULL;
    	overlay->OvlLay[i].FbMemPgs[BACK_FB] = NULL;
    	if(i>0){
        	overlay->OvlFb[i].Type = 1;//        UIL=0,    SCALEL=1,    NotSCALEL=2,
    		OvlSetModeFb(pScrn,i,0,0,0);
    		OvlEnable(pScrn, i, 0);
    	}else
        	overlay->OvlFb[i].Type = 0;
    }

}

//------------------------------------------------------------------
Bool ovl_setup_ovl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int tmp,fd;

    overlay->OvlFb[MasterOvlFB].fd = open(FB_DEV_O1, O_RDONLY); //main overlay
    if (overlay->OvlFb[MasterOvlFB].fd < 0) goto err;
    overlay->OvlFb[UserInterfaceFB].fd = fbdevHWGetFD(pScrn);//open(FB_DEV_UI, O_RDONLY);
    if (overlay->OvlFb[UserInterfaceFB].fd < 0) goto err1;
    overlay->OvlFb[SecondOvlFB].fd = open(FB_DEV_O2, O_RDONLY);
    if (overlay->OvlFb[SecondOvlFB].fd < 0) goto err2;

    overlay->MaxPgSize = FB_MAXPGSIZE;

    if(OvlUpdSavMod(pScrn) == 0){
	memcpy(&overlay->sav_var, &overlay->cur_var, sizeof(struct fb_var_screeninfo));

//	SelHWMods(pScrn, overlay->var.nonstd);
//	ioctl(overlay->fd_o1, FBIOBLANK, FB_BLANK_UNBLANK);

	tmp=1;
	ioctl(overlay->OvlFb[UserInterfaceFB].fd, RK_FBIOSET_OVERLAY_STATE, &tmp);

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
//-------------------------------------------------------
Bool ovl_init_ovl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int i;
    OvlMemPgPtr	FbMemPg;

    pMxv->OvlHW = NULL;
    if(!(pMxv->OvlHW = calloc(1, sizeof(OvlHWRec) )))
    	goto err;
    OvlHWPtr overlay = pMxv->OvlHW;

    if(!ovl_setup_ovl(pScrn)) goto err1;

    set_ovl_param(pScrn);
#ifdef RGA_ENABLE
    pthread_mutex_init(&overlay->rgamutex, NULL);
#endif
#ifdef IPP_ENABLE
    pthread_mutex_init(&overlay->ippmutex, NULL);
#endif
//    OvlReset(pScrn);//TODO

    return TRUE;
err2:
    for(i=0;i<MAX_OVERLAYs;i++)
        if(overlay->OvlFb[i].fd>0)
        	close(overlay->OvlFb[i].fd);
err1:
    MFREE(overlay);
err:
    return FALSE;
}

//----------------------------main init--------------------------
void InitHWAcl(ScreenPtr pScreen, Bool debug)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay;
    struct usi_ump_mbs_info uumi;

    if (ump_open() != UMP_OK){
    	ERRMSG( "HW:Error open UMP");
    	return;
    }

    pMxv->OvlHW = NULL;
    INFMSG( "HW:Initialize overlays");

    if(!ovl_init_ovl(pScrn)){
    	ERRMSG( "HW:Error overlays");
    	return;
    }

    overlay = pMxv->OvlHW;
#ifdef IPP_ENABLE
    INFMSG( "HW:Initialize IPP");
    if (!xf86LoadKernelModule("rk29-ipp"))
    	INFMSG( "can't load 'rk29-ipp' kernel module");
    overlay->fd_IPP = ovlInitIPPHW(pScrn);
    if(overlay->fd_IPP <= 0){
	ERRMSG( "HW:Error IPP");
    }
#endif
#ifdef RGA_ENABLE
    INFMSG( "HW:Initialize RGA");
    overlay->fd_RGA = ovlInitRGAHW(pScrn);
    if(overlay->fd_RGA <= 0){
	ERRMSG( "HW:Error RGA");
    }
#endif
    INFMSG( "HW:Initialize USI");
    if (!xf86LoadKernelModule("rk30_ump"))
    	INFMSG("can't load usi_ump kernel module");
    overlay->fd_USI = ovlInitUSIHW(pScrn);
    if(overlay->fd_USI <= 0){
	ERRMSG( "HW:Error USI");
    }else{
    	ovlUSIGetStat(pScrn, &uumi);
    	INFMSG("UMI buffer size:%d Mb used:%d Mb",uumi.size_full/(1024*1024), uumi.size_used/(1024*1024));
    	if(uumi.size_used){
    		INFMSG("UMI will be freed");
    		ioctl(overlay->fd_USI, USI_FREE_ALL_BLKS);
    	}
    }
    overlay->debug = debug;
    return;
}

void CloseHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int fd,i;
    OvlMemPgPtr	MemPg;

    INFMSG("HW:Close");
    if(pMxv->OvlHW != NULL){
    	OvlHWPtr overlay = pMxv->OvlHW;
#ifdef IPP_ENABLE
    	if(overlay->fd_IPP > 0)
    		close(overlay->fd_IPP);
#endif
#ifdef RGA_ENABLE
    	if(overlay->fd_RGA > 0)
    		close(overlay->fd_RGA);
#endif
    	for(i=0;i<MAX_OVERLAYs;i++){
    		OvlFreeLay(pScrn, i);
    		if(overlay->OvlFb[i].fd > 0 && i > 0){//except main
    			OvlEnable(pScrn, i, 0);
    			close(overlay->OvlFb[i].fd);
    		}
    	}
    	if(overlay->fd_USI > 0)
    		close(overlay->fd_USI);
    	MFREE(overlay);
        ump_close();
    }
}
