/*
 * port_gpu_renderer.cpp — SDL_GPU presentation path (Stage 1 scaffold).
 *
 * See port_gpu_renderer.h for the staged plan. This file proves the
 * SDL_GPU build wiring works end-to-end: device creation succeeds on
 * the host's preferred backend, the embedded SPIR-V blobs load as
 * shader modules, and shutdown unwinds cleanly. Stage 2 hooks the
 * device into port_ppu.cpp's present path.
 *
 * The two SPIR-V blobs are pre-compiled by port/shaders/build.sh and
 * committed under port/shaders/build/. xmake (utils.bin2c rule)
 * generates the corresponding _spv_data / _spv_size symbols when
 * `--gpu_renderer=y` is set.
 */

#include "port_gpu_renderer.h"

#ifndef TMC_GPU_RENDERER

extern "C" bool Port_GPU_Init(SDL_Window* window) {
    (void)window;
    return true; /* no-op stub when GPU path disabled at build time */
}
extern "C" bool Port_GPU_ClaimWindow(SDL_Window* w, int fw, int fh) {
    (void)w; (void)fw; (void)fh; return false;
}
extern "C" bool Port_GPU_PresentFrame(const uint32_t* fb, int w, int h) {
    (void)fb; (void)w; (void)h; return false;
}
extern "C" bool Port_GPU_IsActive(void) { return false; }
extern "C" void Port_GPU_Shutdown(void) {}
extern "C" void Port_GPU_SetFilter(PortGpuFilter f) { (void)f; }
extern "C" PortGpuFilter Port_GPU_GetFilter(void) { return PORT_GPU_FILTER_NONE; }
extern "C" const char* Port_GPU_FilterName(PortGpuFilter f) {
    (void)f; return "Off";
}

#else

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

/* Embed the SPIR-V blobs. xmake's utils.bin2c rule generates a header
 * with the raw bytes (no struct, no symbols) at
 * build/.gens/<target>/.../rules/utils/bin2c/<file>.h — the include path
 * is added automatically. We wrap it in a static array so the size is
 * sizeof()-derivable and the symbol stays local to this TU. */
static const unsigned char kPassthroughVertSpv[] = {
#include "passthrough.vert.spv.h"
};
static const unsigned char kPassthroughFragSpv[] = {
#include "passthrough.frag.spv.h"
};
static const unsigned char kLcdGridFragSpv[] = {
#include "lcd_grid.frag.spv.h"
};
static constexpr size_t kPassthroughVertSpvSize = sizeof(kPassthroughVertSpv);
static constexpr size_t kPassthroughFragSpvSize = sizeof(kPassthroughFragSpv);
static constexpr size_t kLcdGridFragSpvSize     = sizeof(kLcdGridFragSpv);

static SDL_GPUDevice*           sDevice    = nullptr;
static SDL_GPUShader*           sVertShader = nullptr;
/* Stage 3: one fragment shader + one graphics pipeline per filter mode. */
static SDL_GPUShader*           sFragShaders[PORT_GPU_FILTER_COUNT] = {};
static SDL_GPUGraphicsPipeline* sPipelines[PORT_GPU_FILTER_COUNT]   = {};
static SDL_Window*              sWindow    = nullptr;
static SDL_GPUSampler*          sSampler        = nullptr;
static SDL_GPUTexture*          sSourceTexture  = nullptr;
static SDL_GPUTransferBuffer*   sTransferBuffer = nullptr;
static int                      sSourceW = 0;
static int                      sSourceH = 0;
static bool                     sWindowClaimed = false;
static SDL_GPUTextureFormat     sSwapFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
static PortGpuFilter            sActiveFilter = PORT_GPU_FILTER_NONE;

/* Accessors for port_imgui_menu.cpp so it can wire its SDL_GPU backend
 * against the same device and matching swapchain format. */
extern "C" SDL_GPUDevice* Port_GPU_GetDevice(void)             { return sDevice; }
extern "C" SDL_GPUTextureFormat Port_GPU_GetSwapchainFormat(void) { return sSwapFormat; }

