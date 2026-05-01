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
void Port_Config_OpenGamepads(void);
void Port_Config_HandleEvent(const SDL_Event* e);
bool Port_Config_InputPressed(PortInput input);
void Port_Config_CloseGamepads(void);

#ifdef __cplusplus
}
#endif
