/*
 *  For rk3066 - rk3188
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


#ifndef __RK_LAYER_H_
#define __RK_LAYER_H_

#include <linux/fb.h>

#define DEF_COLOR_KEY 0

#define MAX_PANEL_SIZE_X 1920
//#define PANEL_SIZE_X 1280
#define MAX_PANEL_SIZE_Y 1080
//#define PANEL_SIZE_Y 720

#define FB_MAXPGSIZE MAX_PANEL_SIZE_X*MAX_PANEL_SIZE_Y*4

#ifndef Bool
typedef int Bool;
#endif
typedef int OvlMemPg;
typedef int OvlFbPg;
typedef int OvlLayPg;

typedef enum {
    UIFB_MEM,
    FB_MEM,
    BUF_MEM,
} OvlMemPgType;

typedef enum {
    RKL_FORMAT_DEFAULT = 0,
    RKL_FORMAT_RGBA_8888 = 1, // = Hi_AARRGGBB_Low TODO rk3188 kernel BUG
    RKL_FORMAT_RGBX_8888 = 2, // = Hi_AARRGGBB_Low TODO rk3188 kernel BUG
	RKL_FORMAT_BGRA_8888 = 3, // = Hi_AARRGGBB_Low TODO rk3188 kernel BUG
    RKL_FORMAT_RGB_888 = 10,
    RKL_FORMAT_RGB_565 = 21,
    RKL_FORMAT_RGBA_5551 = 25,
    RKL_FORMAT_RGBA_4444 = 27,
	// YCbCr - UV
	RKL_FORMAT_UV_NV12_SP = 40, // YYYY UV, i420
    RKL_FORMAT_UV_NV16_SP = 41, // YYYY UVUV, i422, 16
	// YCrCb - VU
	RKL_FORMAT_VU_NV21_SP = 50, // YYYY VU
	RKL_FORMAT_VU_NV61_SP = 51,

	RKL_FORMAT_420_P  = 60,
	RKL_FORMAT_422_P  = 61,
	RKL_FORMAT_444_P = 42, //yuv444 34

} OvlLayoutFormatType;

#define EMU_L 9	//for usability
#define BOTTOM_LAY 64

typedef enum {
	ERROR_L=-1,
	UI_L=0,
	ANY_L = 1,
	ANY_HW_L = ANY_L,					//preferably any hardware layer
	SCALE_L= 3,							//scalable, preferably hardware layer
	NOTSCALE_L= 4,						//not scalable, preferably hardware layer
	HWC_L= 5,
	ANY_EMU_L = ANY_HW_L + EMU_L,			//preferably any emulated layer
	EMU_SCALE_L = SCALE_L + EMU_L,			//scalable, preferably pseudo layer (IPP based)
	EMU_NOTSCALE_L = NOTSCALE_L + EMU_L,	//not scalable, preferably pseudo layer (RGA based)
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

typedef void *OvlMemPgPtr;

typedef void *OvlFbPtr;

typedef void *OvlLayPtr;

int Open_RkLayers(Bool MasterMode);
void Close_RkLayers(void);
void OvlUpdFbMod(struct fb_var_screeninfo *var);
int OvlInitMainFB(const char *dev_name, int depth);
int OvlSetHDMI(int xres,int yres);
uint32_t OvlGetVersion();

void OvlCopyPackedToFb(OvlMemPgPtr PMemPg, const void *src, int dstPitch, int srcPitch, int w, int h, Bool reverse);
void OvlCopyPlanarToFb(OvlMemPgPtr PMemPg, const void *src_Y, const void *src_U, const void *src_V,
		int dstPitch, int srcPitch_y, int srcPitch_c, int w, int h);
void OvlCopyNV12SemiPlanarToFb(OvlMemPgPtr PMemPg, const void *src_Y, const void *src_UV,
		int dstPitch, int srcPitch, int w, int h);
void OvlCopyNV16SemiPlanarToFb(OvlMemPgPtr PMemPg, const void *src_Y, const void *src_UV,
		int dstPitch, int srcPitch, int w, int h);
int OvlResetFB(OvlLayPg layout);
//-------------------------------------------------------------
OvlMemPgPtr OvlGetBufByLay(OvlLayPg layout, OvlFbBufType BufType);
uint32_t OvlGetVXresByLay(OvlLayPg layout);
int OvlGetUIBpp(void);
uint32_t OvlGetSidByMemPg( OvlMemPgPtr PMemPg);
OvlLayoutFormatType OvlGetModeByLay( OvlLayPg layout);
uint32_t OvlGetXresByLay( OvlLayPg layout);
uint32_t OvlGetYresByLay( OvlLayPg layout);
int OvlGetBppByLay(OvlLayPg layout);
uint32_t OvlVresByXres(uint32_t xres);
uint32_t OvlGetFbSizeByLay( OvlLayPg layout);
int OvlGetPanelSize(uint32_t *Panel_w, uint32_t *Panel_h);
//-------------------------------------------------------------
//int OvlWaitVSync();
int OvlWaitVSync( OvlLayPg layout);
int OvlFlipFb(OvlLayPg layout, OvlFbBufType flip, Bool clrPrev);
//-------------------------------------------------------------
int OvlSetColorKey( OvlLayPg layout, unsigned int color, Bool Enable);
unsigned int OvlGetUIColorKey(void);
void OvlSetUIColorKey(unsigned int ColorKey);
int OvlEnable( OvlLayPg layout, int enable, int vsync_en);
int OvlSetupBufDrw(OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h, int SrcPitch);
int OvlSetupDrw(OvlLayPg layout, int Drw_x, int Drw_y, int Drw_w, int Drw_h);
int OvlSetupFb( OvlLayPg layout, OvlLayoutFormatType format, uint32_t xres, uint32_t yres);
int OvlLayerLinkMemPg( OvlLayPg layout, OvlMemPgPtr MemPg);
//------------------------------------------------------------
int OvlClrMemPg(OvlMemPgPtr PMemPg);
uint32_t OvlGetUVoffsetMemPg( OvlMemPgPtr PMemPg);
uint32_t OvlGetPhyAddrMemPg( OvlMemPgPtr PMemPg);
void * OvlMapBufMem(OvlMemPgPtr PMemPg);
int OvlUnMapBufMem(OvlMemPgPtr PMemPg);
OvlLayPg OvlAllocLay(int type, OvlFbBufAllocType FbBufAlloc);
int OvlFreeLay(OvlLayPg layout);
OvlMemPgPtr OvlAllocMemPg(uint32_t size, uint32_t YUV_offset);
int OvlFreeMemPg(OvlMemPgPtr PMemPg);
int OvlSetIPP_RGADst( OvlLayPg layout, OvlMemPgPtr DstMemPg);

int OvlCacheAllocBlock(OvlLayPg layout, int min_cnt, int max_cnt, uint32_t size, uint32_t *yuv_offs, Bool force_init);
int OvlCacheFreeBlock(OvlLayPg layout);
int OvlGetCacheBlockForPut(OvlLayPg layout, uint32_t *PutFbPhyAddr, uint32_t **PutFbMapAddr);
int OvlCacheStart(OvlLayPg layout, unsigned int delay_us);
int OvlCacheStop(OvlLayPg layout);
int OvlCacheGetStat(OvlLayPg layout);

int OvlChangeFmtInit(int Src_w, int Src_h, int Src_vir, int Dst_vir, OvlLayoutFormatType Src_fmt, OvlLayoutFormatType Dst_fmt);
int OvlChangeFmtInitPhy(int Src_w, int Src_h, int Src_vir, int Dst_vir, OvlLayoutFormatType Src_fmt, OvlLayoutFormatType Dst_fmt);
void OvlChangeFmtSetSrc(uint32_t Y_RGB_Addr, uint32_t U_UV_Addr, uint32_t V_Addr);
void OvlChangeFmtSetDst(uint32_t Y_RGB_Addr, uint32_t U_UV_Addr, uint32_t V_Addr);
int OvlChangeFmtRun(Bool sync);
int OvlChangeFmtFree(void);

#endif
