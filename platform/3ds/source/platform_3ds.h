#ifndef TMC_PLATFORM_3DS_H
#define TMC_PLATFORM_3DS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int Platform3DS_Init(void);
void Platform3DS_Shutdown(void);
void Platform3DS_ShowSplash(void);
uint16_t Platform3DS_ReadKeyInput(void);
uint16_t Platform3DS_ReadKeyDownInput(void);
uint32_t Platform3DS_KeysHeld(void);
void Platform3DS_ReadCircle(float* x, float* y);
uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height);
uint64_t Platform3DS_Milliseconds(void);
void Platform3DS_WaitForVBlank(void);
void Platform3DS_ShowFatal(const char* title, const char* message);
void Platform3DS_Debug(const char* message);

#ifdef __cplusplus
}
#endif

#endif