extern "C" bool Port_GPU_Init(SDL_Window* window) {
    if (sDevice) return true; /* idempotent */

    /* Request SPIR-V format support. The runtime picks the best
     * backend (Vulkan first; Metal/D3D12 if SPIR-V isn't native, with
     * SDL doing the cross-compile internally on those backends). */
    sDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, /*debug=*/false, /*name=*/nullptr);
    if (!sDevice) {
        std::fprintf(stderr, "[gpu] SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        return false;
    }
    const char* backend = SDL_GetGPUDeviceDriver(sDevice);
    std::fprintf(stderr, "[gpu] device created (backend=%s)\n", backend ? backend : "?");

    /* Stage 1: don't ClaimWindow yet. SDL_Renderer already owns the
     * window (via its own Vulkan instance on Linux), and only one
     * device may swapchain a window at a time — claim would fail with
     * VK_ERROR_SURFACE_LOST_KHR. Stage 2 replaces the SDL_Renderer
     * present path with SDL_GPU and moves ClaimWindow + swapchain
     * acquire here.
     *
     * `sWindow` is recorded so Shutdown's symmetric release works
     * once Stage 2 wires it up; for now it's just the future
     * insertion point. */
    sWindow = window;

    /* Vertex shader. No vertex buffer (fullscreen quad synthesised
     * from gl_VertexIndex), no per-vertex sampler/uniform inputs. */
    SDL_GPUShaderCreateInfo vci = {};
    vci.code_size   = kPassthroughVertSpvSize;
    vci.code        = kPassthroughVertSpv;
    vci.entrypoint  = "main";
    vci.format      = SDL_GPU_SHADERFORMAT_SPIRV;
    vci.stage       = SDL_GPU_SHADERSTAGE_VERTEX;
    sVertShader = SDL_CreateGPUShader(sDevice, &vci);
    if (!sVertShader) {
        std::fprintf(stderr, "[gpu] vertex shader load failed: %s\n", SDL_GetError());
        Port_GPU_Shutdown();
        return false;
    }

    /* Fragment shaders — one per filter slot. The passthrough shader
     * uses 1 combined sampler at set=2/binding=0. The lcd_grid shader
     * also takes a vec2 uniform at set=3/binding=0 (viewport size,
     * pushed each frame via SDL_PushGPUFragmentUniformData). */
    struct {
        const unsigned char* code;
        size_t               size;
        unsigned             num_uniforms;
        const char*          label;
    } fragSpec[PORT_GPU_FILTER_COUNT] = {
        { kPassthroughFragSpv, kPassthroughFragSpvSize, 0, "passthrough" },
        { kLcdGridFragSpv,     kLcdGridFragSpvSize,     1, "lcd_grid"    },
    };
    for (int i = 0; i < PORT_GPU_FILTER_COUNT; ++i) {
        SDL_GPUShaderCreateInfo fci = {};
        fci.code_size            = fragSpec[i].size;
        fci.code                 = fragSpec[i].code;
        fci.entrypoint           = "main";
        fci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
        fci.stage                = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fci.num_samplers         = 1;
        fci.num_uniform_buffers  = fragSpec[i].num_uniforms;
        sFragShaders[i] = SDL_CreateGPUShader(sDevice, &fci);
        if (!sFragShaders[i]) {
            std::fprintf(stderr, "[gpu] fragment shader '%s' load failed: %s\n",
                         fragSpec[i].label, SDL_GetError());
            Port_GPU_Shutdown();
            return false;
        }
    }

    std::fprintf(stderr,
        "[gpu] shaders loaded (vert %zu B, frag passthrough %zu B, frag lcd_grid %zu B)\n",
        kPassthroughVertSpvSize, kPassthroughFragSpvSize, kLcdGridFragSpvSize);
    return true;
}

