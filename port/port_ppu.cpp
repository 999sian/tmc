#include "port_ppu.h"

#include <VirtuaPPU.hpp>

#include <cstdint>
#include <cstdio>

static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sTexture = nullptr;

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }

    // GBA native resolution
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 240, 160);
    if (!sTexture) {
        printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(sTexture, SDL_SCALEMODE_NEAREST);

    // Configure ViruaPPU registers
    global_Registers.frame_width = 240;
    global_Registers.mode = 1; // default to Mode 1 (GBA Mode 0)

    printf("PPU initialized (240x160, nearest-neighbor scaling).\n");
}

extern "C" void Port_PPU_PresentFrame(void) {
    if (!sRenderer || !sTexture)
        return;

    // Read GBA DISPCNT to pick the right PPU mode
    uint16_t dispcnt = gIoMem[0x00] | (gIoMem[0x01] << 8);

    uint8_t gbaMode = dispcnt & 0x07;

    switch (gbaMode) {
        case 0:
            global_Registers.mode = 1;
            break; // GBA Mode 0 → ViruaPPU Mode 1
        case 1:
            global_Registers.mode = 2;
            break; // GBA Mode 1 → ViruaPPU Mode 2
        default:
            // Modes 2-5 not implemented yet
            break;
    }

    // Render the frame into ViruaPPU's frame_buffer
    RenderFrame();

    // Upload to SDL texture
    SDL_UpdateTexture(sTexture, nullptr, frame_buffer, 240 * sizeof(uint32_t));

    SDL_RenderClear(sRenderer);
    SDL_RenderTexture(sRenderer, sTexture, nullptr, nullptr);
    SDL_RenderPresent(sRenderer);
}

extern "C" void Port_PPU_Shutdown(void) {
    if (sTexture) {
        SDL_DestroyTexture(sTexture);
        sTexture = nullptr;
    }
    if (sRenderer) {
        SDL_DestroyRenderer(sRenderer);
        sRenderer = nullptr;
    }
}
