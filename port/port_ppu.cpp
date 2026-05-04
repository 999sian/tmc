#include "port_ppu.h"
#include "port_gba_mem.h"

#include <cpu/mode1.h>
#include <virtuappu.h>

#include <cstdint>
#include <cstdio>

enum class RenderBackend {
    None,
    Renderer,
    Surface,
};

static RenderBackend sBackend = RenderBackend::None;
static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sTexture = nullptr;
static SDL_Window* sWindow = nullptr;
static SDL_Surface* sFrameSurface = nullptr;

static void Port_PPU_PresentSurfaceFrame(void) {
    SDL_Surface* windowSurface = SDL_GetWindowSurface(sWindow);
    if (!windowSurface) {
        return;
    }

    SDL_Rect dstRect = {0, 0, windowSurface->w, windowSurface->h};
    SDL_FillSurfaceRect(windowSurface, nullptr, 0);
    SDL_BlitSurfaceScaled(sFrameSurface, nullptr, windowSurface, &dstRect, SDL_SCALEMODE_NEAREST);
    SDL_UpdateWindowSurface(sWindow);
}

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;

    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    } else {
        sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 240, 160);
        if (!sTexture) {
            printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(sRenderer);
            sRenderer = nullptr;
        } else {
            SDL_SetTextureScaleMode(sTexture, SDL_SCALEMODE_NEAREST);
            sBackend = RenderBackend::Renderer;
        }
    }

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

    if (sBackend == RenderBackend::None) {
        sFrameSurface = SDL_CreateSurfaceFrom(
            240,
            160,
            SDL_PIXELFORMAT_ABGR8888,
            virtuappu_frame_buffer,
            240 * static_cast<int>(sizeof(uint32_t)));
        if (!sFrameSurface) {
            printf("Port_PPU_Init: SDL_CreateSurfaceFrom failed: %s\n", SDL_GetError());
            return;
        }

        if (!SDL_SetWindowSurfaceVSync(window, 1)) {
            printf("Port_PPU_Init: SDL_SetWindowSurfaceVSync failed: %s\n", SDL_GetError());
        }

        sBackend = RenderBackend::Surface;
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
        SDL_SyncWindow(window);
        Port_PPU_PresentSurfaceFrame();
        printf("PPU initialized with SDL window surface fallback.\n");
    } else {
        printf("PPU initialized with SDL renderer backend.\n");
    }
}

extern "C" void Port_PPU_PresentFrame(void) {
    uint16_t dispcnt;
    uint8_t gbaMode;

    if (sBackend == RenderBackend::None) {
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

    if (sBackend == RenderBackend::Renderer) {
        SDL_UpdateTexture(sTexture, nullptr, virtuappu_frame_buffer, 240 * sizeof(uint32_t));
        SDL_RenderClear(sRenderer);
        SDL_RenderTexture(sRenderer, sTexture, nullptr, nullptr);
        SDL_RenderPresent(sRenderer);
        return;
    }

    Port_PPU_PresentSurfaceFrame();
}

extern "C" void Port_PPU_SetWindowTitle(const char* title) {
    if (!sWindow || !title) {
        return;
    }
    SDL_SetWindowTitle(sWindow, title);
}

extern "C" void Port_PPU_Shutdown(void) {
    if (sWindow && sBackend == RenderBackend::Surface) {
        SDL_DestroyWindowSurface(sWindow);
    }
    if (sFrameSurface) {
        SDL_DestroySurface(sFrameSurface);
        sFrameSurface = nullptr;
    }
    if (sTexture) {
        SDL_DestroyTexture(sTexture);
        sTexture = nullptr;
    }
    if (sRenderer) {
        SDL_DestroyRenderer(sRenderer);
        sRenderer = nullptr;
    }
    sBackend = RenderBackend::None;
    sWindow = nullptr;
}
