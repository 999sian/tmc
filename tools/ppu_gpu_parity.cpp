/*
 * tools/ppu_gpu_parity.cpp — CPU-vs-GPU PPU parity harness.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Renders identical GBA memory states through BOTH the CPU software
 * rasterizer (port/ppu/src/mode1.c via virtuappu_render_frame) and the GPU
 * rasterizer (port/port_gpu_raster.cpp), then diffs the two framebuffers
 * pixel-for-pixel. Target is bit-exact; see docs/gpu-rasterizer-design.md.
 *
 * Headless: SDL_GPU on the `offscreen` video driver (Vulkan/Metal/D3D12).
 * Each implementation phase adds scenes here to guard its feature.
 *
 * Usage:
 *   ppu_gpu_parity [vert.spv] [frag.spv]
 * Defaults to port/shaders/build/ppu_raster.{vert,frag}.spv relative to CWD.
 * Exit 0 = all scenes bit-exact (or within documented tolerance), nonzero on
 * mismatch / setup failure.
 */

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "cpu/mode1.h"
#include "virtuappu.h"
}
#include "port_gpu_raster.h"

/* ---- synthetic GBA memory for one scene ---- */
struct Scene {
    const char* name;
    uint8_t io[0x400];
    uint8_t vram[0x18000];
    uint16_t bgpal[256];
    uint16_t objpal[256];
    uint16_t oam[512];
};

static void scene_clear(Scene* s, const char* name) {
    std::memset(s, 0, sizeof(*s));
    s->name = name;
    /* Hidden OAM by default (attr0 bit 9 set, non-affine) so stray sprites
     * don't render; matches how the engine parks unused entries. */
    for (int i = 0; i < 128; ++i) {
        s->oam[i * 4 + 0] = 0x0200; /* disable bit */
    }
}

static void set_io16(Scene* s, int off, uint16_t v) {
    s->io[off] = (uint8_t)(v & 0xFF);
    s->io[off + 1] = (uint8_t)(v >> 8);
}

/* rgb555 -> ABGR8888, identical to virtuappu_mode1_rgb555_to_abgr8888. */
static uint32_t rgb555_to_abgr(uint16_t c) {
    uint32_t r = (uint32_t)((c & 0x1F) << 3);
    uint32_t g = (uint32_t)(((c >> 5) & 0x1F) << 3);
    uint32_t b = (uint32_t)(((c >> 10) & 0x1F) << 3);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* Render `s` on the CPU rasterizer into `out` (WxH, tightly packed). */
static void render_cpu(Scene* s, uint32_t* out, int w, int h) {
    VirtuaPPUMode1GbaMemory mem = { s->io, s->vram, s->bgpal, s->objpal, s->oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;
    uint16_t dispcnt = (uint16_t)(s->io[0] | (s->io[1] << 8));
    int gba_mode = dispcnt & 0x7;
    uint8_t vppu_mode = (gba_mode == 1 || gba_mode == 2) ? 2 : 1;
    virtuappu_registers.mode = vppu_mode;
    virtuappu_registers.frame_width = (uint16_t)w;
    virtuappu_registers.frame_pitch = (uint16_t)w;
    virtuappu_render_frame();
    for (int y = 0; y < h; ++y) {
        std::memcpy(&out[(size_t)y * w], &virtuappu_frame_buffer[(size_t)y * w], (size_t)w * sizeof(uint32_t));
    }
}

/* Build the per-line arrays the GPU frame ABI expects (constant across lines
 * when there is no HDMA callback, which is the case for these static scenes). */
static void render_gpu(PortGpuRaster* r, Scene* s, uint32_t* out, int w, int h) {
    static std::vector<uint8_t> io_per_line;
    static std::vector<uint16_t> dispcnt_per_line;
    io_per_line.assign((size_t)h * 0x400, 0);
    dispcnt_per_line.assign((size_t)h, 0);
    uint16_t dispcnt = (uint16_t)(s->io[0] | (s->io[1] << 8));
    for (int y = 0; y < h; ++y) {
        std::memcpy(&io_per_line[(size_t)y * 0x400], s->io, 0x400);
        dispcnt_per_line[y] = dispcnt;
    }
    int gba_mode = dispcnt & 0x7;

    PortGpuRasterFrame f;
    std::memset(&f, 0, sizeof(f));
    f.frame_width = w;
    f.frame_height = h;
    f.frame_pitch = w;
    f.mode = (gba_mode == 1 || gba_mode == 2) ? 2 : 1;
    f.affine = (f.mode == 2);
    f.frame_dispcnt = dispcnt;
    f.vram = s->vram;
    f.bg_palette = s->bgpal;
    f.obj_palette = s->objpal;
    f.oam = s->oam;
    f.io_per_line = io_per_line.data();
    f.dispcnt_per_line = dispcnt_per_line.data();
    f.affine_ref_x = NULL;
    f.affine_ref_y = NULL;

    SDL_GPUTexture* tex = Port_GpuRaster_Render(r, &f);
    if (!tex) {
        std::fprintf(stderr, "  GPU render returned NULL\n");
        std::memset(out, 0xAB, (size_t)w * h * sizeof(uint32_t)); /* poison -> diff */
        return;
    }
    if (!Port_GpuRaster_Readback(r, out, w, h, w)) {
        std::fprintf(stderr, "  GPU readback failed\n");
        std::memset(out, 0xCD, (size_t)w * h * sizeof(uint32_t));
    }
}

/* Compare; return diff count, log first diff. */
static int diff(const uint32_t* cpu, const uint32_t* gpu, int w, int h, const char* name) {
    int diffs = 0;
    int fx = -1, fy = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t a = cpu[(size_t)y * w + x];
            uint32_t b = gpu[(size_t)y * w + x];
            if (a != b) {
                if (diffs == 0) {
                    fx = x;
                    fy = y;
                }
                ++diffs;
            }
        }
    }
    if (diffs == 0) {
        std::printf("  [PASS] %-24s bit-exact (%dx%d)\n", name, w, h);
    } else {
        std::printf("  [FAIL] %-24s %d/%d px differ; first (%d,%d) cpu=0x%08x gpu=0x%08x\n", name, diffs, w * h, fx, fy,
                    cpu[(size_t)fy * w + fx], gpu[(size_t)fy * w + fx]);
    }
    return diffs;
}

