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
/* Internal-render-scale streaming texture: re-sized lazily when scale
 * changes (240*S x 160*S). Used when Port_Config_InternalScale() > 1
 * and the user has chosen a non-xBRZ presentation mode — the framebuffer
 * is S*S nearest-replicated into sScaledBuf and uploaded here. */
static SDL_Texture* sScaledTexture = nullptr;
static int sScaledTextureScale = 0;
static uint32_t* sScaledBuf = nullptr;
static int sScaledBufScale = 0;
static SDL_Window* sWindow = nullptr;
static SDL_Surface* sFrameSurface = nullptr;
static PresentMode sPresentMode = PresentMode::NearestRaw;
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

static const char* Port_PPU_MethodForMode(PresentMode mode) {
    switch (mode) {
        case PresentMode::NearestRaw:
            return "nearest";
        case PresentMode::LinearRaw:
            return "linear";
        case PresentMode::XbrzNearest:
            return "xbrz_nearest";
        case PresentMode::XbrzLinear:
        default:
            return "xbrz_linear";
    }
}

extern "C" const char* Port_PPU_PresentationModeName(void) {
    static const char* const kNames[] = {
        "nearest",
        "xBRZ smooth",
        "xBRZ sharp",
        "linear",
    };
    return kNames[(int)sPresentMode];
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

/* Build (or reuse) sScaledBuf at scale S and S*S-replicate the 240x160
 * framebuffer into it. Returns the buffer + dims via out-params; returns
 * nullptr if S<=1. The buffer survives across frames so we don't realloc
 * unless the scale changes.
 *
 * This is the Stage-1 shape of internal-render-scale: pure post-process
 * nearest-replicate on the CPU. By itself it produces visually the same
 * result as SDL_SCALEMODE_NEAREST presentation, but it puts the scaled
 * framebuffer in the pipeline so future PPU patches can render affine
 * paths directly at sub-pixel density and the rest of the path doesn't
 * need to change. */
static uint32_t* Port_PPU_BuildScaledFrame(int S, int* outW, int* outH) {
    if (S <= 1) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return nullptr;
    }
    const int w = 240 * S;
    const int h = 160 * S;
    if (sScaledBuf == nullptr || sScaledBufScale != S) {
        std::free(sScaledBuf);
        sScaledBuf = (uint32_t*)std::malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        sScaledBufScale = S;
        if (sScaledBuf == nullptr) {
            sScaledBufScale = 0;
            if (outW) *outW = 0;
            if (outH) *outH = 0;
            return nullptr;
        }
    }
    /* Nearest-replicate: each src pixel writes to an SxS block. Loop
     * order is src-major so the source line stays cache-resident while
     * we scatter S output rows. */
    for (int sy = 0; sy < 160; ++sy) {
        const uint32_t* src = &virtuappu_frame_buffer[sy * 240];
        for (int dy = 0; dy < S; ++dy) {
            uint32_t* dst = &sScaledBuf[(sy * S + dy) * w];
            for (int sx = 0; sx < 240; ++sx) {
                uint32_t c = src[sx];
                uint32_t* d = &dst[sx * S];
                for (int dx = 0; dx < S; ++dx) {
                    d[dx] = c;
                }
            }
        }
    }

    /* Sub-pixel overlay for OAM affine sprites. Most TMC frames have no
     * affine OAM and this is a no-op; when there is one (Vaati tornado,
     * world-shrink cinematic, every spinning enemy) it overwrites the
     * 1x-replicated pixels with sub-pixel-accurate ones. */
    virtuappu_mode1_render_affine_obj_overlay(sScaledBuf, w, h, S);

    if (outW) *outW = w;
    if (outH) *outH = h;
    return sScaledBuf;
}

static SDL_Texture* Port_PPU_EnsureScaledTexture(int S) {
    if (S <= 1) return nullptr;
    if (sScaledTexture != nullptr && sScaledTextureScale == S) {
        return sScaledTexture;
    }
    if (sScaledTexture != nullptr) {
        SDL_DestroyTexture(sScaledTexture);
        sScaledTexture = nullptr;
        sScaledTextureScale = 0;
    }
    sScaledTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STREAMING, 240 * S, 160 * S);
    if (sScaledTexture) {
        sScaledTextureScale = S;
    }
    return sScaledTexture;
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

static bool sVSyncEnabled = true;

