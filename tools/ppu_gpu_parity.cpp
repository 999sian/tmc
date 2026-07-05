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

/* ---- BG helpers (build tiles/tilemaps in VRAM) ---- */

/* Fill a 16-colour palette bank (16 entries) with distinguishable colours. */
static void fill_palette_bank(uint16_t* pal, int bank, uint16_t backdrop) {
    if (bank == 0) {
        pal[0] = backdrop;
    }
    for (int i = (bank == 0 ? 1 : 0); i < 16; ++i) {
        /* spread across rgb555 so index changes are visible */
        int idx = bank * 16 + i;
        pal[idx] = (uint16_t)(((idx * 3) & 0x1F) | (((idx * 5) & 0x1F) << 5) | (((idx * 7) & 0x1F) << 10));
    }
}

/* Write a 4bpp 8x8 tile at char_base+tile*32. Pixel (px,py) colour index =
 * pattern(px,py) so hflip/vflip are observable. */
static void write_tile_4bpp(uint8_t* vram, uint32_t char_base, int tile) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = (uint8_t)(((px + py * 2) % 15) + 1); /* 1..15, never 0 */
            uint32_t addr = char_base + (uint32_t)tile * 32u + (uint32_t)py * 4u + (uint32_t)(px / 2);
            if (px & 1) {
                vram[addr] = (uint8_t)((vram[addr] & 0x0F) | (ci << 4));
            } else {
                vram[addr] = (uint8_t)((vram[addr] & 0xF0) | ci);
            }
        }
    }
}

/* Write an 8bpp 8x8 tile at char_base+tile*64. */
static void write_tile_8bpp(uint8_t* vram, uint32_t char_base, int tile) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = (uint8_t)(((px * 7 + py * 13) % 200) + 1); /* 1..200 */
            uint32_t addr = char_base + (uint32_t)tile * 64u + (uint32_t)py * 8u + (uint32_t)px;
            vram[addr] = ci;
        }
    }
}

/* Fill a 32x32 screenblock at screen_base with `entry`. */
static void fill_screenblock(uint8_t* vram, uint32_t screen_base, uint16_t entry) {
    for (int i = 0; i < 32 * 32; ++i) {
        uint32_t a = screen_base + (uint32_t)i * 2u;
        vram[a] = (uint8_t)(entry & 0xFF);
        vram[a + 1] = (uint8_t)(entry >> 8);
    }
}

/* Basic single-BG0 4bpp tiled scene: char base 0, screen base 0x4000
 * (block 8), tile 1, palette bank 2, backdrop dark grey. */
static void scene_bg0_tiled(Scene* s) {
    scene_clear(s, "bg0_tiled_4bpp");
    set_io16(s, 0x00, 0x0100);          /* DISPCNT: mode 0, BG0 on */
    set_io16(s, 0x08, 0x0800 | 0x0080); /* BG0CNT: screen_base block 16? see below */
    /* BG0CNT: char_base=0 (bits2-3=0), screen_base block = 8 (bits8-12=8 -> *0x800=0x4000),
     * 4bpp (bit7=0), priority 0. */
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* tile 1, palbank 2 */
}

/* BG0 with scroll (sub-tile + past-tile). */
static void scene_bg0_scroll(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_scroll";
    set_io16(s, 0x10, 13);  /* BG0HOFS = 13 */
    set_io16(s, 0x12, 100); /* BG0VOFS = 100 */
}

/* BG0 with per-tile hflip+vflip set in the tilemap entry. */
static void scene_bg0_flip(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_flip";
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (1u << 10) | (1u << 11) | (2u << 12)));
}

/* BG0 8bpp. */
static void scene_bg0_8bpp(Scene* s) {
    scene_clear(s, "bg0_8bpp");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 0x0080)); /* screen block 8, 8bpp */
    s->bgpal[0] = 0x0421;
    for (int i = 1; i < 256; ++i) {
        s->bgpal[i] = (uint16_t)(((i * 3) & 0x1F) | (((i * 5) & 0x1F) << 5) | (((i * 7) & 0x1F) << 10));
    }
    write_tile_8bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, 1);
}

/* BG0 mosaic (h=4, v=3). */
static void scene_bg0_mosaic(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_mosaic";
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 0x0040)); /* BG0CNT + mosaic bit 6 */
    set_io16(s, 0x4C, (uint16_t)((3u) | (2u << 4)));   /* MOSAIC: h-1=3, v-1=2 */
}

/* 512x256 BG0 (size flag 1): fill all 2 horizontal blocks + scroll across. */
static void scene_bg0_512(Scene* s) {
    scene_clear(s, "bg0_512wide");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)((8u << 8) | (1u << 14))); /* screen block 8, size 1 (512x256) */
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    write_tile_4bpp(s->vram, 0, 2);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* block 8 */
    fill_screenblock(s->vram, 0x4800, (uint16_t)(2u | (2u << 12))); /* block 9 */
    set_io16(s, 0x10, 260);                                         /* scroll into 2nd block */
}

