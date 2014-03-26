#include "rk3066.h"

static int resToHDMImodes(int xres,int yres)
{
int ret;
    switch(xres){
    case 640://"640x480p@60Hz"
	ret = 1;
    break;
    case 720://"720x480p@60Hz"
	if(yres == 480)
	    ret = 2;
	else
	    ret = 17;//"720x576p@50Hz"
    break;
    case 1280://"1280x720p@60Hz"=4
	ret = 4;//"1280x720p@50Hz"=19;"1280x720p@24Hz"=60;"1280x720p@25Hz"=61;"1280x720p@30Hz"=62
    break;
    case 1920://"1920x1080p@60Hz"=16
	ret = 16;//"1920x1080p@24Hz"=32;"1920x1080p@25Hz"=33;"1920x1080p@30Hz"=34;"1920x1080p@50Hz"=31
    break;
    default://"1920x1080p@60Hz"
	ret = 16;
    }
    return ret;
}