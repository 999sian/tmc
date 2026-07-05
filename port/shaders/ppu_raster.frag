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
layout(set = 2, binding = 3, std430) readonly buffer ObjPalette { uint data[]; } objpal;
layout(set = 2, binding = 4, std430) readonly buffer Oam { uint data[]; } oam;

layout(set = 3, binding = 0, std140) uniform Params {
    /* geom: x=frame_width, y=height, z=mode(GBA), w=affine(0/1) */
    ivec4 geom;
    /* misc: x=frame_dispcnt (frame-start DISPCNT; forced-blank + backdrop) */
    uvec4 misc;
} params;

layout(location = 0) out vec4 oColor;

/* DISPCNT / BGCNT / IO offsets (mirror cpu/mode1.h). */
const uint DISP_FORCED_BLANK = 0x0080u;
const uint DISP_OBJ_1D = 0x0040u;
const uint DISP_BG0_ON = 0x0100u;
const uint DISP_OBJ_ON = 0x1000u;
const uint OBJ_TILE_BASE = 0x10000u;
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
uint objpal_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (objpal.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
/* OAM is 512 halfwords; entry i uses oam[i*4+0..2] (attr0,attr1,attr2). */
uint oam_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (oam.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
int sx16(uint v) { return (v & 0x8000u) != 0u ? int(v) - 0x10000 : int(v); }

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

/* OBJ shape/size dimension tables (mirror mode1_obj_widths/heights). */
const int OBJ_W[12] = int[12](8, 16, 32, 64, 16, 32, 32, 64, 8, 8, 16, 32);
const int OBJ_H[12] = int[12](8, 16, 32, 64, 8, 8, 16, 32, 16, 32, 32, 64);

/* Resolve the OBJ layer at (x,line): lowest-index opaque non-window sprite
 * wins (matches the CPU's i=127..0 overwrite walk). Also reports the OBJ-window
 * mask (any opaque mode-2 sprite) and the semi-transparent flag of the winner.
 * Returns true when an opaque colour sprite covers the pixel. */
bool obj_resolve(int x, int line, uint line_dispcnt, out uint out_col, out int out_pri, out bool out_semi,
                 out bool out_window) {
    out_col = 0u;
    out_pri = 100;
    out_semi = false;
    out_window = false;
    bool found = false;
    bool obj_1d = (line_dispcnt & DISP_OBJ_1D) != 0u;
    int frame_width = params.geom.x;
    int viewport = frame_width; /* MODE1_GBA_VIEWPORT_X == MODE1_GBA_WIDTH >= frame_width */
    uint mosaic_reg = io_u16(line, IO_MOSAIC);
    int mos_h = int((mosaic_reg >> 8u) & 0xFu) + 1;
    int mos_v = int((mosaic_reg >> 12u) & 0xFu) + 1;

    for (int i = 0; i < 128; ++i) {
        uint attr0 = oam_u16(i * 4 + 0);
        uint attr1 = oam_u16(i * 4 + 1);
        uint attr2 = oam_u16(i * 4 + 2);
        bool affine = ((attr0 >> 8u) & 1u) != 0u;
        bool double_size = affine && (((attr0 >> 9u) & 1u) != 0u);
        bool hidden = !affine && (((attr0 >> 9u) & 1u) != 0u);
        if (hidden) {
            continue;
        }
        uint shape = (attr0 >> 14u) & 3u;
        uint size = (attr1 >> 14u) & 3u;
        if (shape >= 3u) {
            continue;
        }
        int obj_w = OBJ_W[int(shape) * 4 + int(size)];
        int obj_h = OBJ_H[int(shape) * 4 + int(size)];
        int bw = obj_w;
        int bh = obj_h;
        if (affine && double_size) {
            bw *= 2;
            bh *= 2;
        }
        int obj_y = int(attr0 & 0xFFu);
        if (obj_y >= 160) {
            obj_y -= 256;
        }
        if (line < obj_y || line >= obj_y + bh) {
            continue;
        }
        int obj_x = int(attr1 & 0x1FFu);
        if (obj_x >= frame_width) {
            obj_x -= 512;
        }
        if (obj_x + bw <= 0 || obj_x >= viewport) {
            continue;
        }
        if (x < obj_x || x >= obj_x + bw) {
            continue;
        }

        bool mosaic_on = (((attr0 >> 12u) & 1u) != 0u) && (mos_h > 1 || mos_v > 1);
        int eff_line = line;
        if (mosaic_on) {
            eff_line = line - (line % mos_v);
            if (eff_line < obj_y) {
                continue;
            }
        }
        int half_w = bw / 2;
        int half_h = bh / 2;
        int sprite_half_w = obj_w / 2;
        int sprite_half_h = obj_h / 2;
        int input_rel_y = eff_line - obj_y - half_h;
        bool mosaic_x_on = mosaic_on && mos_h > 1;

        int eff_sx = x - obj_x;
        if (mosaic_x_on) {
            int snapped_x = x - (x % mos_h);
            eff_sx = snapped_x - obj_x;
            if (eff_sx < 0) {
                continue;
            }
        }

        int tex_x;
        int tex_y;
        if (affine) {
            uint grp = (attr1 >> 9u) & 0x1Fu;
            int pa = sx16(oam_u16(int(grp) * 16 + 3));
            int pb = sx16(oam_u16(int(grp) * 16 + 7));
            int pc = sx16(oam_u16(int(grp) * 16 + 11));
            int pd = sx16(oam_u16(int(grp) * 16 + 15));
            int input_rel_x = eff_sx - half_w;
            tex_x = ((pa * input_rel_x + pb * input_rel_y) >> 8) + sprite_half_w;
            tex_y = ((pc * input_rel_x + pd * input_rel_y) >> 8) + sprite_half_h;
            if (tex_x < 0 || tex_x >= obj_w || tex_y < 0 || tex_y >= obj_h) {
                continue;
            }
        } else {
            bool hflip = ((attr1 >> 12u) & 1u) != 0u;
            bool vflip = ((attr1 >> 13u) & 1u) != 0u;
            int draw_x = hflip ? (obj_w - 1 - eff_sx) : eff_sx;
            int draw_y = eff_line - obj_y;
            if (vflip) {
                draw_y = obj_h - 1 - draw_y;
            }
            tex_x = draw_x;
            tex_y = draw_y;
        }

        int tile_row = tex_y / 8;
        int pixel_y = tex_y % 8;
        int tile_col = tex_x / 8;
        int pixel_x = tex_x % 8;
        int tiles_w = obj_w / 8;
        bool bpp8 = ((attr0 >> 13u) & 1u) != 0u;
        uint base_tile = attr2 & 0x3FFu;
        uint tile_index;
        if (obj_1d) {
            tile_index = bpp8 ? (base_tile + uint(tile_row * tiles_w + tile_col) * 2u)
                              : (base_tile + uint(tile_row * tiles_w + tile_col));
        } else {
            tile_index = bpp8 ? (base_tile + uint(tile_row * 32 + tile_col * 2))
                              : (base_tile + uint(tile_row * 32 + tile_col));
        }
        uint color_index;
        if (bpp8) {
            uint addr = OBJ_TILE_BASE + tile_index * 32u + uint(pixel_y) * 8u + uint(pixel_x);
            color_index = vram_u8(addr);
        } else {
            uint addr = OBJ_TILE_BASE + tile_index * 32u + uint(pixel_y) * 4u + uint(pixel_x / 2);
            uint packed = vram_u8(addr);
            color_index = ((pixel_x & 1) != 0) ? (packed >> 4u) : (packed & 0xFu);
        }
        if (color_index == 0u) {
            continue;
        }
        uint mode = (attr0 >> 10u) & 3u;
        if (mode == 2u) {
            out_window = true;
            continue;
        }
        if (!found) {
            uint pal_idx = bpp8 ? color_index : (((attr2 >> 12u) & 0xFu) * 16u + color_index);
            out_col = objpal_u16(int(pal_idx));
            out_pri = int((attr2 >> 10u) & 3u);
            out_semi = (mode == 1u);
            found = true;
        }
    }
    return found;
}

void main() {
    int x = int(gl_FragCoord.x);
    int line = int(gl_FragCoord.y);

    if ((params.misc.x & DISP_FORCED_BLANK) != 0u) {
        oColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    uint line_dispcnt = io_u16(line, IO_DISPCNT);
    uint backdrop = bgpal_u16(0);

    /* Gather BG layers. */
    bool bg_on[4];
    bool bg_op[4];
    uint bg_col[4];
    int bg_pri[4];
    for (int bg = 0; bg < 4; ++bg) {
        bg_pri[bg] = int(bg_priority(bg, line));
        bg_on[bg] = (line_dispcnt & (DISP_BG0_ON << uint(bg))) != 0u;
        bg_op[bg] = false;
        bg_col[bg] = 0u;
        if (bg_on[bg]) {
            uint pal_index;
            uint ci = bg_text_pixel(bg, x, line, pal_index);
            if (ci != 0u) {
                bg_op[bg] = true;
                bg_col[bg] = bgpal_u16(int(pal_index));
            }
        }
    }

    /* OBJ layer. */
    bool obj_on = (line_dispcnt & DISP_OBJ_ON) != 0u;
    uint obj_col = 0u;
    int obj_pri = 100;
    bool obj_semi = false;
    bool obj_window = false;
    bool obj_found = obj_on && obj_resolve(x, line, line_dispcnt, obj_col, obj_pri, obj_semi, obj_window);

    /* Stable priority sort of BG indices (index tie-break). */
    int order[4] = int[4](0, 1, 2, 3);
    for (int i = 1; i < 4; ++i) {
        int key = order[i];
        int kp = bg_pri[key];
        int j = i - 1;
        while (j >= 0 && bg_pri[order[j]] > kp) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    /* Merged walk: top + bottom layer (composite_line). Phase 3: no windows
     * (all layers visible) and no blend; output = top. */
    uint top_col = backdrop;
    int top_layer = 5;
    uint bot_col = backdrop;
    int bot_layer = 5;
    bool found_top = false;
    bool found_bottom = false;
    bool obj_candidate = obj_found;
    bool obj_emitted = false;

    for (int oi = 0; oi < 4 && !found_bottom; ++oi) {
        int bg = order[oi];
        if (obj_candidate && !obj_emitted && bg_pri[bg] >= obj_pri) {
            if (!found_top) { top_col = obj_col; top_layer = 4; found_top = true; }
            else { bot_col = obj_col; bot_layer = 4; found_bottom = true; }
            obj_emitted = true;
            if (found_bottom) { break; }
        }
        if (!bg_on[bg] || !bg_op[bg]) {
            continue;
        }
        if (!found_top) { top_col = bg_col[bg]; top_layer = bg; found_top = true; }
        else { bot_col = bg_col[bg]; bot_layer = bg; found_bottom = true; }
    }
    if (obj_candidate && !obj_emitted && !found_bottom) {
        if (!found_top) { top_col = obj_col; top_layer = 4; found_top = true; }
        else { bot_col = obj_col; bot_layer = 4; found_bottom = true; }
    }

    oColor = rgb555_to_rgba(top_col);
}
