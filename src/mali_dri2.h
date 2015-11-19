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

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>
#include "dri2.h"

#include "uthash.h"
#include "layer.h"

enum {
    ST_INIT = 0,
    ST_OVL = 1,
    ST_SCR = 2,
};

/* Data structure with the information about an UMP buffer */
typedef struct
{
    /* The migrated pixmap (may be NULL if it is a window) */
    PixmapPtr               pPixmap;
    int                     BackupDevKind;
    void                   *BackupDevPrivatePtr;
    int                     refcount;
    UT_hash_handle          hh;

    ump_handle              handle;
    size_t                  size;
    uint8_t                *addr;
    int                     depth;
    size_t                  width;
    size_t                  height;
    Bool					frame;
} UMPBufferInfoRec, *UMPBufferInfoPtr;

typedef struct {
    int                     ovl_x;
    int                     ovl_y;
    int                     ovl_w;
    int                     ovl_h;
    unsigned int			lstatus;

    WindowPtr               pOverlayWin;
    Bool                    bOverlayWinEnabled;

    Bool                    bHardwareCursorIsInUse;
//    struct fb_var_screeninfo fb_var;
    DestroyWindowProcPtr    DestroyWindow;
    DestroyPixmapProcPtr    DestroyPixmap;
    ump_secure_id			ump_fb_front_secure_id;
    ump_secure_id			ump_fb_back_secure_id;
    ump_secure_id			ump_null_secure_id;
    ump_handle				ump_null_handle;
    UMPBufferInfoPtr        HashPixmapToUMP;
    int                     drm_fd;
    OvlMemPgPtr				FrontMemBuf;
    OvlMemPgPtr				BackMemBuf;
    void*					FrontMapBuf;
    void*					BackMapBuf;
    OvlLayPg				OvlPg;
    Bool					debug;
} Rk30MaliRec, *Rk30MaliPtr;

//**********************************************
void Rk30MaliDRI2_Init(ScreenPtr pScreen, Bool debug);
void Rk30MaliDRI2_Close(ScreenPtr pScreen);

#endif
