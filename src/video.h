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

#ifndef __VIDEO_H_
#define __VIDEO_H_

#include "fb.h"
#include "layer.h"


typedef struct {
        unsigned char brightness;
        unsigned char contrast;
        RegionRec     clip;
        uint32_t        colorKey;
        int	      videoStatus;
//        Time          offTime;
//        Time          freeTime;
        int           lastPort;

	uint32_t	x_drw;
	uint32_t	y_drw;

	uint32_t	w_src;
	uint32_t	h_src;

//	uint32_t	pixels;
	uint32_t	Uoffset;
	uint32_t	Voffset;

//	int npixels;
//	int nlines;
//	Bool	FlScr;
//	int	rga_pa;
//	int	IPP_mode;
//	int	RGA_mode;

    OvlMemPgPtr		PMemBuf;
    OvlLayPg		OvlPg;
    unsigned char	*fb_mem;
//    uint32_t		mio_offset;
    int			disp_pitch;
} XVPortPrivRec, *XVPortPrivPtr;



void InitXVideo(ScreenPtr pScreen);
void CloseXVideo(ScreenPtr pScreen);

#define LARRAY_SIZE(a) (sizeof((a)) / (sizeof(*(a))))
enum {
 CLIENT_VIDEO_NOINIT	= 0,
 CLIENT_VIDEO_INIT	 = 0x02,
 CLIENT_VIDEO_CHNG	 = 0x03,
 CLIENT_VIDEO_ON	 =0x04,
};
/*
struct offscreen_area {
  unsigned char *priv;
  unsigned char *ptr;
  unsigned int size;
  int fd;
};
*/
// From  xorg_xvmc.c
#define FOURCC_RGB 0x0000003
#define XVIMAGE_RGB                                                             \
{                                                                               \
        FOURCC_RGB,                                                             \
        XvRGB,                                                                  \
        LSBFirst,                                                               \
        {                                                                       \
                'R', 'G', 'B', 0x00,                                            \
                0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71     \
        },                                                                      \
        32,                                                                     \
        XvPacked,                                                               \
        1,                                                                      \
        24, 0x00FF0000, 0x0000FF00, 0x000000FF,                                 \
        0, 0, 0,                                                                \
        0, 0, 0,                                                                \
        0, 0, 0,                                                                \
        {                                                                       \
                'B','G','R','X',                                                \
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0         \
        },                                                                      \
        XvTopToBottom                                                           \
}

#endif