extern "C" bool Port_GPU_ClaimWindow(SDL_Window* window, int fb_width, int fb_height) {
    if (!sDevice || !window) return false;
    if (sWindowClaimed) return true;  /* idempotent */

    if (!SDL_ClaimWindowForGPUDevice(sDevice, window)) {
        std::fprintf(stderr, "[gpu] ClaimWindow failed: %s\n", SDL_GetError());
        return false;
    }
    sWindow = window;
    sWindowClaimed = true;

    /* Query the swapchain texture format so the pipeline's color target
     * description matches. Cache it for the ImGui SDL_GPU backend (see
     * Port_GPU_GetSwapchainFormat). SDL_GPU's default present mode is VSYNC. */
    SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(sDevice, window);
    sSwapFormat = swap_fmt;

    /* Source texture: 240x160 RGBA8888. ViruaPPU's `virtuappu_frame_buffer`
     * is ABGR8888 little-endian (byte 0=R, 1=G, 2=B, 3=A), matching
     * SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM byte order. */
    {
        SDL_GPUTextureCreateInfo tci = {};
        tci.type   = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width  = (Uint32)fb_width;
        tci.height = (Uint32)fb_height;
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        sSourceTexture = SDL_CreateGPUTexture(sDevice, &tci);
        if (!sSourceTexture) {
            std::fprintf(stderr, "[gpu] source texture create failed: %s\n", SDL_GetError());
            Port_GPU_Shutdown();
            return false;
        }
        sSourceW = fb_width;
        sSourceH = fb_height;
    }

    /* Linear sampler — paired with the GPU upscale, gives a soft blit
     * by default. Future stages will let the user pick nearest vs
     * linear (and per-shader-pass overrides) via the F8 cycle. */
    {
        SDL_GPUSamplerCreateInfo sci = {};
        sci.min_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sSampler = SDL_CreateGPUSampler(sDevice, &sci);
        if (!sSampler) {
            std::fprintf(stderr, "[gpu] sampler create failed: %s\n", SDL_GetError());
            Port_GPU_Shutdown();
            return false;
        }
    }

    /* Transfer buffer for per-frame uploads. fb_width * fb_height * 4 bytes
     * is at most ~150 KB at 240x160 — well within any backend's per-frame
     * upload throughput. Cycled per-frame via the MapGPUTransferBuffer
     * `cycle=true` flag inside PresentFrame. */
    {
        SDL_GPUTransferBufferCreateInfo tbci = {};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = (Uint32)(fb_width * fb_height * (int)sizeof(uint32_t));
        sTransferBuffer = SDL_CreateGPUTransferBuffer(sDevice, &tbci);
        if (!sTransferBuffer) {
            std::fprintf(stderr, "[gpu] transfer buffer create failed: %s\n", SDL_GetError());
            Port_GPU_Shutdown();
            return false;
        }
    }

    /* Graphics pipelines — one per filter slot. Each pairs the shared
     * vertex shader with its filter's fragment shader; the rest of the
     * pipeline state is identical (fullscreen quad, triangle strip,
     * REPLACE blend). F8 picks which pipeline binds at draw time. */
    {
        SDL_GPUColorTargetDescription color_target_desc = {};
        color_target_desc.format = swap_fmt;
        color_target_desc.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineTargetInfo target_info = {};
        target_info.num_color_targets = 1;
        target_info.color_target_descriptions = &color_target_desc;

        for (int i = 0; i < PORT_GPU_FILTER_COUNT; ++i) {
            SDL_GPUGraphicsPipelineCreateInfo gpci = {};
            gpci.vertex_shader            = sVertShader;
            gpci.fragment_shader          = sFragShaders[i];
            gpci.primitive_type           = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            gpci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
            gpci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
            gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            gpci.target_info = target_info;
            sPipelines[i] = SDL_CreateGPUGraphicsPipeline(sDevice, &gpci);
            if (!sPipelines[i]) {
                std::fprintf(stderr, "[gpu] pipeline %d create failed: %s\n", i, SDL_GetError());
                Port_GPU_Shutdown();
                return false;
            }
        }
    }

    /* TMC_GPU_FILTER env var lets us flip the active filter at startup
     * without rebuilding. Recognised values: "off" (default),
     * "lcd_grid". Later stages add more presets. */
    if (const char* f = std::getenv("TMC_GPU_FILTER")) {
        if (std::strcmp(f, "lcd_grid") == 0) {
            sActiveFilter = PORT_GPU_FILTER_LCD_GRID;
            std::fprintf(stderr, "[gpu] filter at startup: %s\n", Port_GPU_FilterName(sActiveFilter));
        }
    }

    std::fprintf(stderr, "[gpu] pipeline ready — %dx%d source, swapchain fmt=%u\n",
                 sSourceW, sSourceH, (unsigned)swap_fmt);
    return true;
}