/* Two BGs with different priorities: BG1 (priority 0) over BG0 (priority 1). */
static void scene_bg_priority(Scene* s) {
    scene_clear(s, "bg_priority");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200)); /* BG0+BG1 on */
    /* BG0: screen block 8, priority 1 */
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 1u));
    /* BG1: screen block 10, priority 0, char base 1 (0x4000) */
    set_io16(s, 0x0A, (uint16_t)((10u << 8) | (1u << 2) | 0u));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    fill_palette_bank(s->bgpal, 3, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);                                 /* BG0 tile, char base 0 */
    write_tile_4bpp(s->vram, 0x4000, 1);                            /* BG1 tile, char base 0x4000 */
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* BG0 map block 8 */
    fill_screenblock(s->vram, 0x5000, (uint16_t)(1u | (3u << 12))); /* BG1 map block 10 */
}

/* BG0 with a partly-transparent tile (color index 0 shows backdrop). */
static void scene_bg0_transparent(Scene* s) {
    scene_clear(s, "bg0_transparent");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x7FFF; /* white backdrop */
    fill_palette_bank(s->bgpal, 2, 0x7FFF);
    /* tile 1: checkerboard of index 0 (transparent) and index 5 */
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = ((px + py) & 1) ? 5 : 0;
            uint32_t addr = 0u + 1u * 32u + (uint32_t)py * 4u + (uint32_t)(px / 2);
            if (px & 1) {
                s->vram[addr] = (uint8_t)((s->vram[addr] & 0x0F) | (ci << 4));
            } else {
                s->vram[addr] = (uint8_t)((s->vram[addr] & 0xF0) | ci);
            }
        }
    }
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
}

/* ---- OBJ helpers ---- */
static void set_oam(Scene* s, int i, uint16_t a0, uint16_t a1, uint16_t a2) {
    s->oam[i * 4 + 0] = a0;
    s->oam[i * 4 + 1] = a1;
    s->oam[i * 4 + 2] = a2;
}
static void set_affine(Scene* s, int g, int16_t pa, int16_t pb, int16_t pc, int16_t pd) {
    s->oam[g * 16 + 3] = (uint16_t)pa;
    s->oam[g * 16 + 7] = (uint16_t)pb;
    s->oam[g * 16 + 11] = (uint16_t)pc;
    s->oam[g * 16 + 15] = (uint16_t)pd;
}
/* Fill OBJ tiles 0..63 at obj base (0x10000) with the 4bpp/8bpp patterns. */
static void fill_obj_tiles(Scene* s) {
    for (int t = 0; t < 64; ++t) {
        write_tile_4bpp(s->vram, 0x10000, t);
    }
    /* OBJ palette: bank 0 for 8bpp direct, plus banks for 4bpp. */
    for (int i = 1; i < 256; ++i) {
        s->objpal[i] = (uint16_t)(((i * 7) & 0x1F) | (((i * 3) & 0x1F) << 5) | (((i * 11) & 0x1F) << 10));
    }
}
/* attr0 helpers: y | (affine<<8) | (mode<<10) | (mosaic<<12) | (bpp8<<13) | (shape<<14) */
static uint16_t A0(int y, int affine, int mode, int mosaic, int bpp8, int shape) {
    return (uint16_t)((y & 0xFF) | (affine << 8) | (mode << 10) | (mosaic << 12) | (bpp8 << 13) | (shape << 14));
}
static uint16_t A0_hidden(int y, int shape) {
    return (uint16_t)((y & 0xFF) | (1 << 9) | (shape << 14));
}
/* attr1 (non-affine): x | (hflip<<12) | (vflip<<13) | (size<<14) */
static uint16_t A1(int x, int hflip, int vflip, int size) {
    return (uint16_t)((x & 0x1FF) | (hflip << 12) | (vflip << 13) | (size << 14));
}
/* attr1 (affine): x | (group<<9) | (size<<14) */
static uint16_t A1a(int x, int group, int size) {
    return (uint16_t)((x & 0x1FF) | (group << 9) | (size << 14));
}
/* attr2: tile | (priority<<10) | (palbank<<12) */
static uint16_t A2(int tile, int prio, int palbank) {
    return (uint16_t)((tile & 0x3FF) | (prio << 10) | (palbank << 12));
}

enum { DISP_OBJ_1D_BIT = 0x0040, DISP_OBJ_ENABLE = 0x1000 };
/* base OBJ scene: backdrop + tiles + 1D mapping, one 16x16 sprite. */
static void scene_obj_setup(Scene* s, const char* name) {
    scene_clear(s, name);
    set_io16(s, 0x00, (uint16_t)(DISP_OBJ_ENABLE | DISP_OBJ_1D_BIT)); /* OBJ on, 1D */
    s->bgpal[0] = 0x0421;
    fill_obj_tiles(s);
}

