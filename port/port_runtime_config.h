#pragma once

#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

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
    /* Extra equip buttons (port_softslots.c). Each maps to a configurable
     * key/pad/trigger and is read every frame to decide which item — if any —
     * fires through the B-dispatch path. */
    PORT_INPUT_SOFT_X,
    PORT_INPUT_SOFT_Y,
    PORT_INPUT_SOFT_L2,
    PORT_INPUT_SOFT_R2,
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

typedef enum {
    PORT_TOUCH_SCHEME_JOYSTICK = 0,
    PORT_TOUCH_SCHEME_DPAD     = 1,
} PortTouchScheme;
PortTouchScheme Port_Config_TouchScheme(void);
void Port_Config_SetTouchScheme(PortTouchScheme scheme);
void Port_Config_CycleTouchScheme(int direction);

/* Aspect-ratio mode for the on-screen viewport. The GBA frame is always
 * rendered at its native 3:2 aspect — this knob picks the *stage* it
 * sits inside. Modes wider than 3:2 add pillar bars around the frame
 * (filled per PortBgFill); modes equal to the window's natural aspect
 * fill the whole window. Native means "no constraint, frame fills as
 * much of the window as 3:2 allows" — the historical default. */
typedef enum {
    PORT_ASPECT_NATIVE_3_2          = 0,
    PORT_ASPECT_WIDESCREEN_16_9     = 1,
    PORT_ASPECT_ULTRAWIDE_21_9      = 2,
    PORT_ASPECT_SUPER_ULTRAWIDE_32_9 = 3,
    PORT_ASPECT_COUNT,
} PortAspectMode;
PortAspectMode Port_Config_AspectMode(void);
const char*    Port_Config_AspectModeName(PortAspectMode mode);
void           Port_Config_SetAspectMode(PortAspectMode mode);
void           Port_Config_CycleAspectMode(int direction);

/* Fill style for the area around the GBA frame (the "pillar bars" or
 * "stage background"). Black is the historical default. Solid uses the
 * persisted RGB triple from Port_Config_BgFillColor*. Blurred stretches
 * a linearly-filtered copy of the current frame across the stage,
 * giving a soft halo / "ambient mode" effect. */
typedef enum {
    PORT_BG_FILL_BLACK         = 0,
    PORT_BG_FILL_SOLID_COLOR   = 1,
    PORT_BG_FILL_BLURRED_FRAME = 2,
    PORT_BG_FILL_COUNT,
} PortBgFill;
PortBgFill  Port_Config_BgFill(void);
const char* Port_Config_BgFillName(PortBgFill fill);
void        Port_Config_SetBgFill(PortBgFill fill);
void        Port_Config_CycleBgFill(int direction);
/* RGB triple, 0..255, used when BgFill == SOLID_COLOR. */
void        Port_Config_BgFillColor(u8* r, u8* g, u8* b);
void        Port_Config_SetBgFillColor(u8 r, u8 g, u8 b);

/* Renderer backend selection. The port has two presentation paths:
 * SDL_GPU (shader pipeline, required for CRT/LCD filters and the
 * .glslp preset runtime) and SDL_Renderer (software composite via
 * SDL's 2D API). AUTO is the historical default — try GPU first,
 * fall back to SDL_Renderer if GPU init fails. Changing this needs
 * a restart to take effect; the window's swapchain owner is set
 * once at Port_PPU_Init time. */
typedef enum {
    PORT_RENDER_BACKEND_AUTO     = 0,
    PORT_RENDER_BACKEND_SOFTWARE = 1,
    PORT_RENDER_BACKEND_GPU      = 2,
    PORT_RENDER_BACKEND_COUNT,
} PortRenderBackend;
PortRenderBackend Port_Config_RenderBackend(void);
const char*       Port_Config_RenderBackendName(PortRenderBackend b);
void              Port_Config_SetRenderBackend(PortRenderBackend b);
void              Port_Config_CycleRenderBackend(int direction);

/* Text-to-speech accessibility settings — read by port_tts at startup
 * and re-written on every setter. All persisted to config.json. */
bool        Port_Config_GetTtsEnabled(void);
void        Port_Config_SetTtsEnabled(bool on);
float       Port_Config_GetTtsRate(void);
void        Port_Config_SetTtsRate(float v);
float       Port_Config_GetTtsPitch(void);
void        Port_Config_SetTtsPitch(float v);
float       Port_Config_GetTtsVolume(void);
void        Port_Config_SetTtsVolume(float v);
const char* Port_Config_GetTtsVoice(void);
void        Port_Config_SetTtsVoice(const char* v);
const char* Port_Config_GetTtsLanguage(void);
void        Port_Config_SetTtsLanguage(const char* v);

void Port_Config_OpenGamepads(void);
void Port_Config_HandleEvent(const SDL_Event* e);
bool Port_Config_InputPressed(PortInput input);
void Port_Config_CloseGamepads(void);

/* Soft-slot input poll, indexed 0..3 (X, Y, L2, R2). */
bool Port_Config_SoftSlotPressed(int slot);

/* Raw left-analog-stick reading from the first attached gamepad, in
 * the range [-1, 1] per axis (positive Y = downward on screen, matching
 * TMC's top-left coord system). Returns false with outputs untouched
 * when no pad is attached. Used by the 360° analog movement toggle in
 * src/code_0805EC04.c::UpdatePlayerInput. */
bool Port_Config_GetLeftStick(float* outX, float* outY);

/* Clear the per-input "pressed this frame" edge cache. Call after the
 * port has committed KEYINPUT and the engine has read it, so the next
 * frame's polled state isn't stuck reporting the previous tap. */
void Port_Config_ClearInputEdges(void);

#ifdef launcher
void Port_Config_SetPortSettingsMenuEnabled(bool enabled);
const char* Port_Config_InputUiLabel(PortInput input);
void Port_Config_FormatBindingsLine(PortInput input, char* out, size_t outCap);
void Port_Config_SetKeyboardBindExclusive(PortInput input, int sdl_keycode);
void Port_Config_FormatGamepadBindingsLine(PortInput input, char* out, size_t outCap);
void Port_Config_SetGamepadBindExclusive(PortInput input, int sdl_gamepad_button);
#endif

#ifdef __cplusplus
}
#endif