extern "C" bool Port_GPU_PresentFrame(const uint32_t* fb, int fb_w, int fb_h) {
    if (!sWindowClaimed || !sPipelines[sActiveFilter]) return false;
    if (fb_w != sSourceW || fb_h != sSourceH) return false;  /* size change unsupported */

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(sDevice);
    if (!cmd) return false;

    /* Acquire the swapchain target for this frame. Use the Wait variant
     * so we don't busy-loop when the queue is full — the deadline-based
     * pacer in port_bios.c::VBlankIntrWait still owns frame cadence. */
    SDL_GPUTexture* swap_tex = nullptr;
    Uint32 swap_w = 0, swap_h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, sWindow, &swap_tex, &swap_w, &swap_h) || !swap_tex) {
        /* Swapchain not ready (window minimised, resizing). Cancel the
         * command buffer and skip this present — the GBA continues, the
         * frame just doesn't render. */
        SDL_SubmitGPUCommandBuffer(cmd);  /* a no-op submit cleans up the cmd */
        return false;
    }

    /* Upload the framebuffer via transfer buffer. Map → memcpy → unmap. */
    void* mapped = SDL_MapGPUTransferBuffer(sDevice, sTransferBuffer, /*cycle=*/true);
    if (mapped) {
        std::memcpy(mapped, fb, (size_t)fb_w * (size_t)fb_h * sizeof(uint32_t));
        SDL_UnmapGPUTransferBuffer(sDevice, sTransferBuffer);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = sTransferBuffer;
        src.offset          = 0;
        src.pixels_per_row  = (Uint32)fb_w;
        src.rows_per_layer  = (Uint32)fb_h;
        SDL_GPUTextureRegion dst = {};
        dst.texture = sSourceTexture;
        dst.w = (Uint32)fb_w;
        dst.h = (Uint32)fb_h;
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, /*cycle=*/false);
        SDL_EndGPUCopyPass(copy);
    }

    /* ImGui draw-data preparation must run BEFORE BeginGPURenderPass —
     * the SDL_GPU ImGui backend issues its own copy passes for vertex
     * and index buffer uploads, which can't nest inside a render pass.
     * Safe to call when ImGui isn't initialised; the stub returns
     * immediately. */
    extern void Port_ImGui_PrepareDrawDataGpu(SDL_GPUCommandBuffer*);
    Port_ImGui_PrepareDrawDataGpu(cmd);

    /* Render pass: clear full swapchain to black, then constrain the
     * viewport to a centered fit-rect that preserves the GBA's source
     * aspect ratio (MODE1_GBA_WIDTH:MODE1_GBA_HEIGHT). The clear fills
     * the letterbox / pillarbox bars; the game quad draws inside the
     * viewport. ImGui RenderDrawData runs with the viewport reset to
     * the full swapchain so the menu can use the entire window. */
    SDL_GPUColorTargetInfo color = {};
    color.texture     = swap_tex;
    color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    color.load_op     = SDL_GPU_LOADOP_CLEAR;
    color.store_op    = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);

    /* Aspect-preserving viewport — same math as
     * port_ppu.cpp::Port_PPU_FitAspectRect for the SDL_Renderer path.
     * Source aspect uses the runtime fb size (which equals
     * MODE1_GBA_WIDTH × MODE1_GBA_HEIGHT for now; future internal-scale
     * or widescreen modes feed a bigger source through here too). */
    {
        const int aspW = fb_w;
        const int aspH = fb_h;
        const int w = (int)swap_w;
        const int h = (int)swap_h;
        int rw, rh;
        if (w * aspH >= h * aspW) {
            rh = h;
            rw = (h * aspW) / aspH;
        } else {
            rw = w;
            rh = (w * aspH) / aspW;
        }
        SDL_GPUViewport vp = {};
        vp.x = (float)((w - rw) / 2);
        vp.y = (float)((h - rh) / 2);
        vp.w = (float)rw;
        vp.h = (float)rh;
        vp.min_depth = 0.0f;
        vp.max_depth = 1.0f;
        SDL_SetGPUViewport(rp, &vp);
    }

    SDL_BindGPUGraphicsPipeline(rp, sPipelines[sActiveFilter]);
    SDL_GPUTextureSamplerBinding tsb = {};
    tsb.texture = sSourceTexture;
    tsb.sampler = sSampler;
    SDL_BindGPUFragmentSamplers(rp, /*first_slot=*/0, &tsb, 1);

    /* Push fragment uniforms required by the active filter. The LCD
     * grid shader expects the output viewport size so it can compute
     * cell stride; passthrough has no uniforms. */
    if (sActiveFilter == PORT_GPU_FILTER_LCD_GRID) {
        struct { float viewport[2]; float _pad[2]; } u;
        /* Pass the fit-rect we computed earlier so the cells align with
         * the actual draw area, not the full swapchain. The 16-byte
         * std140 alignment requires padding. */
        const int aspW = fb_w;
        const int aspH = fb_h;
        const int w = (int)swap_w;
        const int h = (int)swap_h;
        int rw, rh;
        if (w * aspH >= h * aspW) {
            rh = h; rw = (h * aspW) / aspH;
        } else {
            rw = w; rh = (w * aspH) / aspW;
        }
        u.viewport[0] = (float)rw;
        u.viewport[1] = (float)rh;
        u._pad[0] = u._pad[1] = 0.0f;
        SDL_PushGPUFragmentUniformData(cmd, /*slot_index=*/0, &u, sizeof(u));
    }

    SDL_DrawGPUPrimitives(rp, /*num_vertices=*/4, /*num_instances=*/1, 0, 0);

    /* Reset the viewport to the full swapchain before drawing ImGui,
     * otherwise the F8 menu draws clipped inside the letterbox. */
    {
        SDL_GPUViewport vp = {};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.w = (float)swap_w;
        vp.h = (float)swap_h;
        vp.min_depth = 0.0f;
        vp.max_depth = 1.0f;
        SDL_SetGPUViewport(rp, &vp);
    }

    extern void Port_ImGui_RenderDrawDataGpu(SDL_GPUCommandBuffer*, SDL_GPURenderPass*);
    Port_ImGui_RenderDrawDataGpu(cmd, rp);

    SDL_EndGPURenderPass(rp);

    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

