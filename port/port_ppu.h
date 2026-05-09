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

// CRT/LCD post-process filter cycling (off / warm-composite-AG /
// LCD-grid / warm-RF-AG). Applied at the upscaled resolution before
// SDL upload — needs internal-scale >= 2 to be visible.
void Port_PPU_CycleFilter(int direction);
const char* Port_PPU_FilterName(void);

// Toggle SDL vsync on the renderer. Used by VBlankIntrWait to disable the
// display-refresh cap when fast-forwarding or when target FPS exceeds the
// monitor refresh rate; without this, fast-forward (#26) and FPS presets
// > 60 are limited by the display, not by the busy-wait timer.
void Port_PPU_SetVSync(bool enabled);

// Cleanup
void Port_PPU_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_PPU_H
