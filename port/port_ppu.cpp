#include "port_ppu.h"
#include "port_gba_mem.h"

#include <cpu/mode1.h>
#include <virtuappu.h>

#include <cstdint>
#include <cstdio>

static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sTexture = nullptr;
static SDL_Window* sWindow = nullptr;

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;
    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }

    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 240, 160);
    if (!sTexture) {
        printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(sTexture, SDL_SCALEMODE_NEAREST);

    {
        VirtuaPPUMode1GbaMemory memory = {
            gIoMem,
            gVram,
            gBgPltt,
            gObjPltt,
            gOamMem,
        };
        virtuappu_mode1_bind_gba_memory(&memory);
    }

    virtuappu_registers.frame_width = 240;
    virtuappu_registers.mode = 1;

    printf("PPU initialized (240x160, nearest-neighbor scaling).\n");
}

extern "C" void Port_PPU_PresentFrame(void) {
    uint16_t dispcnt;
    uint8_t gbaMode;

    if (!sRenderer || !sTexture) {
        return;
    }

    dispcnt = (uint16_t)(gIoMem[0x00] | (gIoMem[0x01] << 8));
    gbaMode = (uint8_t)(dispcnt & 0x07);

    switch (gbaMode) {
        case 0:
            virtuappu_registers.mode = 1;
            break;
        case 1:
            virtuappu_registers.mode = 2;
            break;
        default:
            break;
    }

    virtuappu_render_frame();

    SDL_UpdateTexture(sTexture, nullptr, virtuappu_frame_buffer, 240 * sizeof(uint32_t));
    SDL_RenderClear(sRenderer);
    SDL_RenderTexture(sRenderer, sTexture, nullptr, nullptr);
    SDL_RenderPresent(sRenderer);
}

extern "C" void Port_PPU_SetWindowTitle(const char* title) {
    if (!sWindow || !title) {
        return;
    }
    SDL_SetWindowTitle(sWindow, title);
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
    sWindow = nullptr;
}
