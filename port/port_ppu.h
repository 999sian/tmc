#ifndef PORT_PPU_H
#define PORT_PPU_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the PPU renderer (call after SDL_CreateWindow)
void Port_PPU_Init(SDL_Window* window);

// Read GBA DISPCNT mode bits and render the current frame via ViruaPPU,
// then present it to the SDL window. Call once per VBlank.
void Port_PPU_PresentFrame(void);

// Cleanup
void Port_PPU_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_PPU_H