extern "C" bool Port_GPU_IsActive(void) {
    return sWindowClaimed && sPipelines[PORT_GPU_FILTER_NONE] != nullptr;
}

extern "C" void Port_GPU_SetFilter(PortGpuFilter f) {
    if ((unsigned)f < PORT_GPU_FILTER_COUNT) sActiveFilter = f;
}
extern "C" PortGpuFilter Port_GPU_GetFilter(void) { return sActiveFilter; }
extern "C" const char* Port_GPU_FilterName(PortGpuFilter f) {
    switch (f) {
        case PORT_GPU_FILTER_NONE:     return "Off";
        case PORT_GPU_FILTER_LCD_GRID: return "LCD Grid (GPU)";
        default: return "?";
    }
}

extern "C" void Port_GPU_Shutdown(void) {
    for (int i = 0; i < PORT_GPU_FILTER_COUNT; ++i) {
        if (sPipelines[i]) {
            SDL_ReleaseGPUGraphicsPipeline(sDevice, sPipelines[i]);
            sPipelines[i] = nullptr;
        }
    }
    if (sTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(sDevice, sTransferBuffer);
        sTransferBuffer = nullptr;
    }
    if (sSourceTexture) {
        SDL_ReleaseGPUTexture(sDevice, sSourceTexture);
        sSourceTexture = nullptr;
    }
    if (sSampler) {
        SDL_ReleaseGPUSampler(sDevice, sSampler);
        sSampler = nullptr;
    }
    for (int i = 0; i < PORT_GPU_FILTER_COUNT; ++i) {
        if (sFragShaders[i]) {
            SDL_ReleaseGPUShader(sDevice, sFragShaders[i]);
            sFragShaders[i] = nullptr;
        }
    }
    if (sVertShader) {
        SDL_ReleaseGPUShader(sDevice, sVertShader);
        sVertShader = nullptr;
    }
    if (sDevice) {
        if (sWindowClaimed && sWindow) {
            SDL_ReleaseWindowFromGPUDevice(sDevice, sWindow);
        }
        SDL_DestroyGPUDevice(sDevice);
        sDevice = nullptr;
        sWindow = nullptr;
        sWindowClaimed = false;
    }
}

#endif /* TMC_GPU_RENDERER */
