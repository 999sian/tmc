#include "port_ppu.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_upscale.h"
#include "port_runtime_config.h"


#include <cpu/mode1.h>
#include <virtuappu.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

enum class RenderBackend {
    None,
    Renderer,
    Surface,
};

/* User-cycled presentation modes. F12 advances through these. */
enum class PresentMode {
    NearestRaw = 0,   /* upload 240x160 directly, nearest-neighbor stretch  */
    XbrzLinear,       /* xBRZ 4x → 960x640, linear stretch (smooth, default) */
    XbrzNearest,      /* xBRZ 4x → 960x640, nearest stretch (sharp)          */
    LinearRaw,        /* upload 240x160 directly, linear stretch (blurry)    */
    Count
};

static const int kHiResW = 960;
static const int kHiResH = 640;

static RenderBackend sBackend = RenderBackend::None;
static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sLowResTexture = nullptr;   /* 240x160 raw upload */
static SDL_Texture* sHiResTexture = nullptr;    /* 960x640 upscaled  */
static SDL_Window* sWindow = nullptr;
static SDL_Surface* sFrameSurface = nullptr;
static PresentMode sPresentMode = PresentMode::XbrzLinear;
static uint32_t* sUpscale2xBuf = nullptr;       /* 480x320 intermediate */
static uint32_t* sUpscale4xBuf = nullptr;       /* 960x640 final        */

static void Port_PPU_LoadConfig(void) {
    const char* method = Port_Config_UpscaleMethod();
    if (std::strcmp(method, "nearest") == 0) {
        sPresentMode = PresentMode::NearestRaw;
    } else if (std::strcmp(method, "linear") == 0) {
        sPresentMode = PresentMode::LinearRaw;
    } else if (std::strcmp(method, "xbrz_nearest") == 0) {
        sPresentMode = PresentMode::XbrzNearest;
    } else {
        sPresentMode = PresentMode::XbrzLinear;
    }
}

// Largest 240:160 (3:2) rect fitting inside (w, h), centered.
static void Port_PPU_ComputeFitRect(int w, int h, int* outX, int* outY, int* outW, int* outH) {
    int rw;
    int rh;
    if (w * 160 >= h * 240) {
        rh = h;
        rw = (h * 240) / 160;
    } else {
        rw = w;
        rh = (w * 160) / 240;
    }
    *outX = (w - rw) / 2;
    *outY = (h - rh) / 2;
    *outW = rw;
    *outH = rh;
}

