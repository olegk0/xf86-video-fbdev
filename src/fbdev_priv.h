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

#define GET_UMP_SECURE_ID_BUF1 _IOWR('m', 310, unsigned int)
#define GET_UMP_SECURE_ID_BUF2 _IOWR('m', 311, unsigned int)

//#define GET_UMP_SECURE_ID_SUNXI_FB _IOWR('s', 100, unsigned int)


typedef struct {
	int   fd;
	int   fd_RGA;
	int   fd_IPP;
	unsigned char *fb_mem[4];//0:for RGB, 1:swap?, 2:misc
	unsigned char *fb_mio;
	CARD32 phadr_mem[4];//0:for RGB, 1:swap?, 2:misc
	CARD32 phadr_mio;
	CARD32 pg_len;
//	CARD32 fboff;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct fb_var_screeninfo saved_var;
	int	IPP_mode;
	int	RGA_mode;
	int ShadowPg;
	int rga_pa;
	pthread_mutex_t rgamutex;
	pthread_mutex_t ippmutex;
} OvlHWRec, *OvlHWPtr;

typedef struct {
        unsigned char brightness;
        unsigned char contrast;
        RegionRec     clip;
        CARD32        colorKey;
        int	      videoStatus;
//        Time          offTime;
//        Time          freeTime;
        int           lastPort;

	CARD32	x_drw;
	CARD32	y_drw;

	CARD32	w_src;
	CARD32	h_src;

//	CARD32	pixels;
//	CARD32	offset;

//	int npixels;
//	int nlines;
	Bool	FlScr;
	int	rga_pa;
	int	IPP_mode;
	int	RGA_mode;
} XVPortPrivRec, *XVPortPrivPtr;


typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				fboff;
//	int				fbhres;
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

//        XF86VideoAdaptorPtr	adaptor;
//	ExaDriverPtr 		ExaHW;
	OvlHWPtr		OvlHW;
        XVPortPrivPtr		XVport;
} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

#define RK30_MALI_DRI2(p) ((Rk30MaliDRI2 *) \
                           (FBDEVPTR(p)->Rk30MaliDRI2_private))

#define RK30_DISP_HWC(p) ((Rk30DispHardwareCursor *) \
                          (FBDEVPTR(p)->Rk30DispHardwareCursor_private))
