#ifndef __DRI2_H
#define __DRI2_H

typedef struct {
	int	drm_fd;
} SunxiMaliDRI2;

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen);
void SunxiMaliDRI2_Close(ScreenPtr pScreen);

#endif