static void Port_PPU_PresentSurfaceFrame(void) {
    SDL_Surface* windowSurface = SDL_GetWindowSurface(sWindow);
    int x;
    int y;
    int w;
    int h;
    SDL_Rect dstRect;

    if (!windowSurface) {
        return;
    }

    Port_PPU_ComputeFitRect(windowSurface->w, windowSurface->h, &x, &y, &w, &h);
    dstRect = {x, y, w, h};
    SDL_FillSurfaceRect(windowSurface, nullptr, 0);
    SDL_BlitSurfaceScaled(sFrameSurface, nullptr, windowSurface, &dstRect, SDL_SCALEMODE_NEAREST);
    SDL_UpdateWindowSurface(sWindow);
}

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;
    Port_PPU_LoadConfig();

    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    } else {
        sLowResTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888,
                                           SDL_TEXTUREACCESS_STREAMING, 240, 160);
        sHiResTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888,
                                          SDL_TEXTUREACCESS_STREAMING, kHiResW, kHiResH);
        if (!sLowResTexture || !sHiResTexture) {
            printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(sRenderer);
            sRenderer = nullptr;
        } else {
            sUpscale2xBuf = (uint32_t*)std::malloc((size_t)480 * 320 * sizeof(uint32_t));
            sUpscale4xBuf = (uint32_t*)std::malloc((size_t)kHiResW * kHiResH * sizeof(uint32_t));
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

    /* HBlank-DMA simulation: VirtuaPPU calls this before each scanline. */
    virtuappu_mode1_pre_line_callback = port_hdma_step_line;

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
        case 1:
            virtuappu_registers.mode = 1;
            break;
        
        case 2:
            virtuappu_registers.mode = 2;
            break;
        default:
            virtuappu_registers.mode = 1;
            break;
    }

    virtuappu_render_frame();

    if (sBackend == RenderBackend::Renderer) {
        int outW = 0;
        int outH = 0;
        SDL_GetCurrentRenderOutputSize(sRenderer, &outW, &outH);
        int x;
        int y;
        int w;
        int h;
        Port_PPU_ComputeFitRect(outW, outH, &x, &y, &w, &h);
        SDL_FRect dst = { (float)x, (float)y, (float)w, (float)h };

        SDL_Texture* tex;
        SDL_ScaleMode scale;
        switch (sPresentMode) {
            case PresentMode::XbrzLinear:
            case PresentMode::XbrzNearest:
                Port_Upscale_xBRZ_4x(virtuappu_frame_buffer, 240, 160,
                                     sUpscale2xBuf, sUpscale4xBuf);
                SDL_UpdateTexture(sHiResTexture, nullptr, sUpscale4xBuf,
                                  kHiResW * (int)sizeof(uint32_t));
                tex = sHiResTexture;
                scale = (sPresentMode == PresentMode::XbrzLinear)
                            ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            case PresentMode::LinearRaw:
                SDL_UpdateTexture(sLowResTexture, nullptr, virtuappu_frame_buffer,
                                  240 * (int)sizeof(uint32_t));
                tex = sLowResTexture;
                scale = SDL_SCALEMODE_LINEAR;
                break;
            case PresentMode::NearestRaw:
            default:
                SDL_UpdateTexture(sLowResTexture, nullptr, virtuappu_frame_buffer,
                                  240 * (int)sizeof(uint32_t));
                tex = sLowResTexture;
                scale = SDL_SCALEMODE_NEAREST;
                break;
        }
        SDL_SetTextureScaleMode(tex, scale);
        SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sRenderer);
        SDL_RenderTexture(sRenderer, tex, nullptr, &dst);
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

extern "C" void Port_PPU_ToggleFullscreen(void) {
    if (!sWindow) {
        return;
    }
    SDL_WindowFlags flags = SDL_GetWindowFlags(sWindow);
    bool wantFullscreen = (flags & SDL_WINDOW_FULLSCREEN) == 0;
    SDL_SetWindowFullscreen(sWindow, wantFullscreen);
    SDL_SyncWindow(sWindow);
}

extern "C" void Port_PPU_ToggleSmoothing(void) {
    static const char* const kNames[] = {
        "nearest (sharp pixels)",
        "xBRZ 4x + linear (smooth, default)",
        "xBRZ 4x + nearest (crisp upscale)",
        "linear (blurry stretch)",
    };
    int next = ((int)sPresentMode + 1) % (int)PresentMode::Count;
    sPresentMode = (PresentMode)next;
    fprintf(stderr, "PPU upscale: %s\n", kNames[next]);
}

extern "C" void Port_PPU_Shutdown(void) {
    if (sWindow && sBackend == RenderBackend::Surface) {
        SDL_DestroyWindowSurface(sWindow);
    }
    if (sFrameSurface) {
        SDL_DestroySurface(sFrameSurface);
        sFrameSurface = nullptr;
    }
    if (sLowResTexture) {
        SDL_DestroyTexture(sLowResTexture);
        sLowResTexture = nullptr;
    }
    if (sHiResTexture) {
        SDL_DestroyTexture(sHiResTexture);
        sHiResTexture = nullptr;
    }
    if (sUpscale2xBuf) {
        std::free(sUpscale2xBuf);
        sUpscale2xBuf = nullptr;
    }
    if (sUpscale4xBuf) {
        std::free(sUpscale4xBuf);
        sUpscale4xBuf = nullptr;
    }
    if (sRenderer) {
        SDL_DestroyRenderer(sRenderer);
        sRenderer = nullptr;
    }
    sBackend = RenderBackend::None;
    sWindow = nullptr;
}
