#ifndef TMC_PLATFORM_3DS_H
#define TMC_PLATFORM_3DS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int Platform3DS_Init(void);
void Platform3DS_Shutdown(void);
bool Platform3DS_IsRunning(void);
bool Platform3DS_IsNew3DS(void);
bool Platform3DS_CanUseCore1(void);
unsigned Platform3DS_Core1TimeLimit(void);
void Platform3DS_ShowSplash(void);
uint16_t Platform3DS_ReadKeyInput(void);
uint16_t Platform3DS_ReadKeyDownInput(void);
uint32_t Platform3DS_KeysHeld(void);
void Platform3DS_ReadCircle(float* x, float* y);
uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height);
uint64_t Platform3DS_Milliseconds(void);
uint64_t Platform3DS_SystemTick(void);
uint64_t Platform3DS_TicksPerSecond(void);
bool Platform3DS_SubmitBottomWorker(void);
bool Platform3DS_TryFinishBottomWorker(void);
void Platform3DS_ShutdownBottomWorker(void);
void Platform3DS_WaitForVBlank(void);
void Platform3DS_ShowFatal(const char* title, const char* message);
void Platform3DS_Debug(const char* message);
bool Platform3DS_SaveDisplayedScreens(const char* topPath, const char* bottomPath);

#ifdef __cplusplus
}
#endif

#endif
