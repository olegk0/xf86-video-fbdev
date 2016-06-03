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
//#include <X11/extensions/Xv.h>
//#include "xf86_OSproc.h"
#include "xf86.h"
//#include "os.h"
#include "fb.h"

#include "layer.h"
#include "fbdev_priv.h"

#define FbByLay(layout) (hwacl->OvlLay[layout].OvlFb)
#define MBufByLay(layout) (FbByLay(layout)->CurMemPg)

#define LayIsUIfb(layout)	(FbByLay(layout)->Type == UIL)

#ifdef DEBUG
#define OVLDBG(format, args...)		{if(hwacl->debug) WRNMSG(format, ## args);}
#define OVLDBGU(format, args...)	WRNMSG(format, ## args);
#else
#define OVLDBG(format, args...)
#define OVLDBGU(format, args...)
#endif

void HWAclFillKeyHelper(DrawablePtr pDraw, unsigned int ColorKey, RegionPtr pRegion, Bool DrwOffset)
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
}//----------------------------------------------------------------
uint32_t HWAclSetColorKey(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
   	HWAclPtr hwacl = pMxv->HWAcl;
   	uint32_t tmp;

   	if(OvlGetUIBpp() == 16)
   		tmp = COLOR_KEY_16;//TODO
	else
		tmp = COLOR_KEY_32;

   	OvlSetColorKey(0xff000000 | tmp);

	return tmp;
}
//++++++++++++++++++++++++++++++init/close+++++++++++++++++++++++++
int HWAclUpdSavMod(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int ret=-1,tmp;
    OvlLayPg i;

    OVLDBGU("***Res change***");
    if(pMxv->HWAcl != NULL){

    	HWAclPtr hwacl = pMxv->HWAcl;
    	ret = ioctl(hwacl->fd_UI, FBIOGET_VSCREENINFO, &hwacl->cur_var);

    	OvlUpdFbMod(&hwacl->cur_var);

//    	hwacl->ResChange = TRUE;
		OVLDBG("Resolution changed to %dx%d,  ret=%d ***", hwacl->cur_var.xres, hwacl->cur_var.yres, ret);
    }
    return ret;
}

//------------------------------------------------------------------
HWAclPtr ovl_init_ovl(ScrnInfoPtr pScrn)
{
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    OvlMemPgPtr	FbMemPg;
    int i;

    if(!(pMxv->HWAcl = calloc(1, sizeof(HWAclRec) )))
    	goto err;
    HWAclPtr hwacl = pMxv->HWAcl;

    hwacl->fd_UI = fbdevHWGetFD(pScrn);//open(FB_DEV_UI, O_RDONLY);
    if (hwacl->fd_UI < 0) goto err1;

    return hwacl;

err1:
    MFREE(hwacl);
err:
    return NULL;
}

//----------------------------main init--------------------------
void InitHWAcl(ScreenPtr pScreen, Bool debug)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    HWAclPtr hwacl;
    int ret;
    uint32_t ver;

    pMxv->HWAcl = NULL;

    INFMSG( "HW:Initialize overlays");

    ret = Open_RkLayers(1);
    if(ret < 0){
    	ERRMSG( "HW:Error init RkOverlays:%d",ret);
    	return;
    }

	INFMSG( "HW:Number of overlays(with UI): %d",ret);
	ver = OvlGetVersion();
	INFMSG( "HW:rk_overlays version: %d.%d",ver>>8,ver&0xff);

    hwacl = ovl_init_ovl(pScrn);
    if(!hwacl){
    	ERRMSG( "HW:Error init HWAcl");
    	Close_RkLayers();
    	return;
    }

    HWAclUpdSavMod(pScrn);
    hwacl->debug = debug;

    pMxv->HWAcl = hwacl;
    return;
}

void CloseHWAcl(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr pMxv = FBDEVPTR(pScrn);
    int fd,i;
    OvlMemPgPtr	MemPg;

    INFMSG("HW:Close");
    if(pMxv->HWAcl != NULL){
    	HWAclPtr hwacl = pMxv->HWAcl;
    	MFREE(hwacl);
    	Close_RkLayers();
    }
}
