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

//++++++++++++++++++++++++++++++++++++IPP++++++++++++++++++++++++++++++++++++++++++
static IPPHWPtr InitIPPHW(ScrnInfoPtr pScrn)
{
    IPPHWPtr IPPHW;

    if(!(IPPHW = calloc(1, sizeof(IPPHWRec) )))
	return NULL;
    IPPHW->fd = open("/dev/rk29-ipp", O_RDWR);
    if (IPPHW->fd < 0)
	goto err;

    return(IPPHW);
err:
    free(IPPHW);
    return NULL;
}
//-------------------------------------------------------------
int IppBlit(ScrnInfoPtr pScrn, struct rk29_ipp_req *ipp_req)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    IPPHWPtr IPPHW = pMxv->IPPHW;
/*    ipp_req.src0.YrgbMst = 0x90800000;
    ipp_req.src0.w = FrameWidth;
    ipp_req.src0.h = FrameHeight;
    ipp_req.src0.fmt = IPP_XRGB_8888;
    ipp_req.dst0.YrgbMst = 0x91000000+(800*4);
    ipp_req.dst0.w = 150;
    ipp_req.dst0.h = 150;
    ipp_req->dst0.fmt = IPP_XRGB_8888;
    ipp_req.src_vir_w = 1280;
    ipp_req.dst_vir_w = 1280;
    ipp_req->timeout = 100;
    ipp_req->flag = IPP_ROT_0;
*/
    return ioctl(IPPHW->fd, IPP_BLIT_SYNC, ipp_req);
}
//++++++++++++++++++++++++++++++++++++RGA++++++++++++++++++++++++++++++++++++++++++
static RGAHWPtr InitRGAHW(ScrnInfoPtr pScrn)
{
    RGAHWPtr RGAHW;

    if(!(RGAHW = calloc(1, sizeof(RGAHWRec) )))
	return NULL;
    RGAHW->fd = open("/dev/rga", O_RDWR);
    if (RGAHW->fd < 0)
	goto err;
    return(RGAHW);
err:
    free(RGAHW);
    return NULL;
}
//-------------------------------------------------------------
int RgaBlit(ScrnInfoPtr pScrn, struct rga_req *RGA_req, int syncmode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    RGAHWPtr RGAHW = pMxv->RGAHW;
//Src
/*    RGA_req.src.yrgb_addr = fbmio;//0x90800000;
    
    RGA_req->src.uv_addr  = 0;
    RGA_req->src.v_addr   = 0;
    RGA_req->src.vir_w = 1280;
    RGA_req->src.vir_h = 720;
    RGA_req->src.format = RK_FORMAT_RGBX_8888;
    
    RGA_req->src.act_w = (FrameWidth+15)& ~15;
    RGA_req->src.act_h = (FrameHeight)& ~15;
    RGA_req->src.x_offset = 0;
    RGA_req->src.y_offset = 0;
//Dst
    RGA_req->dst.yrgb_addr = 0x91000000;
    RGA_req->dst.uv_addr  = 0;
    RGA_req->dst.v_addr   = 0;
    RGA_req->dst.vir_w = 1280;
    RGA_req->dst.vir_h = 720;
    RGA_req->dst.format = RK_FORMAT_RGBX_8888; 
    RGA_req->clip.xmin = 0;
    RGA_req->clip.xmax = Rga_Request.dst.vir_w - 1;
    RGA_req->clip.ymin = 0;
    RGA_req->clip.ymax = Rga_Request.dst.vir_h - 1;

    RGA_req->dst.act_w = 720/2;
    RGA_req->dst.act_h = 528/2;//1/2 
    
    RGA_req->dst.x_offset = 600;
    RGA_req->dst.y_offset = 100;

    Rga_Request.sina = 0;
    Rga_Request.cosa = 0;
//Ctl
    RGA_req->render_mode = 5;
    RGA_req->scale_mode = 0;          // 0 nearst / 1 bilnear / 2 bicubic     
    RGA_req->rotate_mode = 0;
*/

    return ioctl(RGAHW->fd, syncmode, RGA_req);
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

    rs = (overlay->var.yres * overlay->var.xres) << 2;//max bytes in screen
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
int OvlSetMode(ScrnInfoPtr pScrn, unsigned short xres, unsigned short yres, unsigned char mode)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = pMxv->OvlHW;

    if((xres > overlay->saved_var.xres_virtual)||(yres > overlay->saved_var.yres_virtual)) return -1;
//    if((xres > overlay->saved_var.xres)||(yres > overlay->saved_var.yres)) return -1;

    memcpy(&overlay->var, &overlay->saved_var, sizeof(struct fb_var_screeninfo));
    overlay->var.nonstd = mode;
    overlay->var.xres = xres;
    overlay->var.yres = yres;

    return ioctl(overlay->fd, FBIOPUT_VSCREENINFO, &overlay->var);
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
static void CloseOvl(ScrnInfoPtr pScrn)
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
	    ioctl(fd, FBIOPUT_VSCREENINFO, &pMxv->OvlHW->saved_var);
	    close(fd);
        }
	free_ovl_memory(pMxv->OvlHW);
        close(pMxv->OvlHW->fd);
        free(pMxv->OvlHW);
	pMxv->OvlHW = NULL;
    }
}
//------------------------------------------------------------------
static OvlHWPtr SetupOvl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlHWPtr overlay = NULL;
    int tmp,fd;
    if(!(overlay = calloc(1, sizeof(OvlHWRec) )))
	return NULL;

    overlay->fb_mem[0] = NULL;

    overlay->fd = open("/dev/fb1", O_RDWR);
    if (overlay->fd < 0) goto err;

    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) goto err1;

    if( 0 != ioctl(overlay->fd, FBIOGET_FSCREENINFO, &overlay->fix)) goto err2;
    if(overlay->fix.smem_len < 12*1024*1024) goto err2;
    if(0 == ioctl(fd, FBIOGET_VSCREENINFO, &overlay->var)){
	overlay->var.activate = 0;
	overlay->var.yres_virtual = overlay->var.yres << 1;//double buf
	memcpy(&overlay->saved_var, &overlay->var, sizeof(struct fb_var_screeninfo));
//	ioctl(overlay->fd, FBIOPUT_VSCREENINFO, &overlay->var);
	ioctl(overlay->fd, FBIOBLANK, FB_BLANK_UNBLANK);
	tmp=0;
	ioctl(fd, FBIOSET_OVERLAY_STATE, &tmp);
//	tmp=0;
//	ioctl(overlay->fd, FBIOSET_ENABLE, &tmp);
        close(fd);

        return(overlay);
    }
