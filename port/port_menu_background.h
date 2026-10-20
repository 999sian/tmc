#ifndef PORT_MENU_BACKGROUND_H
#define PORT_MENU_BACKGROUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native decorative BG3 only: no menu text, sprites, or interactive panels. */
const uint32_t* Port_MenuBackground_GetPixels(void); /* 256 x 160 ABGR8888; NULL outside supported menus. */
void Port_MenuBackground_Render(float stageX, float stageY, float stageW, float stageH,
                                float frameX, float frameY, float frameW, float frameH);

#ifdef __cplusplus
}
#endif

#endif
