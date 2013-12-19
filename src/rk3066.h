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

/*
fb0:win0	
fb1:win1	
ARGB888
RGB888
RGB565
YCbCr420
YCbCr422
YCbCr444

fb2:win2 	
ARGB888
RGB888
RGB565
8bpp
4bpp
2bpp
1bpp

*/
#define FB_DEV_O2	"/dev/fb2" //second overlay(not scalable)
#define FB_DEV_O1	"/dev/fb1" //main overlay
#define FB_DEV_UI	"/dev/fb0"
#define FB_DEV_IPP	"/dev/rk29-ipp"
#define FB_DEV_RGA	"/dev/rga"

enum {
    FBUI,
    FBO1,
    FBO2,
};
/*
#define FB_MAXPGS_O1 2
#define FB_MAXPGS_O2 2
#define FB_MAXPGS_UI 1
*/

#define PANEL_SIZE_X 1920
#define PANEL_SIZE_Y 1080

#define FB_MAXPGSIZE PANEL_SIZE_X*PANEL_SIZE_Y*4

#define FBIOGET_PANEL_SIZE	0x5001
#define FBIOGET_OVERLAY_STATE   0X4619
#define FBIOSET_YUV_ADDR	0x5002
#define FBIOSET_COLORKEY	0x5010
#define FBIOSET_DISP_PSET	0x5011
#define FBIOSET_FBMEM_CLR	0x5013
#define FBIOSET_FBMEM_OFFS_SYNC	0x5012
#define GET_UMP_SECURE_ID_BUF1	_IOWR('m', 310, unsigned int)
#define GET_UMP_SECURE_ID_BUF2	_IOWR('m', 311, unsigned int)
#define GET_UMP_SECURE_ID_BUFn	_IOWR('m', 312, unsigned int)

#define FBIOSET_OVERLAY_STATE   0x5018
#define FBIOSET_ENABLE          0x5019
#define FBIOGET_ENABLE          0x5020
#define FBIO_WAITFORVSYNC       _IOW('F', 0x20, __u32)

typedef struct
{
    int		poffset_x;	//Panel offset x
    int		poffset_y;	//Panel offset y
    int		ssize_w;	//Source img size width, 0-not change
    int		ssize_h;	//Source img size height, 0-not change
    int		scale_w;	//Scale size width, 0-not change
    int		scale_h;	//Scale size height, 0-not change
} SFbioDispSet;




enum {
    RGBA_8888          = 1,
    RGBX_8888          = 2,
    RGB_888            = 3,
    RGB_565            = 4,
    /* Legacy formats (deprecated), used by ImageFormat.java */
    YCbCr_422_SP       = 0x10, // NV16	16
    YCrCb_NV12_SP      = 0x20, // YUY2	32
    YCrCb_444          = 0x22, //yuv444 34
//add formats NOT for display
    YCbCr_422_P       = 0x11, // NV16	16
    YCrCb_NV12_P      = 0x21, // YUY2	32

};