err2:
    close(fd);
err1:
    close(overlay->fd);
err:
    free(overlay);
    return NULL;
}

static Bool InitOvl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
//    OvlHWPtr overlay;
    unsigned int yuv_phy[2];

    pMxv->OvlHW = SetupOvl(pScrn);
    if(NULL == pMxv->OvlHW) goto err;
    pMxv->OvlHW->fb_mem[0] = NULL;
    if(!alloc_ovl_memory(pScrn)) goto err1;


    yuv_phy[0] = pMxv->OvlHW->fix.smem_start;
    yuv_phy[1] = yuv_phy[0] + ((pMxv->OvlHW->pg_len >> 1) & ~PAGE_MASK); //four uv
    pMxv->OvlHW->phadr_mio = yuv_phy[1];
    ioctl(pMxv->OvlHW->fd, FBIOSET_YUV_ADDR, &yuv_phy);

    OvlReset(pScrn);
    OvlEnable(pScrn, 0);

    return TRUE;
err1:
    close(pMxv->OvlHW->fd);
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

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init ovl\n");
    if(!InitOvl(pScrn))
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ovl\n");
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized ovl\n");
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init ipp\n");
    pMxv->IPPHW = InitIPPHW(pScrn);
    if(pMxv->IPPHW == NULL)
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init ipp\n");
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized ipp\n");
    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Try init rga\n");
    pMxv->RGAHW = InitRGAHW(pScrn);
    if(pMxv->RGAHW == NULL)
	xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "HW:Error init rga\n");
    else
	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Initialized rga\n");

}

void CloseHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);

    xf86DrvMsg( pScrn->scrnIndex, X_INFO, "HW:Close\n");
    if(pMxv->OvlHW != NULL)
	CloseOvl(pScrn);
    if(pMxv->IPPHW != NULL){
	close(pMxv->IPPHW->fd);
	free(pMxv->IPPHW);
	pMxv->IPPHW = NULL;
    }
    if(pMxv->RGAHW != NULL){
	close(pMxv->RGAHW->fd);
	free(pMxv->RGAHW);
	pMxv->RGAHW = NULL;
    }

}