extern "C" void Port_PPU_SetVSync(bool enabled) {
    if (sRenderer == nullptr) {
        sVSyncEnabled = enabled;
        return;
    }
    if (sVSyncEnabled == enabled) {
        return;
    }
    sVSyncEnabled = enabled;
    SDL_SetRenderVSync(sRenderer, enabled ? 1 : 0);
}

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;
    Port_PPU_LoadConfig();

    /* Reuse the renderer the bootstrap progress UI created (if any)
     * instead of destroying it and making a new one. SDL only allows
     * one renderer per window, and recreating it on the same window
     * causes a visible compositor flash on most platforms — exactly
     * what made the asset-extractor screen look like a separate
     * window from the game. SDL_GetRenderer returns NULL when no
     * renderer has been associated with the window, in which case we
     * fall back to creating one ourselves. */
    sRenderer = SDL_GetRenderer(window);
    if (!sRenderer) {
        sRenderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    } else {
        if (!SDL_SetRenderVSync(sRenderer, 1)) {
            printf("Port_PPU_Init: SDL_SetRenderVSync failed: %s\n", SDL_GetError());
        }
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

    /* GBA mode 1 = BG0/BG1 text + BG2 affine + OBJ. VirtuaPPU's mode 2
     * matches that hardware behaviour; routing GBA mode 1 to VirtuaPPU mode
     * 1 reads BG2 with text-BG indexing and the title-screen affine sword
     * comes out as garbage tiles. Keep GBA mode 0 on VirtuaPPU mode 1.
     * (Originally fixed in ad9b4d94, regressed in matheo merge dec390c2.) */
    switch (gbaMode) {
        case 0:
            virtuappu_registers.mode = 1;
            break;
        case 1:
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
        const int internalS = (int)Port_Config_InternalScale();
        switch (sPresentMode) {
            case PresentMode::XbrzLinear:
            case PresentMode::XbrzNearest:
                /* xBRZ owns its own 4x upscaler — internal-render-scale
                 * is mutually exclusive with it. The xBRZ path always
                 * consumes the unscaled 240x160 framebuffer. */
                Port_Upscale_xBRZ_4x(virtuappu_frame_buffer, 240, 160,
                                     sUpscale2xBuf, sUpscale4xBuf);
                SDL_UpdateTexture(sHiResTexture, nullptr, sUpscale4xBuf,
                                  kHiResW * (int)sizeof(uint32_t));
                tex = sHiResTexture;
                scale = (sPresentMode == PresentMode::XbrzLinear)
                            ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            case PresentMode::LinearRaw:
            case PresentMode::NearestRaw:
            default: {
                int sw = 0, sh = 0;
                uint32_t* scaled = Port_PPU_BuildScaledFrame(internalS, &sw, &sh);
                SDL_Texture* scaledTex = Port_PPU_EnsureScaledTexture(internalS);
                if (scaled && scaledTex) {
                    SDL_UpdateTexture(scaledTex, nullptr, scaled, sw * (int)sizeof(uint32_t));
                    tex = scaledTex;
                } else {
                    SDL_UpdateTexture(sLowResTexture, nullptr, virtuappu_frame_buffer,
                                      240 * (int)sizeof(uint32_t));
                    tex = sLowResTexture;
                }
                scale = (sPresentMode == PresentMode::LinearRaw)
                            ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            }
        }
        SDL_SetTextureScaleMode(tex, scale);
        SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sRenderer);
        SDL_RenderTexture(sRenderer, tex, nullptr, &dst);
        {
            extern void Port_DebugMenu_Render(SDL_Renderer*, int, int);
            Port_DebugMenu_Render(sRenderer, outW, outH);
        }
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

extern "C" bool Port_PPU_IsFullscreen(void) {
    if (!sWindow) {
        return false;
    }
    return (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}

extern "C" unsigned char Port_PPU_WindowScale(void) {
    return Port_Config_WindowScale();
}

extern "C" void Port_PPU_CycleWindowScale(int direction) {
    u8 scale = Port_Config_WindowScale();
    if (direction < 0) {
        scale = scale <= 1 ? 10 : (u8)(scale - 1);
    } else {
        scale = scale >= 10 ? 1 : (u8)(scale + 1);
    }
    Port_Config_SetWindowScale(scale);
    if (sWindow && !Port_PPU_IsFullscreen()) {
        SDL_SetWindowSize(sWindow, 240 * scale, 160 * scale);
        SDL_SyncWindow(sWindow);
    }
}

extern "C" void Port_PPU_CyclePresentationMode(int direction) {
    int next = (int)sPresentMode + (direction < 0 ? -1 : 1);
    if (next < 0) {
        next = (int)PresentMode::Count - 1;
    } else if (next >= (int)PresentMode::Count) {
        next = 0;
    }
    sPresentMode = (PresentMode)next;
    Port_Config_SetUpscaleMethod(Port_PPU_MethodForMode(sPresentMode));
    fprintf(stderr, "PPU upscale: %s\n", Port_PPU_PresentationModeName());
}

extern "C" void Port_PPU_ToggleSmoothing(void) {
    Port_PPU_CyclePresentationMode(1);
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
