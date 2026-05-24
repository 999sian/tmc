#include "port_ppu.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_upscale.h"
#include "port_runtime_config.h"
#include "port_filter.h"
#include "port_gpu_renderer.h"  /* PortGpuFilter for the GPU-backend filter cycle */
#include "port_touch_controls.h"

#ifdef launcher
#include "tmc_launcher.h"
#endif

#include <cpu/mode1.h>
#include <virtuappu.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Manual access to gMain (the engine's Main struct): including main.h
 * would pull in player.h, which uses `this` as a C parameter name and
 * doesn't compile as C++. Treat the symbol as opaque bytes and read the
 * task field at known offset 2 (interruptFlag, sleepStatus, task —
 * include/main.h). C linkage matches the engine's Main gMain. */
extern "C" uint8_t gMain[]; // fix for MSVC (UWP PORT)

enum class RenderBackend {
    None,
    Renderer,
    Surface,
    Gpu,       /* Stage 2: SDL_GPU pipeline owns the swapchain */
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
#ifdef launcher
static SDL_Window* sBootstrapWindow = nullptr;
#endif

static SDL_Window* Port_PPU_ActiveWindow(void) {
#ifdef launcher
    return sWindow ? sWindow : sBootstrapWindow;
#else
    return sWindow;
#endif
}

#ifdef launcher
extern "C" void Port_SetBootstrapWindow(SDL_Window* window) {
    sBootstrapWindow = window;
}
#endif
static SDL_Surface* sFrameSurface = nullptr;
static PresentMode sPresentMode = PresentMode::NearestRaw;
static PortFilterType sFilter = PORT_FILTER_NONE;
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

// Largest rect with aspect aspW:aspH fitting inside (w, h), centered.
static void Port_PPU_FitAspectRect(int w, int h, int aspW, int aspH,
                                   int* outX, int* outY, int* outW, int* outH) {
    int rw;
    int rh;
    if (w * aspH >= h * aspW) {
        rh = h;
        rw = (h * aspW) / aspH;
    } else {
        rw = w;
        rh = (w * aspH) / aspW;
    }
    *outX = (w - rw) / 2;
    *outY = (h - rh) / 2;
    *outW = rw;
    *outH = rh;
}

// Largest GBA-aspect rect fitting inside (w, h), centered. Aspect uses
// MODE1_GBA_WIDTH so the widescreen spike (override via -DMODE1_GBA_WIDTH)
// keeps the rendered rect's proportions matching the framebuffer rather
// than letterboxing the wider content.
static void Port_PPU_ComputeFitRect(int w, int h, int* outX, int* outY, int* outW, int* outH) {
    Port_PPU_FitAspectRect(w, h, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT,
                           outX, outY, outW, outH);
}

// Compute both the "stage" rect (the visible area honoring the user's
// configured aspect mode — gets the chosen background fill) and the
// GBA "frame" rect that sits centered inside the stage. Outside the
// stage is always black. For PORT_ASPECT_NATIVE_3_2 the stage equals
// the GBA frame and there is no background fill region.
static void Port_PPU_ComputeViewportRects(int outW, int outH,
                                          int* stageX, int* stageY,
                                          int* stageW, int* stageH,
                                          int* frameX, int* frameY,
                                          int* frameW, int* frameH) {
    const int FW = MODE1_GBA_WIDTH;
    const int FH = MODE1_GBA_HEIGHT;

    int aspW = FW;
    int aspH = FH;
    const PortAspectMode mode = Port_Config_AspectMode();
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:      aspW = 16; aspH = 9; break;
        case PORT_ASPECT_ULTRAWIDE_21_9:       aspW = 21; aspH = 9; break;
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9: aspW = 32; aspH = 9; break;
        case PORT_ASPECT_NATIVE_3_2:
        default:                                aspW = FW; aspH = FH; break;
    }
    Port_PPU_FitAspectRect(outW, outH, aspW, aspH,
                           stageX, stageY, stageW, stageH);
    // Inside the stage, fit the GBA frame at its native 3:2.
    int fx, fy, fw, fh;
    Port_PPU_FitAspectRect(*stageW, *stageH, FW, FH, &fx, &fy, &fw, &fh);
    *frameX = *stageX + fx;
    *frameY = *stageY + fy;
    *frameW = fw;
    *frameH = fh;
}

