#version 450
/*
 * GPU PPU rasterizer — fragment shader.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Faithful integer reimplementation of port/ppu/src/mode1.c, one GBA pixel per
 * fragment. Storage buffers hold the GBA memory the CPU sequential pass has
 * already prepared (VRAM, palettes, OAM, per-line IO snapshots, affine refs);
 * the fragment computes the composited ABGR8888 pixel for (x, line).
 *
 * Binding order is by feature-onset so each implementation phase declares a
 * contiguous prefix of storage buffers (keeps num_storage_buffers in sync):
 *   set=2 binding=0  bg_palette   (Phase 1)
 *   set=2 binding=1  io_per_line  (Phase 2)
 *   set=2 binding=2  vram         (Phase 2)
 *   set=2 binding=3  obj_palette  (Phase 3)
 *   set=2 binding=4  oam          (Phase 3)
 *   set=2 binding=5  affine_ref   (Phase 5)
 *
 * PHASE 2 (current): text BGs (GBA mode 0), all 4 layers, per-line IO
 * (scroll / char+screen base / 4bpp+8bpp / flip / 256|512 sizes / mosaic),
 * and BG priority compositing (BGCNT priority, BG-index tie-break). No OBJ,
 * windows, blending, or affine yet.
 */

layout(set = 2, binding = 0, std430) readonly buffer BgPalette { uint data[]; } bgpal;
layout(set = 2, binding = 1, std430) readonly buffer IoPerLine { uint data[]; } io;
layout(set = 2, binding = 2, std430) readonly buffer Vram { uint data[]; } vram;

layout(set = 3, binding = 0, std140) uniform Params {
    /* geom: x=frame_width, y=height, z=mode(GBA), w=affine(0/1) */
    ivec4 geom;
    /* misc: x=frame_dispcnt (frame-start DISPCNT; forced-blank + backdrop) */
    uvec4 misc;
} params;

layout(location = 0) out vec4 oColor;

/* DISPCNT / BGCNT / IO offsets (mirror cpu/mode1.h). */
const uint DISP_FORCED_BLANK = 0x0080u;
const uint DISP_BG0_ON = 0x0100u;
const int IO_DISPCNT = 0x00;
const int IO_BG0CNT = 0x08;
const int IO_BG0HOFS = 0x10;
const int IO_BG0VOFS = 0x12;
const int IO_MOSAIC = 0x4C;
const uint VRAM_SIZE = 0x18000u;

