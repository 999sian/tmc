#include "main.h"
#include "port_offset_USA.h"
#include "port_types.h"
#include "stdio.h"
#include <SDL3/SDL.h>
int main() {

    printf("Initializing port layer...\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != true) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // create window, renderer, etc. here as needed for the port layer

    SDL_Window* window = SDL_CreateWindow("Port Layer Test", 800, 600, 0);
    if (window == NULL) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    printf("Port layer initialized successfully.\n");

    AgbMain();
    SDL_Quit();
    return 0;
}