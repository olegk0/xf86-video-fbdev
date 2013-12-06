/*
 *  For rk3066 with the modified kernel
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
#include "xf86.h"
#include "xf86xv.h"
#include "os.h"

#include "layer.h"
#include "rk3066.h"
#include "fbdev_priv.h"
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>

#define HW_TIMEOUT 100

#ifdef DEBUG
#define OVLDBG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, format, ## args)
#else
#define OVLDBG(format, args...)
#endif

//++++++++++++++++++++++++++++++++++++IPP++++++++++++++++++++++++++++++++++++++++++
static int InitIPPHW(ScrnInfoPtr pScrn)
{
    return open("/dev/rk29-ipp", O_RDWR);
}
//-------------------------------------------------------------
int IppBlit(ScrnInfoPtr pScrn, struct rk29_ipp_req *ipp_req)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    return ioctl(overlay->fd_IPP, IPP_BLIT_SYNC, ipp_req);
}
//++++++++++++++++++++++++++++++++++++RGA++++++++++++++++++++++++++++++++++++++++++
static int InitRGAHW(ScrnInfoPtr pScrn)
{
    return open("/dev/rga", O_RDWR);
}
//-------------------------------------------------------------
int RgaBlit(ScrnInfoPtr pScrn, struct rga_req *RGA_req, int syncmode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

//    OvlSync(pScrn);
    return  ioctl(overlay->fd_RGA, syncmode, RGA_req);
}
//-------------------
void OvlRGAUnlock(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    overlay->rga_pa = 0;
}
//------------------------------------------------------------------------------
/*int RgaFillReg(ScrnInfoPtr pScrn, RegionPtr clipBoxes, CARD32 yrgb_addr, CARD32 v_addr, CARD32 v_addr
	CARD32 vir_w, CARD32 vir_w, CARD32 format, CARD32, CARD32 , CARD32 color)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    RGAHWPtr RGAHW = pMxv->RGAHW;
    struct rga_req *RGA_req;

    RGA_req = &pMxv->RGAHW;

    memset(RGA_req, 0, sizeof(struct rga_req));
clipBoxes->extents.x1,clipBoxes->extents.x2,clipBoxes->extents.y1,clipBoxes->extents.y2
//Dst
    RGA_req->dst.yrgb_addr = yrgb_addr;
    RGA_req->dst.uv_addr  = uv_addr;
    RGA_req->dst.v_addr   = v_addr;
    RGA_req->dst.vir_w = vir_w;
    RGA_req->dst.vir_h = vir_h;
    RGA_req->dst.format = format; 
    RGA_req->clip.xmin = 0;
    RGA_req->clip.xmax = Rga_Request.dst.vir_w - 1;
    RGA_req->clip.ymin = 0;
    RGA_req->clip.ymax = Rga_Request.dst.vir_h - 1;

    RGA_req->dst.act_w = 720/2;
    RGA_req->dst.act_h = 528/2;//1/2 
    
    RGA_req->dst.x_offset = 600;
    RGA_req->dst.y_offset = 100;
//    Rga_Request.sina = 0;
//    Rga_Request.cosa = 0;
//Ctl
    RGA_req->render_mode = color_fill_mode;
//    RGA_req->scale_mode = 0;          // 0 nearst / 1 bilnear / 2 bicubic     
//    RGA_req->rotate_mode = 0;
    return RgaBlit(pScrn, RGA_req, RGA_BLIT_ASYNC);
}
*/
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
static int ovlcopyhwbufchfrmt(ScrnInfoPtr pScrn,
				unsigned int SrcYAddr, unsigned int SrcUVAddr, unsigned int SrcVAddr,
				int SrcFrmt, int DstFrmt, unsigned int DstYAddr,
				int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    struct rga_req RGA_req;
    int ret, timeout = 0;

    while(pthread_mutex_trylock(&overlay->rgamutex) ==  EBUSY){
	timeout++;
	if(timeout > HW_TIMEOUT) return -1;
    }

    memset(&RGA_req, 0, sizeof(struct rga_req));
//Src
    RGA_req.src.format = SrcFrmt;//    = 0x1,;
    RGA_req.src.act_w = Drw_w;
    RGA_req.src.act_h = Drw_h;

    RGA_req.src.yrgb_addr = SrcYAddr;
    RGA_req.src.uv_addr  = SrcUVAddr;
    RGA_req.src.v_addr   = SrcVAddr;

    RGA_req.src.vir_w = Src_vir;
    RGA_req.src.vir_h = overlay->var.yres_virtual;
//Dst
    RGA_req.dst.vir_w = Dst_vir;
    RGA_req.dst.vir_h = overlay->var.yres;
    RGA_req.dst.x_offset = Drw_x;
    RGA_req.dst.y_offset = Drw_y;
    RGA_req.dst.act_w = RGA_req.src.act_w;
    RGA_req.dst.act_h = RGA_req.src.act_h;//1/2 

    RGA_req.dst.format = DstFrmt;
    RGA_req.dst.yrgb_addr = DstYAddr;

    RGA_req.clip.xmax = overlay->var.xres-1;
    RGA_req.clip.ymax = overlay->var.yres-1;

    ret =  RgaBlit(pScrn, &RGA_req, RGA_BLIT_SYNC);
    pthread_mutex_unlock(&overlay->rgamutex);
    return ret;
}
//--------------------------------------------------------------------------------
static int ovlcopyhwbufscale(ScrnInfoPtr pScrn,
				unsigned int SrcYAddr, unsigned int SrcUVAddr, int SrcFrmt,
				unsigned int DstYAddr, unsigned int DstUVAddr,
				int Src_w, int Src_h, int Drw_w, int Drw_h, int Src_vir, int Dst_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    struct rk29_ipp_req ipp_req;
    int ret, timeout = 0;

    while(pthread_mutex_trylock(&overlay->ippmutex) ==  EBUSY){
	timeout++;
	if(timeout > HW_TIMEOUT) return -1;
    }

    memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));

    ipp_req.src0.w = Src_w;
    ipp_req.src0.h = Src_h;
    ipp_req.src_vir_w = Src_vir;

