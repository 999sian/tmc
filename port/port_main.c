#include "gba/io_reg.h"
#include "main.h"
#include "port_gba_mem.h"
#include "port_offset_USA.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_types.h"
#include "stdio.h"
#include <SDL3/SDL.h>
int main() {

    fprintf(stderr, "Initializing port layer...\n");

    // Initialize REG_KEYINPUT to all-keys-released (GBA: 1=not pressed)
    *(u16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;

    // Load ROM data (graphics, palettes, pointer tables)
    Port_LoadRom("baserom.gba");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != true) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // GBA resolution × 3
    SDL_Window* window = SDL_CreateWindow("The Minish Cap", 240 * 3, 160 * 3, 0);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Initialize PPU renderer
    Port_PPU_Init(window);

    fprintf(stderr, "Port layer initialized. Entering AgbMain...\n");

    AgbMain();

    Port_PPU_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}