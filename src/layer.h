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

#include "include/ipp.h"
#include "include/rga.h"
#include <linux/fb.h>
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#define OVL_MEM_PGs 5
#define OVLs 3
#define SRC_MODE TRUE
#define DST_MODE FALSE

#define UserInterfaceFB 0
#define MasterOvlFB 1
#define SecondOvlFB 2


typedef int8_t OvlMemPg;
typedef int8_t OvlFbPg;
typedef int8_t OvlLayPg;

typedef enum
{
    UIFB_MEM,
    FB_MEM,
    BUF_MEM,
} OvlMemPgType;

typedef enum
{
    ERRORL=-1,
    UIL=0,
    SCALEL=1,
    NOT_SCALEL=2,
    ANYL =3,
//    IPPScale = 1+4
} OvlLayoutType;

typedef struct {
	ump_secure_id	ump_fb_secure_id;
	unsigned char	*fb_mem;
	unsigned char	*fb_mio;
	uint32_t	phadr_mio;
	uint32_t	phadr_mem;
	uint32_t	offset;
	uint32_t	offset_mio;
	OvlMemPgType	MemPgType;
	Bool		InUse;
} OvlMemPgRec, *OvlMemPgPtr;

typedef struct {
	int		fd;
//	OvlMemPg	OvlMemPg;
	OvlMemPgPtr	OvlMemPg;
	struct fb_fix_screeninfo	fix;
	OvlLayoutType	FbType;
} OvlFbRec, *OvlFbPtr;

typedef struct {
//	OvlFbPg			OvlFb;
	OvlFbPtr		OvlFb;
	struct fb_var_screeninfo	var;
	Bool			InUse;
	OvlLayoutType		ReqType;
	struct rga_req		RGA_req;
	struct rk29_ipp_req	IPP_req;
	Bool			ResChange;
} OvlLayRec, *OvlLayPtr;

typedef struct {
//	int		fd_ui;
	int		fd_RGA;
	int		fd_IPP;
	OvlLayRec	OvlLay[OVLs];
	OvlFbRec	OvlFb[OVLs];
	OvlMemPgRec	OvlMemPg[OVL_MEM_PGs];//0:, 1:, 2: 3: 4:
	OvlMemPg	OvlMemPgs;
	uint32_t	MaxPgSize;
//	struct fb_fix_screeninfo	fix;
	struct fb_var_screeninfo	cur_var;
	struct fb_var_screeninfo	sav_var;
	Bool		ResChange;
//	uint8_t		ShadowPg;
//	uint8_t		rga_pa;
	pthread_mutex_t	rgamutex;
	pthread_mutex_t	ippmutex;
} OvlHWRec, *OvlHWPtr;


void InitHWAcl(ScreenPtr pScreen);
void CloseHWAcl(ScreenPtr pScreen);
int OvlUpdSavMod(ScrnInfoPtr pScrn);

int OvlClearBuf(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
//int OvlSetMode(ScrnInfoPtr pScrn, unsigned short xres, unsigned short yres, unsigned char mode);
int OvlReset(ScrnInfoPtr pScrn);
int OvlFlushPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, int mode);
int OvlResetFB(ScrnInfoPtr pScrn, OvlLayPg layout);
int OvlCopyHWBufCF(ScrnInfoPtr pScrn, uint32_t SrcYAddr, uint32_t SrcUVAddr, uint32_t SrcVAddr,
				int SrcFrmt, int DstFrmt, uint32_t DstYAddr,
				int Drw_w, int Drw_h, int Drw_x, int Drw_y, int Src_vir, int Dst_vir, Bool useMMU);
//-------------------------------------------------------------
OvlMemPgPtr OvlGetBufByLay(ScrnInfoPtr pScrn, OvlLayPg layout);
//-------------------------------------------------------------
int OvlWaitSync(ScrnInfoPtr pScrn, OvlLayPg layout);
int OvlCpBufToDisp(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg, OvlLayPg layout);
//-------------------------------------------------------------
int OvlSetColorKey(ScrnInfoPtr pScrn, uint32_t color);
int OvlEnable(ScrnInfoPtr pScrn, OvlLayPg layout, int enable);
int OvlSetupBufDrw(ScrnInfoPtr pScrn, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int SrcPitch, OvlLayPg layout);
int OvlSetupDrw(ScrnInfoPtr pScrn, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int Src_w, int Src_h, OvlLayPg layout, Bool BlackBorder);
int OvlSetupFb(ScrnInfoPtr pScrn, int SrcFrmt, int DstFrmt, OvlLayPg layout);
//------------------------------------------------------------
void * OvlMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
int OvlUnMapBufMem(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
OvlLayPg OvlAllocLay(ScrnInfoPtr pScrn, OvlLayoutType type);
void OvlFreeLay(ScrnInfoPtr pScrn, OvlLayPg layout);
OvlMemPgPtr OvlAllocMemPg(ScrnInfoPtr pScrn, OvlMemPgType type);
void OvlFreeMemPg(ScrnInfoPtr pScrn, OvlMemPgPtr PMemPg);
#endif
