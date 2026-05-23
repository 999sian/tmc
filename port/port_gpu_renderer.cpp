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

#else

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <cstdint>
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
static constexpr size_t kPassthroughVertSpvSize = sizeof(kPassthroughVertSpv);
static constexpr size_t kPassthroughFragSpvSize = sizeof(kPassthroughFragSpv);

static SDL_GPUDevice*           sDevice    = nullptr;
static SDL_GPUShader*           sVertShader = nullptr;
static SDL_GPUShader*           sFragShader = nullptr;
static SDL_Window*              sWindow    = nullptr;
/* Stage 2: claimed-window state. Set when Port_GPU_ClaimWindow succeeds. */
static SDL_GPUGraphicsPipeline* sPipeline       = nullptr;
static SDL_GPUSampler*          sSampler        = nullptr;
static SDL_GPUTexture*          sSourceTexture  = nullptr;
static SDL_GPUTransferBuffer*   sTransferBuffer = nullptr;
static int                      sSourceW = 0;
static int                      sSourceH = 0;
static bool                     sWindowClaimed = false;

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

    /* Fragment shader. One combined sampler at set=2, binding=0
     * (the SDL_GPU convention for "fragment samplers"). */
    SDL_GPUShaderCreateInfo fci = {};
    fci.code_size            = kPassthroughFragSpvSize;
    fci.code                 = kPassthroughFragSpv;
    fci.entrypoint           = "main";
    fci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    fci.stage                = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fci.num_samplers         = 1;
    sFragShader = SDL_CreateGPUShader(sDevice, &fci);
    if (!sFragShader) {
        std::fprintf(stderr, "[gpu] fragment shader load failed: %s\n", SDL_GetError());
        Port_GPU_Shutdown();
        return false;
    }

    std::fprintf(stderr,
        "[gpu] passthrough shader loaded (vert %zu B, frag %zu B)\n",
        kPassthroughVertSpvSize, kPassthroughFragSpvSize);
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
     * description matches. SDL_GPU's default present mode is VSYNC. */
    SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(sDevice, window);

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

    /* Graphics pipeline. No vertex buffer (fullscreen quad synthesised
     * from gl_VertexIndex in the vertex shader), triangle-strip topology,
     * single combined-image-sampler at fragment set=2/binding=0, simple
     * REPLACE blend (the source covers the entire viewport every frame). */
    {
        SDL_GPUColorTargetDescription color_target_desc = {};
        color_target_desc.format = swap_fmt;
        color_target_desc.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineTargetInfo target_info = {};
        target_info.num_color_targets = 1;
        target_info.color_target_descriptions = &color_target_desc;

        SDL_GPUGraphicsPipelineCreateInfo gpci = {};
        gpci.vertex_shader            = sVertShader;
        gpci.fragment_shader          = sFragShader;
        gpci.primitive_type           = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        gpci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
        gpci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
        gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        gpci.target_info = target_info;
        /* No vertex_input_state — vertices are synthesised in the
         * vertex shader from gl_VertexIndex; no per-vertex inputs. */

        sPipeline = SDL_CreateGPUGraphicsPipeline(sDevice, &gpci);
        if (!sPipeline) {
            std::fprintf(stderr, "[gpu] graphics pipeline create failed: %s\n", SDL_GetError());
            Port_GPU_Shutdown();
            return false;
        }
    }

    std::fprintf(stderr, "[gpu] pipeline ready — %dx%d source, swapchain fmt=%u\n",
                 sSourceW, sSourceH, (unsigned)swap_fmt);
    return true;
}

extern "C" bool Port_GPU_PresentFrame(const uint32_t* fb, int fb_w, int fb_h) {
    if (!sWindowClaimed || !sPipeline) return false;
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

    /* Render pass: clear to black, draw the fullscreen quad through the
     * passthrough shader, end. */
    SDL_GPUColorTargetInfo color = {};
    color.texture     = swap_tex;
    color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    color.load_op     = SDL_GPU_LOADOP_CLEAR;
    color.store_op    = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(rp, sPipeline);
    SDL_GPUTextureSamplerBinding tsb = {};
    tsb.texture = sSourceTexture;
    tsb.sampler = sSampler;
    SDL_BindGPUFragmentSamplers(rp, /*first_slot=*/0, &tsb, 1);
    SDL_DrawGPUPrimitives(rp, /*num_vertices=*/4, /*num_instances=*/1, 0, 0);
    SDL_EndGPURenderPass(rp);

    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

extern "C" bool Port_GPU_IsActive(void) {
    return sWindowClaimed && sPipeline != nullptr;
}

extern "C" void Port_GPU_Shutdown(void) {
    if (sPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(sDevice, sPipeline);
        sPipeline = nullptr;
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
    if (sFragShader) {
        SDL_ReleaseGPUShader(sDevice, sFragShader);
        sFragShader = nullptr;
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