//IPP_XRGB_8888
    ipp_req.src0.fmt = SrcFrmt;
    ipp_req.dst0.fmt = ipp_req.src0.fmt;
    ipp_req.dst_vir_w = Dst_vir;
    ipp_req.timeout = 100;
    ipp_req.flag = IPP_ROT_0;

    ipp_req.src0.YrgbMst = SrcYAddr;
    ipp_req.src0.CbrMst = SrcUVAddr;
    ipp_req.dst0.YrgbMst = DstYAddr;
    ipp_req.dst0.CbrMst = DstUVAddr;
    ipp_req.dst0.w = ipp_req.src0.w;
    ipp_req.dst0.h = ipp_req.src0.h;

    ret = IppBlit(pScrn, &ipp_req);
    pthread_mutex_unlock(&overlay->ippmutex);
    return ret;
}
//--------------------------------------------------------------------------------
int OvlPutBufToSrcn(ScrnInfoPtr pScrn, unsigned int SrcBuf, int Src_vir,
				int Drw_w, int Drw_h, int Drw_x, int Drw_y, int pa_code)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

//ErrorF("-----Enter===SrcBuf:%X RGA_mode:%d Drw_w:%d Drw_h:%d Drw_x:%d Drw_y:%d Src_vir:%d Dst_vir:%d  pa_l:%d pa_in:%d\n",
//    SrcBuf, overlay->RGA_mode, Drw_w, Drw_h, Drw_x, Drw_y, Src_vir, overlay->var.xres_virtual ,overlay->rga_pa ,pa_code);

    if(overlay->rga_pa == 0){
	overlay->rga_pa = 1;
    }
    else
	if(overlay->rga_pa != pa_code){
	    ovlcopyhwbufscale(pScrn, SrcBuf, 0, overlay->IPP_mode, 
		overlay->phadr_mem[0]+((Drw_y*overlay->var.xres_virtual+Drw_x)<<2), 0,
		Drw_w, Drw_h, Drw_w, Drw_h, Src_vir, overlay->var.xres_virtual);
	    return 0;
	}
    ovlcopyhwbufchfrmt(pScrn, SrcBuf, 0, 0, overlay->RGA_mode, overlay->RGA_mode, 
	overlay->phadr_mem[0], Drw_w, Drw_h, Drw_x, Drw_y, Src_vir, overlay->var.xres_virtual);
    return 1;
}
//--------------------------------------------------------------------------------
int OvlChBufFrmt(ScrnInfoPtr pScrn, unsigned int SrcYAddr, unsigned int SrcUVAddr, unsigned int SrcVAddr,
				int SrcFrmt, int DstFrmt, unsigned int DstYAddr,
				int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if(overlay->rga_pa < 2 ){
	if(overlay->rga_pa == 1 ){
	    overlay->rga_pa = 2;
	    usleep(10);
	}
	else
	    overlay->rga_pa = 2;
    }
    return ovlcopyhwbufchfrmt(pScrn, SrcYAddr, SrcUVAddr, SrcVAddr, SrcFrmt, DstFrmt, 
					    DstYAddr, Drw_w, Drw_h, Drw_x, Drw_y, Src_vir, Dst_vir);
}
//--------------------------------------------------------------------------------
int OvlScaleBuf(ScrnInfoPtr pScrn, unsigned int SrcYAddr, unsigned int SrcUVAddr, int SrcFrmt,
				unsigned int DstYAddr, unsigned int DstUVAddr,
				int Src_w, int Src_h, int Drw_w, int Drw_h, int Src_vir, int Dst_vir)
{
    return ovlcopyhwbufscale(pScrn, SrcYAddr, SrcUVAddr, SrcFrmt, DstYAddr, DstUVAddr,
				Src_w, Src_h, Drw_w, Drw_h, Src_vir, Dst_vir);
}
//+++++++++++++++++++++++++++++++++++Overlay+++++++++++++++++++++++++++++++++++++++++
void OvlFillBuf(CARD32 *buf, unsigned int len, CARD32 color)
{
    unsigned int i;

    for(i = 0;i < len;i++){
	buf[i] = color;
    }
}
//------------------------------------------------------------
void OvlClearBuf(ScrnInfoPtr pScrn, unsigned char pg)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int len, flen;
    CARD32 *buf;

