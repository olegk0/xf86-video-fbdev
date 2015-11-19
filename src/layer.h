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


#ifndef __LAYER_H_
#define __LAYER_H_

//#define IPP_ENABLE

#ifdef IPP_ENABLE
#include "include/ipp.h"
#endif
#include "include/rga.h"
#include <linux/fb.h>
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>


#define MAX_OVERLAYs 3
#define SRC_MODE TRUE
#define DST_MODE FALSE

typedef int8_t OvlMemPg;
typedef int8_t OvlFbPg;
typedef int8_t OvlLayPg;

enum {
	UserInterfaceFB=0,
	MasterOvlFB=1,
	SecondOvlFB=2,
};

typedef enum {
    UIFB_MEM,
    FB_MEM,
    BUF_MEM,
} OvlMemPgType;

typedef enum {
    ERRORL=-1,
    UIL=0,
    SCALEL=1,
//    NOT_SCALEL=2,
    ANYL =3,
//    IPPScale = 1+4
} OvlLayoutType;

typedef enum
{
    FRONT_FB=0,
    BACK_FB=1,
	NEXT_FB=10,
} OvlFbBufType;

typedef enum
{
	ALC_NONE_FB=0,
	ALC_FRONT_FB=1,
	ALC_FRONT_BACK_FB=2,
} OvlFbBufAllocType;

typedef struct {
	ump_secure_id	ump_fb_secure_id;
	ump_handle		ump_handle;
	unsigned char	*fb_mmap;
	unsigned long	buf_size;
	unsigned long	phy_addr;
	OvlMemPgType	MemPgType;
	unsigned long	offset_mio;
//	unsigned long	phadr_mio;
//	unsigned long	offset;
//	unsigned char	*fb_mio_mmap;
//	Bool			InUse;
} OvlMemPgRec, *OvlMemPgPtr;

typedef struct {
	int				fd;
	OvlMemPgPtr		CurMemPg;
	struct fb_fix_screeninfo	fix;
//	unsigned long	offset_mio;
	OvlLayoutType	Type;
} OvlFbRec, *OvlFbPtr;

typedef struct {
	OvlFbPtr		OvlFb;
	OvlFbBufType	FbBufUsed;
	OvlMemPgPtr		FbMemPgs[2];
	struct fb_var_screeninfo	var;
	OvlLayoutType	ReqType;
	struct rga_req	RGA_req;
#ifdef IPP_ENABLE
	struct rk29_ipp_req	IPP_req;
#endif
	Bool			InUse;
//	Bool			ResChange;
} OvlLayRec, *OvlLayPtr;

typedef struct {
	int				fd_USI;
	OvlLayRec		OvlLay[MAX_OVERLAYs];
	OvlFbRec		OvlFb[MAX_OVERLAYs];
	uint32_t		MaxPgSize;
	struct fb_var_screeninfo	cur_var;
	struct fb_var_screeninfo	sav_var;
	Bool			ResChange;
#ifdef RGA_ENABLE
	int				fd_RGA;
	pthread_mutex_t	rgamutex;
#endif
#ifdef IPP_ENABLE
	int				fd_IPP;
	pthread_mutex_t	ippmutex;
#endif
	Bool			debug;
} OvlHWRec, *OvlHWPtr;


void InitHWAcl(ScreenPtr pScreen, Bool debug);
void CloseHWAcl(ScreenPtr pScreen);
int OvlUpdSavMod(ScrnInfoPtr pScrn);

//int OvlClearBuf(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
//int OvlReset(ScrnInfoPtr pScrn);
int OvlSetModeFb(ScrnInfoPtr pScrn, OvlLayPg layout, unsigned short xres, unsigned short yres, unsigned char mode);
int OvlResetFB(ScrnInfoPtr pScrn, OvlLayPg layout);
int OvlCopyHWBufCF(ScrnInfoPtr pScrn, uint32_t SrcYAddr, uint32_t SrcUVAddr, uint32_t SrcVAddr,
				int SrcFrmt, int DstFrmt, uint32_t DstYAddr,
				int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir, Bool useMMU);
//-------------------------------------------------------------
OvlMemPgPtr OvlGetBufByLay(ScrnInfoPtr pScrn, OvlLayPg layout, OvlFbBufType BufType);
int OvlGetVXresByLay(ScrnInfoPtr pScrn, OvlLayPg layout);
void OvlFillKeyHelper(DrawablePtr pDraw, unsigned int ColorKey, RegionPtr pRegion, Bool DrwOffset);
int OvlGetUIBpp(ScrnInfoPtr pScrn);
//-------------------------------------------------------------
//int OvlWaitSync(ScrnInfoPtr pScrn, OvlLayPg layout);
int OvlCpBufToDisp(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout);
int OvlFlipFb(ScrnInfoPtr pScrn, OvlLayPg layout, OvlFbBufType flip, Bool clrPrev);
int Ovl2dBlt(ScrnInfoPtr pScrn, uint32_t *src_bits, uint32_t *dst_bits, int src_stride, int dst_stride, int src_bpp, int dst_bpp, int src_x, int src_y, int dst_x, int dst_y, int w, int h);
//-------------------------------------------------------------
int OvlSetColorKey(ScrnInfoPtr pScrn, uint32_t color);
int OvlEnable(ScrnInfoPtr pScrn, OvlLayPg layout, int enable);
int OvlSetupBufDrw(ScrnInfoPtr pScrn, OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int SrcPitch);
int OvlSetupDrw(ScrnInfoPtr pScrn, OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int Src_w, int Src_h);
int OvlSetupFb(ScrnInfoPtr pScrn, OvlLayPg layout, int SrcFrmt, int DstFrmt, unsigned short xres, unsigned short yres);
//------------------------------------------------------------
void * OvlMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
int OvlUnMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
OvlLayPg OvlAllocLay(ScrnInfoPtr pScrn, OvlLayoutType type, OvlFbBufAllocType FbBufAlloc);
void OvlFreeLay(ScrnInfoPtr pScrn, OvlLayPg layout);
OvlMemPgPtr OvlAllocMemPg(ScrnInfoPtr pScrn, unsigned long size);
int OvlFreeMemPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
#endif