static void Port_PPU_QueryOutputSize(int* outW, int* outH) {
    int w = 0;
    int h = 0;
    if (sRenderer) {
        SDL_GetCurrentRenderOutputSize(sRenderer, &w, &h);
    }
    if (w > 0 && h > 0) {
        *outW = w;
        *outH = h;
        return;
    }
    if (sWindow) {
        SDL_GetWindowSize(sWindow, &w, &h);
    }
    if (w > 0 && h > 0) {
        *outW = w;
        *outH = h;
        return;
    }
    *outW = 960;
    *outH = 540;
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
    const int FW = MODE1_GBA_WIDTH;
    const int FH = MODE1_GBA_HEIGHT;
    const int w = FW * S;
    const int h = FH * S;
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
    for (int sy = 0; sy < FH; ++sy) {
        const uint32_t* src = &virtuappu_frame_buffer[sy * FW];
        for (int dy = 0; dy < S; ++dy) {
            uint32_t* dst = &sScaledBuf[(sy * S + dy) * w];
            for (int sx = 0; sx < FW; ++sx) {
                uint32_t c = src[sx];
                uint32_t* d = &dst[sx * S];
                for (int dx = 0; dx < S; ++dx) {
                    d[dx] = c;
                }
            }
        }
    }

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
                                       SDL_TEXTUREACCESS_STREAMING,
                                       MODE1_GBA_WIDTH * S, MODE1_GBA_HEIGHT * S);
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

extern "C" bool Port_PPU_VSyncEnabled(void) {
    return sVSyncEnabled;
}

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;
#ifdef launcher
    sBootstrapWindow = nullptr;
#endif
    Port_PPU_LoadConfig();

    /* Stage 2: if the GPU renderer is compiled in and the device
     * initialised successfully (Port_GPU_Init was called before us in
     * port_main.c), tear down any pre-existing SDL_Renderer on this
     * window and hand it to the SDL_GPU pipeline. Only one device can
     * own a window's swapchain, so the SDL_Renderer fallback must give
     * up the window first. On any GPU-side failure we keep the
     * SDL_Renderer path — no behavioural change vs. pre-Stage-2 builds.
     *
     * Bootstrap-renderer reuse note: bootstrap may have created an
     * SDL_Renderer for the LOADING splash. Destroying it before the
     * GPU claim is the safe order — without that, SDL_ClaimWindow
     * returns VK_ERROR_SURFACE_LOST_KHR. If GPU init fails the
     * fallback path re-creates the renderer below. */
    extern bool Port_GPU_Init(SDL_Window*);
    extern bool Port_GPU_ClaimWindow(SDL_Window*, int, int);
    extern bool Port_GPU_IsActive(void);
    (void)Port_GPU_Init;  /* called earlier in port_main.c */

    if (SDL_Renderer* existing = SDL_GetRenderer(window)) {
        /* Try GPU first; if it claims, we drop the renderer. Otherwise
         * fall through to the normal SDL_Renderer reuse path. */
        SDL_DestroyRenderer(existing);
    }
    if (Port_GPU_ClaimWindow(window, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT)) {
        sBackend = RenderBackend::Gpu;
        std::fprintf(stderr, "PPU initialized with SDL_GPU backend.\n");
        /* GPU path doesn't need any of the SDL_Renderer / SDL_Texture
         * state below — skip straight to the ViruaPPU memory bind. */
        goto bind_virtuappu_memory;
    }

    /* GPU not available (build flag off, init failed, or claim failed):
     * fall back to SDL_Renderer. SDL_GetRenderer is NULL here because
     * we destroyed any existing one above; create one fresh. */
    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (sRenderer) {
        SDL_SetRenderTarget(sRenderer, nullptr);
        SDL_SetRenderClipRect(sRenderer, nullptr);
    }
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    } else {
        if (!SDL_SetRenderVSync(sRenderer, 1)) {
            printf("Port_PPU_Init: SDL_SetRenderVSync failed: %s\n", SDL_GetError());
        }
        sLowResTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT);
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