//    memset(overlay->fb_mem[pg], 0, overlay->pg_len);
//    len = overlay->pg_len >> 2;
    buf = (CARD32 *)overlay->fb_mem[pg];
    flen = overlay->pg_len >> 2;
    OvlFillBuf(buf, flen, 0);
//    for(len = 0;len < flen;len++){
//	buf[len] = 0;
//    }
}
//------------------------------------------------------------
static void free_ovl_memory(OvlHWPtr overlay)
{
    if (overlay->fb_mem[0] != NULL){
//        OvlClearBuf(overlay->fb_mem[0], overlay->pg_len<<1);
	if( 0 == munmap(overlay->fb_mem[0], overlay->fix.smem_len))
	    overlay->fb_mem[0] = NULL;
    }
}
//----------------------------------------------------------------------
static Bool alloc_ovl_memory(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr	overlay = pMxv->OvlHW;
    CARD32 rs,ll;

//    rs = (overlay->var.yres * overlay->var.xres) << 2;//max bytes in screen
    rs = (1920 * 1080) << 2;//max bytes in screen
//    overlay->pg_len = (overlay->fix.smem_len >> 2) & ~PAGE_MASK;
    overlay->pg_len = (rs + PAGE_MASK) & ~PAGE_MASK;
    overlay->fb_mem[0] = mmap( NULL, overlay->fix.smem_len, PROT_READ | PROT_WRITE,
					     MAP_SHARED, overlay->fd, 0);
    if ( -1 == (long)overlay->fb_mem[0] ){
        xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "Error mmap fb1_mem, fbmem_len:%X\n",overlay->fix.smem_len);
        overlay->fb_mem[0] = NULL;
	return FALSE;
    }
    ll = overlay->fix.smem_len - overlay->pg_len;
    rs = 1;
    overlay->phadr_mem[0] = overlay->fix.smem_start;
    while(ll >= overlay->pg_len){
	overlay->phadr_mem[rs] = overlay->phadr_mem[rs-1] + overlay->pg_len;
	overlay->fb_mem[rs] = overlay->fb_mem[rs-1] + overlay->pg_len;
        OvlClearBuf(pScrn, rs-1);
	ll -= overlay->pg_len;
	rs++;
    }
    overlay->fb_mio = overlay->fb_mem[0] + ((overlay->pg_len >> 1) & ~PAGE_MASK);
    return TRUE;
}
//---------------------------------------------------------------------
int OvlFlushPg(ScrnInfoPtr pScrn, unsigned char pg, int mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    return msync(pMxv->OvlHW->fb_mem[pg],pMxv->OvlHW->pg_len, mode);
}
//---------------------------------------------------------------------
void SelHWMods(ScrnInfoPtr pScrn, int mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    switch(mode) {
    case RGBX_8888:
    case RGBA_8888:
	overlay->RGA_mode = RK_FORMAT_RGBX_8888;
	overlay->IPP_mode = IPP_XRGB_8888;
	break;
    case RGB_888:
	overlay->RGA_mode = RK_FORMAT_RGB_888;
	overlay->IPP_mode = 0;//TODO: add support to ipp
	break;
    case RGB_565:
	overlay->RGA_mode = RK_FORMAT_RGB_565;
	overlay->IPP_mode = IPP_RGB_565;
	break;
/*    case YCrCb_NV12:
//	inRGAmode = RK_FORMAT_YCbCr_420_P;
        inRGAmode = RK_FORMAT_YCbCr_420_P;
	IPPmode = IPP_Y_CBCR_H2V2;
        break;
    case YCbCr_422_SP:
	inRGAmode = RK_FORMAT_YCrCb_422_SP;
	IPPmode = IPP_Y_CBCR_H2V1;
	break;
    case YCrCb_444:
	break;
*/
    default:
	overlay->RGA_mode = RK_FORMAT_RGBX_8888;
	overlay->IPP_mode = IPP_XRGB_8888;
    }
}
//--------------------------------------------------------------------
int OvlSetMode(ScrnInfoPtr pScrn, unsigned short xres, unsigned short yres, unsigned char mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    if((xres > overlay->saved_var.xres_virtual)||(yres > overlay->saved_var.yres_virtual)) return -1;
//    if((xres > overlay->saved_var.xres)||(yres > overlay->saved_var.yres)) return -1;

    memcpy(&overlay->var, &overlay->saved_var, sizeof(struct fb_var_screeninfo));
    overlay->var.nonstd = mode;
    overlay->var.xres = xres;
    overlay->var.yres = yres;

    ret = ioctl(overlay->fd, FBIOPUT_VSCREENINFO, &overlay->var);
    if(ret == 0)
	SelHWMods(pScrn, overlay->var.nonstd);

    return ret;
}
//---------------------------------------------------------------------
int OvlReset(ScrnInfoPtr pScrn)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    OvlClearBuf(pScrn, 0);
    OvlClearBuf(pScrn, 1);
    OvlSwDisp(pScrn, 0, TRUE);

    return ioctl(overlay->fd, FBIOPUT_VSCREENINFO, &overlay->saved_var);
}
//---------------------------------------------------------------------
int OvlSync(ScrnInfoPtr pScrn)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int tmp = 0;

    return ioctl(overlay->fd, FBIO_WAITFORVSYNC, &tmp);
}
//---------------------------------------------------------------------
int OvlSwDisp(ScrnInfoPtr pScrn, int disp, Bool clear)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int ret;

    memcpy(&overlay->var, &overlay->saved_var, sizeof(struct fb_var_screeninfo));
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
    ret = ioctl(overlay->fd, FBIOPAN_DISPLAY, &overlay->var);
