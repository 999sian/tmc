#ifndef PORT_PPU_H
#define PORT_PPU_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the PPU renderer (call after SDL_CreateWindow)
void Port_PPU_Init(SDL_Window* window);

// Read GBA DISPCNT mode bits and render the current frame via ViruaPPU,
// then present it to the SDL window. Call once per VBlank.
void Port_PPU_PresentFrame(void);

// Update the SDL window title used by the port.
void Port_PPU_SetWindowTitle(const char* title);

// Toggle borderless desktop fullscreen on the SDL window.
void Port_PPU_ToggleFullscreen(void);
bool Port_PPU_IsFullscreen(void);
void Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);

// Toggle nearest-neighbor (sharp pixels) ↔ linear (smooth) upscale filter.
void Port_PPU_ToggleSmoothing(void);
void Port_PPU_CyclePresentationMode(int direction);
const char* Port_PPU_PresentationModeName(void);

// Cleanup
void Port_PPU_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_PPU_H
