/*
 */

#include "compat-api.h"
#include <linux/fb.h>

#include "include/ipp.h"
#include "include/rga.h"
#include "xf86xv.h"
#include "xf86fbman.h"
//#include "exa.h"

#define DEBUG
#define PAGE_MASK    (getpagesize() - 1)

typedef struct {
	int   fd;
	unsigned char *fb_mem[4];//0:for RGB, 1:swap?, 2:misc
	unsigned char *fb_mio;
	CARD32 phadr_mem[4];//0:for RGB, 1:swap?, 2:misc
	CARD32 phadr_mio;
	CARD32 pg_len;
//	CARD32 fboff;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct fb_var_screeninfo saved_var;
	int ShadowPg;
} OvlHWRec, *OvlHWPtr;

typedef struct {
	int  fd;
//	struct rga_req  RGA_req;
} RGAHWRec, *RGAHWPtr;

typedef struct {
	int  fd;
//        struct rk29_ipp_req IPP_req;
} IPPHWRec, *IPPHWPtr;

typedef struct {
        unsigned char brightness;
        unsigned char contrast;
        RegionRec     clip;
        CARD32        colorKey;
        int	      videoStatus;
        Time          offTime;
        Time          freeTime;
        int           lastPort;

	CARD32	x_drv;
	CARD32	y_drv;

//	CARD32	pixels;
//	CARD32	offset;

//	int npixels;
//	int nlines;
//	char	flmmode;
        struct rk29_ipp_req IPP_req;
	struct rga_req  RGA_req;
	struct rga_req  RGA_req1;
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

	void				*Rk30MaliDRI2_private;
	void				*Rk30DispHardwareCursor_private;

//IAM
//        XF86VideoAdaptorPtr	adaptor;
//	ExaDriverPtr 		ExaHW;
	OvlHWPtr		OvlHW;
	IPPHWPtr		IPPHW;
	RGAHWPtr		RGAHW;
        XVPortPrivPtr		XVport;

} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

#define RK30_MALI_DRI2(p) ((Rk30MaliDRI2 *) \
                           (FBDEVPTR(p)->Rk30MaliDRI2_private))

#define RK30_DISP_HWC(p) ((Rk30DispHardwareCursor *) \
                          (FBDEVPTR(p)->Rk30DispHardwareCursor_private))