//    if(ret) return ret;

    if(!disp && clear) OvlClearBuf(pScrn, overlay->ShadowPg);
    return ret;
}
//---------------------------------------------------------------------
int OvlEnable(ScrnInfoPtr pScrn, int enable)
{       
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    return ioctl(overlay->fd, FBIOSET_ENABLE, &enable);
}
//++++++++++++++++++++++++++++++init/close+++++++++++++++++++++++++
int OvlUpdSavMod(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int fd, ret;

    if(overlay != NULL){
	fd = open("/dev/fb0", O_RDONLY);
        if(fd <= 0)
	    ret = -1;
	else {
    	    ret = ioctl(fd, FBIOGET_VSCREENINFO, &overlay->saved_var);
	    overlay->saved_var.activate = 0;
	    overlay->saved_var.yres_virtual = overlay->saved_var.yres << 1;//double buf
	    close(fd);
	}
    }
    else
	ret = -1;
    OVLDBG("****Change res to %d ,  ret=%d ***\n", overlay->saved_var.yres, ret);
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
//	    pMxv->OvlHW->saved_var.activate = 1;
//	    ioctl(fd, FBIOPUT_VSCREENINFO, &pMxv->OvlHW->saved_var);
	    close(fd);
        }
	free_ovl_memory(pMxv->OvlHW);
        close(pMxv->OvlHW->fd);
        free(pMxv->OvlHW);
	pMxv->OvlHW = NULL;
    }
}*/
//------------------------------------------------------------------
static Bool SetupOvl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int tmp,fd;

    overlay->fd = open("/dev/fb1", O_RDWR);
    if (overlay->fd < 0) goto err;

    if( 0 != ioctl(overlay->fd, FBIOGET_FSCREENINFO, &overlay->fix)) goto err1;
