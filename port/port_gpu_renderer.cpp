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
extern "C" bool Port_GPU_IsActive(void) { return false; }
extern "C" void Port_GPU_Shutdown(void) {}

#else

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <cstdint>

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

static SDL_GPUDevice* sDevice    = nullptr;
static SDL_GPUShader* sVertShader = nullptr;
static SDL_GPUShader* sFragShader = nullptr;
static SDL_Window*    sWindow    = nullptr;

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

extern "C" bool Port_GPU_IsActive(void) {
    /* Stage 1: device exists but we do not yet drive presentation.
     * Returning false keeps port_ppu.cpp on the SDL_Renderer path.
     * Stage 2 will flip this once a real present pipeline lands. */
    return false;
}

extern "C" void Port_GPU_Shutdown(void) {
    if (sFragShader) {
        SDL_ReleaseGPUShader(sDevice, sFragShader);
        sFragShader = nullptr;
    }
    if (sVertShader) {
        SDL_ReleaseGPUShader(sDevice, sVertShader);
        sVertShader = nullptr;
    }
    if (sDevice) {
        /* Stage 2 will need a matching SDL_ReleaseWindowFromGPUDevice
         * once ClaimWindowForGPUDevice has actually been called. For
         * Stage 1 we only have the device, so skip. */
        SDL_DestroyGPUDevice(sDevice);
        sDevice = nullptr;
        sWindow = nullptr;
    }
}

#endif /* TMC_GPU_RENDERER */
