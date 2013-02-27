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


#define FBIOGET_OVERLAY_STATE   0X4619
#define FBIOSET_YUV_ADDR	0x5002
#define FBIOPUT_SET_COLORKEY	0x5010
#define FBIOSET_OVERLAY_STATE   0x5018
#define FBIOSET_ENABLE          0x5019
#define FBIOGET_ENABLE          0x5020


enum {
    RGBA_8888          = 1,
    RGBX_8888          = 2,
    RGB_888            = 3,
    RGB_565            = 4,
    /* Legacy formats (deprecated), used by ImageFormat.java */
    YCbCr_422_SP       = 0x10, // NV16	16
    YCrCb_NV12         = 0x20, // YUY2	32
    YCrCb_444          = 0x22, //yuv444 34
};