//    if(overlay->fix.smem_len < overlay->var.yres_virtual*overlay->var.xres_virtual*4*3) goto err1;
    if(overlay->fix.smem_len < 1920*1080*4*4) goto err1;

    if(OvlUpdSavMod(pScrn) == 0){
	memcpy(&overlay->var, &overlay->saved_var, sizeof(struct fb_var_screeninfo));
/*
	tmp=0;
	ioctl(overlay->fd, FBIOPUT_SET_COLORKEY, &tmp);
	tmp=0;
	ioctl(fd, FBIOPUT_SET_COLORKEY, &tmp);
*/
	ioctl(overlay->fd, FBIOPUT_VSCREENINFO, &overlay->var);
	SelHWMods(pScrn, overlay->var.nonstd);
	ioctl(overlay->fd, FBIOBLANK, FB_BLANK_UNBLANK);
	tmp=0;
	ioctl(overlay->fd, FBIOSET_ENABLE, &tmp);

	fd = open("/dev/fb0", O_RDWR);
	if (fd > 0){
	    tmp=0;
	    ioctl(fd, FBIOSET_OVERLAY_STATE, &tmp);
    	    close(fd);
	}

        return(TRUE);
    }
err1:
    close(overlay->fd);
err:
    return FALSE;
}

static Bool InitOvl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    unsigned int yuv_phy[2];

    pMxv->OvlHW = NULL;
    if(!(pMxv->OvlHW = calloc(1, sizeof(OvlHWRec) )))
	goto err;


    if(!SetupOvl(pScrn)) goto err1;

    pMxv->OvlHW->fb_mem[0] = NULL;
    if(!alloc_ovl_memory(pScrn)) goto err2;

    pthread_mutex_init(&pMxv->OvlHW->rgamutex, NULL);
    pthread_mutex_init(&pMxv->OvlHW->ippmutex, NULL);
    pMxv->OvlHW->rga_pa = 0;

    yuv_phy[0] = pMxv->OvlHW->fix.smem_start;
    yuv_phy[1] = yuv_phy[0] + ((pMxv->OvlHW->pg_len >> 1) & ~PAGE_MASK); //four uv
    pMxv->OvlHW->phadr_mio = yuv_phy[1];
    ioctl(pMxv->OvlHW->fd, FBIOSET_YUV_ADDR, &yuv_phy);

    OvlReset(pScrn);
    OvlEnable(pScrn, 1);

    return TRUE;
err2:
    close(pMxv->OvlHW->fd);
err1:
    free(pMxv->OvlHW);
    pMxv->OvlHW = NULL;
err:
    return FALSE;
}

//----------------------------main init--------------------------
void InitHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Main setup ovl\n");
    if(!InitOvl(pScrn)){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ovl\n");
	return;
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Main setup ovl - pass\n");

    OvlHWPtr overlay = pMxv->OvlHW;
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init ipp\n");
    overlay->fd_IPP = InitIPPHW(pScrn);
    if(overlay->fd_IPP <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ipp\n");
	goto err;
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized ipp\n");

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init rga\n");
    overlay->fd_RGA = InitRGAHW(pScrn);
    if(overlay->fd_RGA <= 0){
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init rga\n");
	goto err;
    }
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized rga\n");
    return;
err:
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Init ovl failed\n");
    if(overlay != NULL){
	if(overlay->fd_IPP > 0)
	    close(overlay->fd_IPP);
	if(overlay->fd_RGA > 0)
	    close(overlay->fd_RGA);

	free_ovl_memory(pMxv->OvlHW);
        close(pMxv->OvlHW->fd);
        free(pMxv->OvlHW);
	pMxv->OvlHW = NULL;
    }

}

void CloseHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;
    int fd,tmp;

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Close\n");
    if(overlay != NULL){
	if(overlay->fd_IPP > 0)
	    close(overlay->fd_IPP);
	if(overlay->fd_RGA > 0)
	    close(overlay->fd_RGA);

	OvlReset(pScrn);
	OvlEnable(pScrn, 0);
/*	fd = open("/dev/fb0", O_RDWR);
	if (fd > 0){
	    tmp=1;
	    ioctl(fd, FBIOSET_ENABLE, &tmp);
	    tmp=0;
	    ioctl(fd, FBIOSET_OVERLAY_STATE, &tmp);
//	    pMxv->OvlHW->saved_var.activate = 1;
//	    ioctl(fd, FBIOPUT_VSCREENINFO, &pMxv->OvlHW->saved_var);
	    close(fd);
        }*/
	free_ovl_memory(pMxv->OvlHW);
        close(pMxv->OvlHW->fd);
        free(pMxv->OvlHW);
	pMxv->OvlHW = NULL;
    }
}