static void scene_obj_basic(Scene* s) {
    scene_obj_setup(s, "obj_basic_16x16");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2)); /* 16x16, palbank 2 */
}
static void scene_obj_hflip(Scene* s) {
    scene_obj_setup(s, "obj_hflip");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 1, 0, 1), A2(0, 0, 2));
}
static void scene_obj_vflip(Scene* s) {
    scene_obj_setup(s, "obj_vflip");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 1, 1), A2(0, 0, 2));
}
static void scene_obj_8bpp(Scene* s) {
    scene_obj_setup(s, "obj_8bpp");
    for (int t = 0; t < 64; ++t)
        write_tile_8bpp(s->vram, 0x10000, t);
    set_oam(s, 0, A0(30, 0, 0, 0, 1, 0), A1(60, 0, 0, 1), A2(0, 0, 0)); /* 8bpp 16x16 */
}
static void scene_obj_2d(Scene* s) {
    scene_obj_setup(s, "obj_2d_mapping");
    set_io16(s, 0x00, DISP_OBJ_ENABLE);                                 /* 2D mapping (no bit6) */
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 2), A2(0, 0, 2)); /* 32x32 */
}
static void scene_obj_affine(Scene* s) {
    scene_obj_setup(s, "obj_affine_rot");
    /* ~30deg rotation-ish matrix (8.8 fixed): pa=0xE0,pb=-0x80,pc=0x80,pd=0xE0 */
    set_affine(s, 0, 0x00E0, (int16_t)0xFF80, 0x0080, 0x00E0);
    set_oam(s, 0, A0(40, 1, 0, 0, 0, 0), A1a(60, 0, 2), A2(0, 0, 2)); /* affine 32x32, group 0 */
}
static void scene_obj_affine_double(Scene* s) {
    scene_obj_setup(s, "obj_affine_double");
    set_affine(s, 1, 0x0100, 0, 0, 0x0100); /* identity, double-size bounds */
    /* attr0 affine + double-size (bit9) */
    uint16_t a0 = (uint16_t)((40 & 0xFF) | (1 << 8) | (1 << 9));
    set_oam(s, 0, a0, A1a(60, 1, 2), A2(0, 0, 2));
}
static void scene_obj_over_bg(Scene* s) {
    scene_obj_setup(s, "obj_over_bg");
    set_io16(s, 0x00, (uint16_t)(DISP_OBJ_ENABLE | DISP_OBJ_1D_BIT | 0x0100)); /* + BG0 */
    set_io16(s, 0x08, (uint16_t)(8u << 8));                                    /* BG0 screen block 8, prio 0 */
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2)); /* obj prio 0 -> over BG prio 0 */
}
static void scene_obj_two_overlap(Scene* s) {
    scene_obj_setup(s, "obj_two_overlap");
    /* Two overlapping sprites; lower OAM index (0) must win the overlap. */
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2));
    set_oam(s, 1, A0(44, 0, 0, 0, 0, 0), A1(54, 0, 0, 1), A2(4, 0, 3));
}
static void scene_obj_wrap(Scene* s) {
    scene_obj_setup(s, "obj_xy_wrap");
    /* x near right edge wraps via -512; y>=160 wraps via -256. */
    set_oam(s, 0, A0(250, 0, 0, 0, 0, 1), A1(500, 0, 0, 1), A2(0, 0, 2)); /* y=250 -> -6 */
}
static void scene_obj_mosaic(Scene* s) {
    scene_obj_setup(s, "obj_mosaic");
    set_io16(s, 0x4C, (uint16_t)((3u << 8) | (2u << 12)));              /* OBJ mosaic h-1=3, v-1=2 */
    set_oam(s, 0, A0(40, 0, 0, 1, 0, 0), A1(50, 0, 0, 2), A2(0, 0, 2)); /* mosaic bit set, 32x32 */
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

    SceneFn scenes[] = { scene_forced_blank,    scene_backdrop_blue, scene_backdrop_black,    scene_backdrop_mixed,
                         scene_bg0_tiled,       scene_bg0_scroll,    scene_bg0_flip,          scene_bg0_8bpp,
                         scene_bg0_mosaic,      scene_bg0_512,       scene_bg_priority,       scene_bg0_transparent,
                         scene_obj_basic,       scene_obj_hflip,     scene_obj_vflip,         scene_obj_8bpp,
                         scene_obj_2d,          scene_obj_affine,    scene_obj_affine_double, scene_obj_over_bg,
                         scene_obj_two_overlap, scene_obj_wrap,      scene_obj_mosaic };
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
