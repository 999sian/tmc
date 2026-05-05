#pragma once

#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_INPUT_A,
    PORT_INPUT_B,
    PORT_INPUT_SELECT,
    PORT_INPUT_START,
    PORT_INPUT_RIGHT,
    PORT_INPUT_LEFT,
    PORT_INPUT_UP,
    PORT_INPUT_DOWN,
    PORT_INPUT_R,
    PORT_INPUT_L,
    PORT_INPUT_COUNT,
} PortInput;

void Port_Config_Load(const char* path);
u8 Port_Config_WindowScale(void);
const char* Port_Config_UpscaleMethod(void);
u64 Port_Config_FrameTimeNs(void);
u32 Port_Config_TargetFps(void);
bool Port_Config_PortSettingsMenuEnabled(void);
void Port_Config_SetWindowScale(u8 scale);
void Port_Config_SetUpscaleMethod(const char* method);
void Port_Config_SetTargetFps(u32 fps);
void Port_Config_CycleTargetFps(int direction);

/* Internal render-resolution multiplier. The PPU normally renders at the
 * GBA-native 240x160; with scale=N>1 it produces a 240*N by 160*N
 * framebuffer, with sub-pixel sampling on affine paths (OAM affine,
 * mode2 BG2, mode7) so rotated layers and scaled sprites stop staircase-
 * aliasing. Text BGs and non-affine sprites are simply S*S nearest-
 * replicated — pixel-art has no information to recover at higher density.
 * Range 1..4 (capped by PPU framebuffer height of 640 = 160*4). */
u8 Port_Config_InternalScale(void);
void Port_Config_SetInternalScale(u8 scale);
void Port_Config_CycleInternalScale(int direction);
void Port_Config_OpenGamepads(void);
void Port_Config_HandleEvent(const SDL_Event* e);
bool Port_Config_InputPressed(PortInput input);
void Port_Config_CloseGamepads(void);

#ifdef __cplusplus
}
#endif
