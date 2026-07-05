/*
 * port_gpu_raster.cpp — GPU PPU rasterizer host side.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See port_gpu_raster.h and docs/gpu-rasterizer-design.md. Uploads the
 * CPU-prepared GBA memory to storage buffers, runs the fragment PPU shader
 * into an offscreen R8G8B8A8_UNORM target, and reads it back on demand.
 */

#include "port_gpu_raster.h"

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <cstring>
#include <new>
/* Storage-buffer slots, ordered by feature-onset so each implementation phase
 * binds a contiguous prefix (must match ppu_raster.frag's binding numbers).
 * Bump PORT_GPU_RASTER_NUM_SSBO as phases add layers; keep it equal to the
 * shader's num_storage_buffers. */
enum {
    SSBO_BG_PALETTE = 0,
    SSBO_IO_PER_LINE = 1,
    SSBO_VRAM = 2,
    SSBO_OBJ_PALETTE = 3,
    SSBO_OAM = 4,
    SSBO_AFFINE_REF = 5,
    SSBO_SLOT_COUNT = 6
};

/* Phase 1 binds only the BG palette. */
#define PORT_GPU_RASTER_NUM_SSBO 1

struct PortGpuRaster {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUShader* vert = nullptr;
    SDL_GPUShader* frag = nullptr;

    /* Persistent per-slot storage buffers + upload transfer buffers, grown on
     * demand to the frame's requirement. */
    SDL_GPUBuffer* ssbo[SSBO_SLOT_COUNT] = {};
    SDL_GPUTransferBuffer* upload[SSBO_SLOT_COUNT] = {};
    Uint32 ssbo_cap[SSBO_SLOT_COUNT] = {};

    SDL_GPUTexture* target = nullptr;
    int target_w = 0;
    int target_h = 0;

    SDL_GPUTransferBuffer* download = nullptr;
    Uint32 download_cap = 0;
};

/* std140 mirror of ppu_raster.frag's Params (two 16-byte vectors). */
struct RasterParams {
    int32_t geom[4];  /* width, height, mode, affine */
    uint32_t misc[4]; /* frame_dispcnt, pad, pad, pad */
};

static SDL_GPUShader* make_shader(SDL_GPUDevice* dev, const void* code, size_t len, SDL_GPUShaderStage stage,
                                  Uint32 num_storage, Uint32 num_uniform) {
    SDL_GPUShaderCreateInfo ci;
    SDL_zero(ci);
    ci.code = static_cast<const Uint8*>(code);
    ci.code_size = len;
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_storage_buffers = num_storage;
    ci.num_uniform_buffers = num_uniform;
    SDL_GPUShader* s = SDL_CreateGPUShader(dev, &ci);
    if (!s) {
        std::fprintf(stderr, "[gpuraster] CreateGPUShader failed: %s\n", SDL_GetError());
    }
    return s;
}

