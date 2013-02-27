/*
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

void InitXVideo(ScreenPtr pScreen);
void CloseXVideo(ScreenPtr pScreen);

#define LARRAY_SIZE(a) (sizeof((a)) / (sizeof(*(a))))

#define CLIENT_VIDEO_ON		0x04
#define CLIENT_VIDEO_CH		0x03
#define CLIENT_VIDEO_INIT	0x02

struct offscreen_area {
  unsigned char *priv;
  unsigned char *ptr;
  unsigned int size;
  int fd;
};

#define FOURCC_RGBA8888   0x41424752
#define XVIMAGE_RGBA8888 \
        { \
                FOURCC_RGBA8888, \
                XvRGB, \
                LSBFirst, \
                { 'R', 'G', 'B', 'A', \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                32, \
                XvPacked, \
                1, \
                32, 0x00FF0000, 0x0000FF00, 0x000000FF, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                {'R','G','B','A',\
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }               

#define FOURCC_RGB888    0x3

#define XVIMAGE_RGB888   \
        { \
                FOURCC_RGB888, \
                XvRGB, \
                LSBFirst, \
                { 3, 0, 0, 0, \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                32, \
                XvPacked, \
                1, \
                24, 0x00FF0000, 0x0000FF00, 0x000000FF, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                { 'R', 'G', 'B', 3, \
                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }

#endif
