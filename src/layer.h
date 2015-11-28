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


#ifndef __LAYER_H_
#define __LAYER_H_

#include "rk_layers.h"

typedef struct {
	int				fd_UI;
	struct fb_var_screeninfo	cur_var;
//	struct fb_var_screeninfo	sav_var;
//	Bool			OvlInit;
	Bool			debug;
} HWAclRec, *HWAclPtr;


void InitHWAcl(ScreenPtr pScreen, Bool debug);
void CloseHWAcl(ScreenPtr pScreen);
int HWAclUpdSavMod(ScrnInfoPtr pScrn);
void HWAclFillKeyHelper(DrawablePtr pDraw, unsigned int ColorKey, RegionPtr pRegion, Bool DrwOffset);

#endif