extern "C" PortGpuRaster* Port_GpuRaster_Create(SDL_GPUDevice* device, const void* vert_spv, size_t vert_len,
                                                const void* frag_spv, size_t frag_len) {
    if (!device || !vert_spv || !frag_spv) {
        return nullptr;
    }
    PortGpuRaster* r = new (std::nothrow) PortGpuRaster();
    if (!r) {
        return nullptr;
    }
    r->device = device;

    r->vert = make_shader(device, vert_spv, vert_len, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    r->frag = make_shader(device, frag_spv, frag_len, SDL_GPU_SHADERSTAGE_FRAGMENT, PORT_GPU_RASTER_NUM_SSBO, 1);
    if (!r->vert || !r->frag) {
        Port_GpuRaster_Destroy(r);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd;
    SDL_zero(ctd);
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_GPUGraphicsPipelineCreateInfo pci;
    SDL_zero(pci);
    pci.vertex_shader = r->vert;
    pci.fragment_shader = r->frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &ctd;
    r->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    if (!r->pipeline) {
        std::fprintf(stderr, "[gpuraster] CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        Port_GpuRaster_Destroy(r);
        return nullptr;
    }
    return r;
}

extern "C" void Port_GpuRaster_Destroy(PortGpuRaster* r) {
    if (!r) {
        return;
    }
    SDL_GPUDevice* d = r->device;
    if (d) {
        for (int i = 0; i < SSBO_SLOT_COUNT; ++i) {
            if (r->ssbo[i]) {
                SDL_ReleaseGPUBuffer(d, r->ssbo[i]);
            }
            if (r->upload[i]) {
                SDL_ReleaseGPUTransferBuffer(d, r->upload[i]);
            }
        }
        if (r->download) {
            SDL_ReleaseGPUTransferBuffer(d, r->download);
        }
        if (r->target) {
            SDL_ReleaseGPUTexture(d, r->target);
        }
        if (r->pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(d, r->pipeline);
        }
        if (r->vert) {
            SDL_ReleaseGPUShader(d, r->vert);
        }
        if (r->frag) {
            SDL_ReleaseGPUShader(d, r->frag);
        }
    }
    delete r;
}

/* Ensure slot `i` has a storage buffer + upload transfer buffer of >= `size`
 * bytes. Returns false on allocation failure. */
static bool ensure_ssbo(PortGpuRaster* r, int i, Uint32 size) {
    if (size == 0) {
        size = 4;
    }
    if (r->ssbo[i] && r->ssbo_cap[i] >= size) {
        return true;
    }
    if (r->ssbo[i]) {
        SDL_ReleaseGPUBuffer(r->device, r->ssbo[i]);
        r->ssbo[i] = nullptr;
    }
    if (r->upload[i]) {
        SDL_ReleaseGPUTransferBuffer(r->device, r->upload[i]);
        r->upload[i] = nullptr;
    }
    SDL_GPUBufferCreateInfo bci;
    SDL_zero(bci);
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = size;
    r->ssbo[i] = SDL_CreateGPUBuffer(r->device, &bci);
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_zero(tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;
    r->upload[i] = SDL_CreateGPUTransferBuffer(r->device, &tci);
    if (!r->ssbo[i] || !r->upload[i]) {
        std::fprintf(stderr, "[gpuraster] ssbo alloc failed (slot %d, %u bytes): %s\n", i, size, SDL_GetError());
        return false;
    }
    r->ssbo_cap[i] = size;
    return true;
}

/* Map, copy `src`→`size` into slot `i`'s upload buffer, and record an upload in
 * `cp`. Returns false on map failure. */
static bool stage_upload(PortGpuRaster* r, SDL_GPUCopyPass* cp, int i, const void* src, Uint32 size) {
    if (!ensure_ssbo(r, i, size)) {
        return false;
    }
    void* map = SDL_MapGPUTransferBuffer(r->device, r->upload[i], false);
    if (!map) {
        std::fprintf(stderr, "[gpuraster] map upload slot %d failed: %s\n", i, SDL_GetError());
        return false;
    }
    std::memcpy(map, src, size);
    SDL_UnmapGPUTransferBuffer(r->device, r->upload[i]);

    SDL_GPUTransferBufferLocation loc;
    SDL_zero(loc);
    loc.transfer_buffer = r->upload[i];
    loc.offset = 0;
    SDL_GPUBufferRegion reg;
    SDL_zero(reg);
    reg.buffer = r->ssbo[i];
    reg.offset = 0;
    reg.size = size;
    SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
    return true;
}

static bool ensure_target(PortGpuRaster* r, int w, int h) {
    if (r->target && r->target_w == w && r->target_h == h) {
        return true;
    }
    if (r->target) {
        SDL_ReleaseGPUTexture(r->device, r->target);
        r->target = nullptr;
    }
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = static_cast<Uint32>(w);
    ti.height = static_cast<Uint32>(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    r->target = SDL_CreateGPUTexture(r->device, &ti);
    if (!r->target) {
        std::fprintf(stderr, "[gpuraster] CreateGPUTexture %dx%d failed: %s\n", w, h, SDL_GetError());
        return false;
    }
    r->target_w = w;
    r->target_h = h;
    return true;
}

extern "C" SDL_GPUTexture* Port_GpuRaster_Render(PortGpuRaster* r, const PortGpuRasterFrame* f) {
    if (!r || !f || f->frame_width <= 0 || f->frame_height <= 0) {
        return nullptr;
    }
    if (!ensure_target(r, f->frame_width, f->frame_height)) {
        return nullptr;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return nullptr;
    }

    /* Upload the storage buffers the current phase binds. */
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    bool ok = true;
    ok = ok && stage_upload(r, cp, SSBO_BG_PALETTE, f->bg_palette, 256u * sizeof(uint16_t));
    SDL_EndGPUCopyPass(cp);
    if (!ok) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return nullptr;
    }

    RasterParams p;
    SDL_zero(p);
    p.geom[0] = f->frame_width;
    p.geom[1] = f->frame_height;
    p.geom[2] = f->mode;
    p.geom[3] = f->affine ? 1 : 0;
    p.misc[0] = f->frame_dispcnt;

    SDL_GPUColorTargetInfo ct;
    SDL_zero(ct);
    ct.texture = r->target;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_BindGPUGraphicsPipeline(rp, r->pipeline);
    SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof(p));
    SDL_GPUBuffer* bind[PORT_GPU_RASTER_NUM_SSBO];
    for (int i = 0; i < PORT_GPU_RASTER_NUM_SSBO; ++i) {
        bind[i] = r->ssbo[i];
    }
    SDL_BindGPUFragmentStorageBuffers(rp, 0, bind, PORT_GPU_RASTER_NUM_SSBO);
    SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
    SDL_EndGPURenderPass(rp);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }
    return r->target;
}

extern "C" bool Port_GpuRaster_Readback(PortGpuRaster* r, uint32_t* dst, int width, int height, int pitch) {
    if (!r || !r->target || !dst || width <= 0 || height <= 0) {
        return false;
    }
    if (width > r->target_w || height > r->target_h) {
        return false;
    }
    Uint32 need = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;
    if (!r->download || r->download_cap < need) {
        if (r->download) {
            SDL_ReleaseGPUTransferBuffer(r->device, r->download);
            r->download = nullptr;
        }
        SDL_GPUTransferBufferCreateInfo tci;
        SDL_zero(tci);
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tci.size = need;
        r->download = SDL_CreateGPUTransferBuffer(r->device, &tci);
        if (!r->download) {
            std::fprintf(stderr, "[gpuraster] download alloc failed: %s\n", SDL_GetError());
            return false;
        }
        r->download_cap = need;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = r->target;
    reg.w = static_cast<Uint32>(width);
    reg.h = static_cast<Uint32>(height);
    reg.d = 1;
    SDL_GPUTextureTransferInfo dti;
    SDL_zero(dti);
    dti.transfer_buffer = r->download;
    dti.offset = 0;
    dti.pixels_per_row = static_cast<Uint32>(width);
    dti.rows_per_layer = static_cast<Uint32>(height);
    SDL_DownloadFromGPUTexture(cp, &reg, &dti);
    SDL_EndGPUCopyPass(cp);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }

    const uint32_t* src = static_cast<const uint32_t*>(SDL_MapGPUTransferBuffer(r->device, r->download, false));
    if (!src) {
        std::fprintf(stderr, "[gpuraster] map download failed: %s\n", SDL_GetError());
        return false;
    }
    if (pitch <= 0) {
        pitch = width;
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(&dst[static_cast<size_t>(y) * static_cast<size_t>(pitch)], &src[static_cast<size_t>(y) * width],
                    static_cast<size_t>(width) * sizeof(uint32_t));
    }
    SDL_UnmapGPUTransferBuffer(r->device, r->download);
    return true;
}