static void* load_file(const char* path, size_t* len) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    void* b = std::malloc((size_t)n);
    if (std::fread(b, 1, (size_t)n, f) != (size_t)n) {
        std::free(b);
        std::fclose(f);
        return NULL;
    }
    std::fclose(f);
    *len = (size_t)n;
    return b;
}

/* ---- scenes ---- */
typedef void (*SceneFn)(Scene*);

static void scene_forced_blank(Scene* s) {
    scene_clear(s, "forced_blank");
    set_io16(s, 0x00, 0x0080); /* DISPCNT: forced blank */
    s->bgpal[0] = 0x1234;      /* ignored under forced blank */
}
static void scene_backdrop_blue(Scene* s) {
    scene_clear(s, "backdrop_blue");
    set_io16(s, 0x00, 0x0000); /* mode 0, all layers off */
    s->bgpal[0] = 0x7C00;      /* pure blue in rgb555 (bit 10-14) */
}
static void scene_backdrop_black(Scene* s) {
    scene_clear(s, "backdrop_black");
    set_io16(s, 0x00, 0x0000);
    s->bgpal[0] = 0x0000;
}
static void scene_backdrop_mixed(Scene* s) {
    scene_clear(s, "backdrop_mixed");
    set_io16(s, 0x00, 0x0000);
    s->bgpal[0] = 0x3DEF; /* arbitrary */
}

int main(int argc, char** argv) {
    const char* vpath = argc > 1 ? argv[1] : "port/shaders/build/ppu_raster.vert.spv";
    const char* fpath = argc > 2 ? argv[2] : "port/shaders/build/ppu_raster.frag.spv";

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) {
        std::fprintf(stderr, "CreateGPUDevice failed: %s\n", SDL_GetError());
        return 2;
    }
    std::printf("GPU parity harness — backend=%s\n", SDL_GetGPUDeviceDriver(dev));

    size_t vlen = 0, flen = 0;
    void* vspv = load_file(vpath, &vlen);
    void* fspv = load_file(fpath, &flen);
    if (!vspv || !fspv) {
        return 2;
    }
    PortGpuRaster* r = Port_GpuRaster_Create(dev, vspv, vlen, fspv, flen);
    if (!r) {
        std::fprintf(stderr, "Port_GpuRaster_Create failed\n");
        return 2;
    }

    const int W = 240, H = 160;
    static Scene scene;
    std::vector<uint32_t> cpu((size_t)W * H), gpu((size_t)W * H);

    SceneFn scenes[] = { scene_forced_blank, scene_backdrop_blue, scene_backdrop_black, scene_backdrop_mixed };
    int total_diffs = 0;
    for (SceneFn fn : scenes) {
        fn(&scene);
        render_cpu(&scene, cpu.data(), W, H);
        render_gpu(r, &scene, gpu.data(), W, H);
        total_diffs += diff(cpu.data(), gpu.data(), W, H, scene.name);
    }

    Port_GpuRaster_Destroy(r);
    SDL_DestroyGPUDevice(dev);
    SDL_Quit();
    std::free(vspv);
    std::free(fspv);

    if (total_diffs == 0) {
        std::printf("ALL SCENES BIT-EXACT\n");
        return 0;
    }
    std::printf("MISMATCH: %d total differing pixels\n", total_diffs);
    return 1;
}