bind_virtuappu_memory:
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

    virtuappu_mode1_pre_line_callback = nullptr;

    virtuappu_registers.frame_width = MODE1_GBA_WIDTH;
    virtuappu_registers.mode = 1;

    if (sBackend == RenderBackend::None) {
        sFrameSurface = SDL_CreateSurfaceFrom(
            MODE1_GBA_WIDTH,
            MODE1_GBA_HEIGHT,
            SDL_PIXELFORMAT_ABGR8888,
            virtuappu_frame_buffer,
            MODE1_GBA_WIDTH * static_cast<int>(sizeof(uint32_t)));
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
    } else if (sBackend == RenderBackend::Renderer) {
        printf("PPU initialized with SDL renderer backend.\n");
    }
    /* GPU backend already logged "PPU initialized with SDL_GPU backend"
     * inside the early Port_GPU_ClaimWindow branch above. */

    /* Hand the renderer to the ImGui menu layer so it can draw on top
     * of the rasterized GBA frame. Failure to init ImGui is non-fatal —
     * the legacy SDL-text menu still works.
     *
     * Stage 2 GPU build: Port_ImGui_Init detects sBackend internally
     * and uses the SDL_GPU ImGui backend (ImGui_ImplSDLGPU3_*) when the
     * GPU pipeline owns the window. Renderer can be NULL in that case. */
    if (sBackend == RenderBackend::Renderer) {
        extern void Port_ImGui_Init(SDL_Window*, SDL_Renderer*);
        Port_ImGui_Init(window, sRenderer);
    } else if (sBackend == RenderBackend::Gpu) {
        extern void Port_ImGui_Init(SDL_Window*, SDL_Renderer*);
        Port_ImGui_Init(window, nullptr);
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

    /* HBlank-DMA simulation: only enable the scanline callback while a
     * channel is active. Affine BG rendering treats BG2X/BG2Y differently
     * when HDMA has already supplied per-line reference points. */
    virtuappu_mode1_pre_line_callback =
        port_hdma_has_active_channels() ? port_hdma_step_line : nullptr;

    virtuappu_render_frame();

    /* Widescreen-spike post-process: on screens where the engine doesn't
     * load BG tile data past column 239 (title, file-select), the extra
     * widescreen columns (240+) read stale VRAM and visually glitch
     * (e.g. yellow band on title). Force-black them on those tasks; the
     * gameplay task is left alone since it does scroll the BG buffer.
     *
     * gMain.task is byte 2 of the Main struct (vu8 interruptFlag at 0,
     * sleepStatus at 1, task at 2 — see include/main.h). Read raw bytes
     * to avoid pulling main.h into this C++ TU (the engine headers use
     * `this` as a C parameter name and don't parse as C++). */
    /* Widescreen Phase 1: ViruaPPU clips BG/OAM at col 240 unconditionally
     * (engine's 32-tile BG buffer doesn't have reliable data past col 240
     * on static screens, and parked off-screen sprites live at x >= 240).
     * For any widescreen_width > 240, uniform-stretch the 240-px frame
     * into the full window. Phase 2 (sa2-style BGCNT_TXT512x256 + 64-tile
     * BG buffer) replaces this with real extended tile loading. */
    /* Post-render stretch path: only kicks in when the framebuffer is
     * wider than 256 (Phase 2+). For 241..256, the BG renderer is
     * already filling cols 240..255 from the engine's scroll buffer
     * (MODE1_GBA_BG_CLIP_X follows MODE1_GBA_WIDTH up to 256), so
     * stretching would destroy that real-tile content. */
    if (MODE1_GBA_WIDTH > 256) {
        uint32_t scratch[256];
        for (int y = 0; y < MODE1_GBA_HEIGHT; ++y) {
            uint32_t* row = &virtuappu_frame_buffer[y * MODE1_GBA_WIDTH];
            std::memcpy(scratch, row, 256 * sizeof(uint32_t));
            for (int dst_x = 0; dst_x < MODE1_GBA_WIDTH; ++dst_x) {
                int src_x = (dst_x * 256) / MODE1_GBA_WIDTH;
                if (src_x > 255) src_x = 255;
                row[dst_x] = scratch[src_x];
            }
        }
    }
    (void)gMain;

    /* Stage 2: SDL_GPU present path. Stretched 240x160 → swapchain via
     * the passthrough shader. No aspect-mode / internal-scale / xBRZ /
     * filter integration yet — those features stay on the SDL_Renderer
     * path; Stage 3 will rebuild them on the GPU side as shader passes.
     *
     * ImGui must run BEFORE Port_GPU_PresentFrame so its NewFrame + UI
     * build + ImGui::Render() collect draw data that PresentFrame can
     * then upload (PrepareDrawData) and overlay inside the same render
     * pass. The SDL_Renderer branch below calls Port_ImGui_Render at
     * the end (after the game's RenderPresent), which works because
     * the renderer presents-then-overlays in one call; SDL_GPU needs
     * the order swapped because the menu draws into the game's pass. */
    if (sBackend == RenderBackend::Gpu) {
        extern bool Port_ImGui_Render(void);
        Port_ImGui_Render();
        extern bool Port_GPU_PresentFrame(const uint32_t*, int, int);
        Port_GPU_PresentFrame(virtuappu_frame_buffer, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT);
        return;
    }

    if (sBackend == RenderBackend::Renderer) {
        int outW = 0;
        int outH = 0;
        Port_PPU_QueryOutputSize(&outW, &outH);
        Port_TouchControls_NotifyRenderSize(outW, outH);
        int sx, sy, sw_stage, sh_stage;
        int x, y, w, h;
        Port_PPU_ComputeViewportRects(outW, outH,
                                      &sx, &sy, &sw_stage, &sh_stage,
                                      &x,  &y,  &w,        &h);
        SDL_FRect stage = { (float)sx, (float)sy, (float)sw_stage, (float)sh_stage };
        SDL_FRect dst   = { (float)x,  (float)y,  (float)w,        (float)h        };

        SDL_Texture* tex;
        SDL_ScaleMode scale;
        const int internalS = (int)Port_Config_InternalScale();
        switch (sPresentMode) {
            case PresentMode::XbrzLinear:
            case PresentMode::XbrzNearest:
                /* xBRZ owns its own 4x upscaler — internal-render-scale
                 * is mutually exclusive with it. The xBRZ path always
                 * consumes the unscaled GBA-native framebuffer. */
                Port_Upscale_xBRZ_4x(virtuappu_frame_buffer,
                                     MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT,
                                     sUpscale2xBuf, sUpscale4xBuf);
                /* CRT/LCD filter at the upscaled resolution (4x). The
                 * pattern needs >= 3 px per phosphor cell to read
                 * correctly, so xBRZ's 4x output is always large enough. */
                Port_Filter_Apply(sUpscale4xBuf, kHiResW, kHiResH, 4, sFilter);
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
                /* Filter needs a scaled buffer to operate on (1x has too
                 * few pixels per phosphor cell). Force at least 4x when
                 * a filter is active, otherwise honour the user's
                 * internal-scale setting. */
                int effScale = internalS;
                if (sFilter != PORT_FILTER_NONE && effScale < 4) {
                    effScale = 4;
                }
                uint32_t* scaled = Port_PPU_BuildScaledFrame(effScale, &sw, &sh);
                SDL_Texture* scaledTex = Port_PPU_EnsureScaledTexture(effScale);
                if (scaled && scaledTex) {
                    Port_Filter_Apply(scaled, sw, sh, effScale, sFilter);
                    SDL_UpdateTexture(scaledTex, nullptr, scaled, sw * (int)sizeof(uint32_t));
                    tex = scaledTex;
                } else {
                    SDL_UpdateTexture(sLowResTexture, nullptr, virtuappu_frame_buffer,
                                      MODE1_GBA_WIDTH * (int)sizeof(uint32_t));
                    tex = sLowResTexture;
                }
                scale = (sPresentMode == PresentMode::LinearRaw)
                            ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            }
        }
        /* Clear-to-black covers anything outside the stage rect (the
         * area honoring the user's aspect-ratio choice). Inside the
         * stage, the chosen background-fill style applies; on top of
         * that, the sharp GBA frame is composited in `dst`. */
        SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sRenderer);

        const PortBgFill bgFill = Port_Config_BgFill();
        if (bgFill == PORT_BG_FILL_SOLID_COLOR) {
            u8 bgR = 0, bgG = 0, bgB = 0;
            Port_Config_BgFillColor(&bgR, &bgG, &bgB);
            SDL_SetRenderDrawColor(sRenderer, bgR, bgG, bgB, 255);
            SDL_RenderFillRect(sRenderer, &stage);
        } else if (bgFill == PORT_BG_FILL_BLURRED_FRAME) {
            /* Stretch the same texture across the whole stage with
             * linear filtering for a soft "ambient mode" halo, then
             * the sharp letterboxed copy paints over the center. */
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(sRenderer, tex, nullptr, &stage);
        }

        SDL_SetTextureScaleMode(tex, scale);
        SDL_RenderTexture(sRenderer, tex, nullptr, &dst);
        {
            /* Try the ImGui-based menu first; if disabled (or init
             * failed), fall back to the legacy SDL_RenderDebugText
             * overlay so the menu still works. The soft-slot overlay
             * is a separate HUD layer and always uses SDL primitives. */
            extern bool Port_ImGui_Render(void);
            if (!Port_ImGui_Render()) {
                extern void Port_DebugMenu_Render(SDL_Renderer*, int, int);
                Port_DebugMenu_Render(sRenderer, outW, outH);
            }
            extern void Port_SoftSlots_RenderOverlay(void*, int, int);
            Port_SoftSlots_RenderOverlay(sRenderer, outW, outH);
            Port_TouchControls_Render(sRenderer, outW, outH);
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
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (!w) {
        return;
    }
    SDL_WindowFlags flags = SDL_GetWindowFlags(w);
    bool wantFullscreen = (flags & SDL_WINDOW_FULLSCREEN) == 0;
    SDL_SetWindowFullscreen(w, wantFullscreen);
    SDL_SyncWindow(w);
    /* Hide the cursor while fullscreen, show it again in windowed mode.
     * The bare-mouse cursor floating over Hyrule Town gets distracting
     * fast — flagged by nayyar in the suggestions thread. */
    if (wantFullscreen) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

extern "C" bool Port_PPU_IsFullscreen(void) {
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (!w) {
        return false;
    }
    return (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
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
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (w && !Port_PPU_IsFullscreen()) {
        SDL_SetWindowSize(w, MODE1_GBA_WIDTH * scale, MODE1_GBA_HEIGHT * scale);
        SDL_SyncWindow(w);
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

extern "C" void Port_PPU_CycleFilter(int direction) {
    /* On the SDL_GPU backend, the F8 → Filter button drives the GLSL
     * pipeline switcher in port_gpu_renderer.cpp instead of the
     * CPU-side filter (which is bypassed on the GPU path entirely).
     * Stage 3 currently exposes 2 GPU filters (None + LCD Grid);
     * Stages 4+ port the rest of port_filter.c's CRT / scanlines /
     * vignette presets as GLSL shaders. */
    if (sBackend == RenderBackend::Gpu) {
        extern PortGpuFilter Port_GPU_GetFilter(void);
        extern void Port_GPU_SetFilter(PortGpuFilter);
        extern const char* Port_GPU_FilterName(PortGpuFilter);
        int next = (int)Port_GPU_GetFilter() + (direction < 0 ? -1 : 1);
        if (next < 0) next = (int)PORT_GPU_FILTER_COUNT - 1;
        else if (next >= (int)PORT_GPU_FILTER_COUNT) next = 0;
        Port_GPU_SetFilter((PortGpuFilter)next);
        fprintf(stderr, "GPU filter: %s\n", Port_GPU_FilterName((PortGpuFilter)next));
        return;
    }

    int next = (int)sFilter + (direction < 0 ? -1 : 1);
    if (next < 0) {
        next = (int)PORT_FILTER_COUNT - 1;
    } else if (next >= (int)PORT_FILTER_COUNT) {
        next = 0;
    }
    sFilter = (PortFilterType)next;
    fprintf(stderr, "PPU filter: %s\n", Port_Filter_Name(sFilter));
}

extern "C" const char* Port_PPU_FilterName(void) {
    if (sBackend == RenderBackend::Gpu) {
        extern PortGpuFilter Port_GPU_GetFilter(void);
        extern const char* Port_GPU_FilterName(PortGpuFilter);
        return Port_GPU_FilterName(Port_GPU_GetFilter());
    }
    return Port_Filter_Name(sFilter);
}

extern "C" bool Port_InGameSettingsModalIsOpen(void) {
#ifdef launcher
    return TmcSettings_IsModalOpen();
#else
    return false;
#endif
}

extern "C" void Port_OpenInGameSettingsModal(void) {
#ifdef launcher
    if (TmcSettings_IsModalOpen()) {
        return;
    }
    SDL_Window* w = sWindow ? sWindow : sBootstrapWindow;
    if (!w) {
        return;
    }
    SDL_Renderer* r = sRenderer;
    if (!r) {
        r = SDL_GetRenderer(w);
    }
    if (!r) {
        return;
    }
    if (!TmcSettings_RunModalInGame(w, r)) {
        SDL_Event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
#else
    /* No launcher: settings UI is not linked. */
#endif
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
