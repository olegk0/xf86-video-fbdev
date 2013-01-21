/*
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 */

#include "compat-api.h"
#include <linux/fb.h>

//#include "xf86.h"
#include "xf86xv.h"
#include "xf86fbman.h"

typedef struct {
	unsigned char *fbmio;//for UV
	int   ovl_fd;
	int  fb_fd;
	unsigned char *fbmem;//for RGB or Y
	CARD32   fbmem_len;
	CARD32   fbmio_len;
	CARD32   fboff;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct fb_var_screeninfo saved_var;
	CARD32	pixels;
	CARD32	offset;
	int npixels;
	int nlines;
	char	flmmode;
//	DisplayModeRec buildin;
//	int xres;
//	int yres;
} OvlHWRec, *OvlHWPtr;

typedef struct {
        unsigned char brightness;
        unsigned char contrast;
//        FBAreaPtr   area;
        RegionRec     clip;
        CARD32        colorKey;
        CARD32        videoStatus;
        Time          offTime;
        Time          freeTime;
        int           lastPort;
} XVPortPrivRec, *XVPortPrivPtr;


typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				fboff;
	int				lineLength;
	int				rotate;
	Bool				shadowFB;
	void				*shadow;
	CloseScreenProcPtr		CloseScreen;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	void				(*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);
	EntityInfoPtr			pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int				nDGAMode;
	OptionInfoPtr			Options;

	void				*SunxiMaliDRI2_private;
//	void				*sunxi_disp_private;;
	void				*SunxiDispHardwareCursor_private;

//IAM
        XF86VideoAdaptorPtr	adaptor;
	OvlHWPtr		overlay;
        XVPortPrivPtr		XVportPrivate;
} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

#define SUNXI_MALI_DRI2(p) ((SunxiMaliDRI2 *) \
                           (FBDEVPTR(p)->SunxiMaliDRI2_private))

#define SUNXI_DISP_HWC(p) ((SunxiDispHardwareCursor *) \
                          (FBDEVPTR(p)->SunxiDispHardwareCursor_private))
