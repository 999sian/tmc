#ifndef PORT_PRELAUNCH_LOGO_H
#define PORT_PRELAUNCH_LOGO_H

#include <SDL3/SDL.h>
#include <imgui.h>

#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL_gpu.h>
#else
typedef struct SDL_GPUDevice SDL_GPUDevice;
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool Port_PrelaunchLogo_EnsureLoaded(SDL_Renderer* renderer, SDL_GPUDevice* gpu_device);
ImTextureID Port_PrelaunchLogo_GetTexId(void);
void Port_PrelaunchLogo_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PRELAUNCH_LOGO_H */