/* ---- storage-buffer accessors (byte/halfword views over u32 words) ---- */
uint bgpal_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (bgpal.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
uint io_u16(int line, int off) {
    uint b = uint(line) * 0x400u + uint(off);
    return (io.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
uint vram_u8(uint addr) {
    if (addr >= VRAM_SIZE) return 0u;
    return (vram.data[addr >> 2u] >> ((addr & 3u) * 8u)) & 0xFFu;
}
uint vram_u16(uint addr) {
    return vram_u8(addr) | (vram_u8(addr + 1u) << 8u);
}

vec4 rgb555_to_rgba(uint c) {
    uint r = (c & 0x1Fu) << 3u;
    uint g = ((c >> 5u) & 0x1Fu) << 3u;
    uint b = ((c >> 10u) & 0x1Fu) << 3u;
    return vec4(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0, 1.0);
}

/* Text-BG pixel (native path of virtuappu_mode1_render_text_bg_line).
 * Returns the palette index (0 = transparent) and, when opaque, `abgr`. */
uint bg_text_pixel(int bg, int x, int line, out uint pal_index) {
    int cnt_off = IO_BG0CNT + bg * 2;
    uint bgcnt = io_u16(line, cnt_off);
    uint char_base = ((bgcnt >> 2u) & 3u) * 0x4000u;
    bool mosaic_on = ((bgcnt >> 6u) & 1u) != 0u;
    bool bpp8 = ((bgcnt >> 7u) & 1u) != 0u;
    uint screen_base = ((bgcnt >> 8u) & 0x1Fu) * 0x800u;
    uint size_flag = (bgcnt >> 14u) & 3u;
    int map_w = (size_flag & 1u) != 0u ? 64 : 32;
    int map_h = (size_flag & 2u) != 0u ? 64 : 32;

    int scroll_x = int(io_u16(line, IO_BG0HOFS + bg * 4)) & 0x1FF;
    int scroll_y = int(io_u16(line, IO_BG0VOFS + bg * 4)) & 0x1FF;
    uint mosaic_reg = io_u16(line, IO_MOSAIC);
    int mh = mosaic_on ? int((mosaic_reg & 0xFu) + 1u) : 1;
    int mv = mosaic_on ? int(((mosaic_reg >> 4u) & 0xFu) + 1u) : 1;

    int eff_line = (mv == 1) ? line : (line / mv) * mv;
    int src_y = (eff_line + scroll_y) & (map_h * 8 - 1);
    int tile_row = src_y / 8;
    int pixel_y = src_y % 8;

    int eff_x = (mh == 1) ? x : (x / mh) * mh;
    int src_x = (eff_x + scroll_x) & (map_w * 8 - 1);
    int tile_col = src_x / 8;
    int pixel_x = src_x % 8;

    int screen_block_y = tile_row / 32;
    int local_row = tile_row % 32;
    int blocks_per_row = map_w / 32;
    int screen_block_x = tile_col / 32;
    int local_col = tile_col % 32;
    int screen_block_index = screen_block_x + screen_block_y * blocks_per_row;
    uint map_addr = screen_base + uint(screen_block_index) * 0x800u + uint(local_row * 32 + local_col) * 2u;
    uint entry = vram_u16(map_addr);

    uint tile_index = entry & 0x3FFu;
    bool hflip = ((entry >> 10u) & 1u) != 0u;
    bool vflip = ((entry >> 11u) & 1u) != 0u;
    uint pal_bank = (entry >> 12u) & 0xFu;
    int tpy = vflip ? (7 - pixel_y) : pixel_y;
    int tpx = hflip ? (7 - pixel_x) : pixel_x;

    uint color_index;
    if (bpp8) {
        uint addr = char_base + tile_index * 64u + uint(tpy) * 8u + uint(tpx);
        color_index = vram_u8(addr);
    } else {
        uint addr = char_base + tile_index * 32u + uint(tpy) * 4u + uint(tpx / 2);
        uint packed = vram_u8(addr);
        color_index = ((tpx & 1) != 0) ? (packed >> 4u) : (packed & 0xFu);
    }
    if (color_index == 0u) {
        pal_index = 0u;
        return 0u;
    }
    pal_index = bpp8 ? color_index : (pal_bank * 16u + color_index);
    return color_index;
}

uint bg_priority(int bg, int line) {
    return io_u16(line, IO_BG0CNT + bg * 2) & 3u;
}

void main() {
    int x = int(gl_FragCoord.x);
    int line = int(gl_FragCoord.y);

    /* Forced blank uses the frame-start DISPCNT (CPU checks it once). */
    if ((params.misc.x & DISP_FORCED_BLANK) != 0u) {
        oColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    uint line_dispcnt = io_u16(line, IO_DISPCNT);

    /* Backdrop = bg_palette[0]; below every BG. */
    uint top_pal = bgpal_u16(0);
    int top_pri = 100; /* above any BG priority (0..3) -> any BG wins */
    int top_bg = 9;

    for (int bg = 0; bg < 4; ++bg) {
        if ((line_dispcnt & (DISP_BG0_ON << uint(bg))) == 0u) {
            continue;
        }
        uint pal_index;
        uint ci = bg_text_pixel(bg, x, line, pal_index);
        if (ci == 0u) {
            continue;
        }
        int pri = int(bg_priority(bg, line));
        /* First opaque in (priority, bg-index) order wins — equals the CPU's
         * stable priority sort + first-opaque walk. */
        if (pri < top_pri || (pri == top_pri && bg < top_bg)) {
            top_pri = pri;
            top_bg = bg;
            top_pal = bgpal_u16(int(pal_index));
        }
    }

    oColor = rgb555_to_rgba(top_pal);
}
