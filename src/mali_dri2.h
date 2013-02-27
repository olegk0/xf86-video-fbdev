#ifndef __DRI2_H
#define __DRI2_H

typedef struct {
	int	drm_fd;
} Rk30MaliDRI2;

Rk30MaliDRI2 *Rk30MaliDRI2_Init(ScreenPtr pScreen);
void Rk30MaliDRI2_Close(ScreenPtr pScreen);

#endif
