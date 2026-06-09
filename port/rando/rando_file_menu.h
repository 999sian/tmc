#ifndef PORT_RANDO_FILE_MENU_H
#define PORT_RANDO_FILE_MENU_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Port_RandoFileMenu_SetWindow(SDL_Window* window);
bool Port_RandoFileMenu_ShouldOpenForNewFile(void);
void Port_RandoFileMenu_Open(int save_slot);
void Port_RandoFileMenu_Close(void);
bool Port_RandoFileMenu_IsOpen(void);
bool Port_RandoFileMenu_HandleEvent(const SDL_Event* event);
void Port_RandoFileMenu_Render(SDL_Renderer* renderer, int window_width, int window_height);

/* Spec-facing names. They are thin wrappers over the port-prefixed API. */
void ProcessFileMenuInput(SDL_Event* event);
void RenderFileMenuUI(SDL_Renderer* renderer);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_FILE_MENU_H */
