/*
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

#include "compat-api.h"
#include <linux/fb.h>

#include "xf86xv.h"
#include "xf86fbman.h"
#include "fbdevhw.h"

#define DEBUG

#define FB_DEV_UI "/dev/fb0"

#define INFMSG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_INFO, format "\n", ## args)
#define WRNMSG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, format "\n", ## args)
#define ERRMSG(format, args...)		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, format "\n", ## args)

#define MFREE(p)	{free(p);p=NULL;}

typedef struct {
	unsigned char*		fbstart;
	unsigned char*		fbmem;
	int					fboff;
	int					lineLength;
	int					rotate;
	Bool				shadowFB;
	void				*shadow;
	CloseScreenProcPtr	CloseScreen;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	void				(*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);
	EntityInfoPtr		pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int					nDGAMode;
	OptionInfoPtr		Options;
	Bool				WaitForSync;
//	Bool				HWLayerFor3D;
	unsigned int			ColorKey;
	Bool				ColorKeyEn;

	void				*RkMali;
	void				*RkHWC;
	void				*HWAcl;
	void				*XVport;
	int					DebugLvl;

} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))


