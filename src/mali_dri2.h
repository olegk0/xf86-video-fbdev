/*
 * Adapted for rk3066 olegk0 <olegvedi@gmail.com>
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


#ifndef MALI_UMP_DRI2_H
#define MALI_UMP_DRI2_H

#include "dri2.h"

#include "uthash.h"
#include "layer.h"

enum {
    ST_INIT = 0,
    ST_OVL = 1,
    ST_SCR = 2,
};

typedef struct
{
    /* The migrated pixmap (may be NULL if it is a window) */
    PixmapPtr               pPixmap;
    int                     BackupDevKind;
    void                   *BackupDevPrivatePtr;
    int                     refcount;
    UT_hash_handle          hh;

    int                     depth;
    size_t                  width;
    size_t                  height;
//    uint32_t		fb_id;

    int                     ovl_x;
    int                     ovl_y;
    int                     ovl_w;
    int                     ovl_h;

    Bool                    bOverlayWinEnabled;
    OvlMemPgPtr		MemBuf;
    void*		MapMemBuf;

} UMPBufferInfoRec, *UMPBufferInfoPtr;

#define DRIMEMBUFCNT 3

typedef struct {

    uint32_t				colorKey;
    WindowPtr               pOverlayWin;

    int                     scr_w;
    int                     scr_h;


    Bool                    bOverlayWinOverlapped;
    Bool                    bWalkingAboveOverlayWin;
    Bool                    bHardwareCursorIsInUse;
//    struct fb_var_screeninfo fb_var;
    DestroyWindowProcPtr    DestroyWindow;
    DestroyPixmapProcPtr    DestroyPixmap;
    PostValidateTreeProcPtr PostValidateTree;
    uint32_t			null_id;
    uint32_t				null_handle;
    UMPBufferInfoPtr        HashPixmapToUMP;
    int                     drm_fd;
    OvlLayPg			OvlPg;
    OvlLayPg			OvlPgUI;
    OvlMemPgPtr			UIBackMemBuf;
    void*			UIBackMapBuf;
    Bool			debug;
    Bool 			WaitForSync;
    Bool 			HWFullScrFor3D;
    Bool 			EnFl;
    int			mali_refs;
    int 		OvlNeedUpdate;
} RkMaliRec, *RkMaliPtr;

//**********************************************
void RkMaliDRI2_Init(ScreenPtr pScreen, Bool debug, Bool WaitForSync, Bool HWFullScrFor3D);
void RkMaliDRI2_Close(ScreenPtr pScreen);

#endif
