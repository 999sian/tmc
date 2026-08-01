#ifndef PLATFORM_GPU_3DS_H
#define PLATFORM_GPU_3DS_H

#include <stdbool.h>
#include <stdint.h>

bool PlatformGpu3DS_Init(void);
uint32_t* PlatformGpu3DS_TopBuffer(void);
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index);
void PlatformGpu3DS_BeginTop(const uint32_t* pixels);
void PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed);
void PlatformGpu3DS_Shutdown(void);

#endif
