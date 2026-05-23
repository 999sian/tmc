#pragma once

/*
 * port_gpu_renderer.h — SDL_GPU-backed presentation path (Stage 1 scaffold).
 *
 * The default presentation goes through SDL_Renderer in port_ppu.cpp.
 * SDL_Renderer doesn't expose custom fragment shaders, so adding
 * libretro-style shader filters (CRT, scanlines, lcd-grid as
 * single/multi-pass GLSL) requires migrating to SDL_GPU.
 *
 * Stage 1 (current): scaffold only — init the device, compile the
 * passthrough vertex+fragment shaders from the SPIR-V blobs under
 * port/shaders/build/, log success. No frame is rendered through this
 * path yet; the SDL_Renderer presentation continues to drive the screen.
 *
 * Compile-time gate: pass `--gpu_renderer=y` to `xmake f` to enable.
 * Without the flag the entire module is a no-op linker stub.
 */

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Initialise the SDL_GPU device and load the passthrough shaders.
 * Returns true on success; logs failures to stderr and returns false
 * so the caller can fall back to the SDL_Renderer path. Safe to call
 * even when the binary was built without TMC_GPU_RENDERER — becomes
 * a no-op that returns true. */
bool Port_GPU_Init(SDL_Window* window);

/* Whether the GPU renderer was successfully initialised and is the
 * active presentation path. Stage 1 always returns false (we init
 * the device but don't yet drive present); kept as the integration
 * point Stage 2 will flip. */
bool Port_GPU_IsActive(void);

/* Release device + shader objects. Idempotent. */
void Port_GPU_Shutdown(void);

#ifdef __cplusplus
}
#endif
