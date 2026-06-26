/*
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Software GBA PPU, vendored as first-party port source. Derived from
 * VirtuaPPU by Mathéo Vignaud (https://github.com/MatheoVignaud/VirtuaPPU,
 * commit 5cf5e99) and incorporating this project's 15 accuracy/portability
 * patches (formerly port/patches/viruappu-*.patch; preserved in git history).
 * Maintained here directly — not kept in sync with upstream.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../ppu_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*virtuappu_mode1_pre_line_fn)(int line);
extern virtuappu_mode1_pre_line_fn virtuappu_mode1_pre_line_callback;

/* PORT_WIDESCREEN_SPIKE: MODE1_GBA_WIDTH made overridable via
 * -DMODE1_GBA_WIDTH=N so the PC port can render wider frames. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif
/* OAM and BG clip extents (widescreen Phase 2 — Option A).
 *
 * MODE1_GBA_BG_CLIP_X (240) is the split point: BG columns < 240 read
 * the engine's VRAM screenblock as usual; columns >= 240 read the
 * port-side shadow tilemap (virtuappu_mode1_ws_shadow[], populated from
 * gMapData*Special) — the 32-tile screenblock only spans 256 px of world
 * and wraps past that, so the reveal columns can't come from VRAM. When
 * no shadow is registered (native 240, or non-gameplay screens) the
 * composite force-blacks past 240.
 *
 * MODE1_GBA_VIEWPORT_X is the OAM clip; it tracks MODE1_GBA_WIDTH so
 * sprites render across the full widescreen viewport. Engine-parked
 * off-screen sprites are kept out by the DISPLAY_WIDTH-relative culling
 * (CheckOnScreen / per-entity bounds), not by clipping here. */
#define MODE1_GBA_VIEWPORT_X MODE1_GBA_WIDTH
#define MODE1_GBA_BG_CLIP_X  240
enum {
    MODE1_GBA_HEIGHT = 160,
    MODE1_GBA_BG_COUNT = 4,
    MODE1_GBA_OAM_COUNT = 128,
    MODE1_IO_MEM_SIZE = 0x400,
    MODE1_VRAM_SIZE = 0x18000,
    MODE1_PALETTE_COLORS = 256,
    MODE1_OAM_HALFWORDS = 512
};

/* Widescreen Option A — port-side shadow tilemap for the reveal region
 * (display cols >= MODE1_GBA_BG_CLIP_X on 32-tile BGs). Populated by
 * port/port_linked_stubs.c::Port_Widescreen_UpdateShadows; a NULL entry
 * means "no shadow" => render_text_bg_line clips at 240 and the composite
 * force-blacks past it (native-240 / non-gameplay behaviour). COLS scales
 * with the configured width (reveal tiles = (W-240)/8, plus scroll/wrap
 * headroom); ROWS=32 mirrors the engine's mod-32 vertical row rolling. */
#define MODE1_WS_SHADOW_ROWS 32
#define MODE1_WS_SHADOW_COLS (((MODE1_GBA_WIDTH - 240) / 8) + 4)
extern uint16_t *virtuappu_mode1_ws_shadow[MODE1_GBA_BG_COUNT];
extern int virtuappu_mode1_ws_shadow_base_tile[MODE1_GBA_BG_COUNT];

/* Runtime WIP widescreen HUD anchor. BG0 stays 32 tiles wide, but gameplay
 * HUD uses both left-anchored widgets (hearts/charge) and right-anchored
 * widgets (rupees/keys). When enabled by the port, render BG0 cols
 * MODE1_WS_HUD_RIGHT_NATIVE_X..239 at the far right of the wide viewport
 * instead of in the middle of the revealed world. */
#define MODE1_WS_HUD_RIGHT_NATIVE_X 176
extern int virtuappu_mode1_ws_hud_right_anchor;

enum {
    MODE1_IO_DISPCNT = 0x00,
    MODE1_IO_BG0CNT = 0x08,
    MODE1_IO_BG1CNT = 0x0A,
    MODE1_IO_BG2CNT = 0x0C,
    MODE1_IO_BG3CNT = 0x0E,
    MODE1_IO_BG0HOFS = 0x10,
    MODE1_IO_BG0VOFS = 0x12,
    MODE1_IO_BG1HOFS = 0x14,
    MODE1_IO_BG1VOFS = 0x16,
    MODE1_IO_BG2HOFS = 0x18,
    MODE1_IO_BG2VOFS = 0x1A,
    MODE1_IO_BG3HOFS = 0x1C,
    MODE1_IO_BG3VOFS = 0x1E,
    MODE1_IO_WIN0H = 0x40,
    MODE1_IO_WIN1H = 0x42,
    MODE1_IO_WIN0V = 0x44,
    MODE1_IO_WIN1V = 0x46,
    MODE1_IO_WININ = 0x48,
    MODE1_IO_WINOUT = 0x4A,
    MODE1_IO_MOSAIC = 0x4C,
    MODE1_IO_BLDCNT = 0x50,
    MODE1_IO_BLDALPHA = 0x52,
    MODE1_IO_BLDY = 0x54
};

enum {
    MODE1_DISP_OBJ_1D = 0x0040,
    MODE1_DISP_FORCED_BLANK = 0x0080,
    MODE1_DISP_BG0_ON = 0x0100,
    MODE1_DISP_BG1_ON = 0x0200,
    MODE1_DISP_BG2_ON = 0x0400,
    MODE1_DISP_BG3_ON = 0x0800,
    MODE1_DISP_OBJ_ON = 0x1000,
    MODE1_DISP_WIN0_ON = 0x2000,
    MODE1_DISP_WIN1_ON = 0x4000,
    MODE1_DISP_OBJWIN_ON = 0x8000
};

typedef struct VirtuaPPUMode1GbaMemory {
    uint8_t *io_mem;
    uint8_t *vram;
    uint16_t *bg_palette;
    uint16_t *obj_palette;
    uint16_t *oam_mem;
} VirtuaPPUMode1GbaMemory;

void virtuappu_mode1_bind_gba_memory(const VirtuaPPUMode1GbaMemory *memory);
void virtuappu_mode1_get_bound_gba_memory(VirtuaPPUMode1GbaMemory *memory);
void virtuappu_mode1_set_frame_geometry(const PPUMemory *ppu);
int virtuappu_mode1_frame_width(void);
int virtuappu_mode1_frame_pitch(void);
uint16_t virtuappu_mode1_io_read16(uint16_t offset);
uint32_t virtuappu_mode1_io_read32(uint16_t offset);
uint32_t virtuappu_mode1_rgb555_to_abgr8888(uint16_t color);
void virtuappu_mode1_render_text_bg_line(int bg_index, int line, uint32_t *line_buffer, uint8_t *priority_buffer);
void virtuappu_mode1_render_obj_line(int line, bool obj_1d, uint32_t *line_buffer, uint8_t *priority_buffer);
void virtuappu_mode1_composite_line(
    int line,
    uint32_t bg_layers[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
    uint8_t bg_priority[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
    uint32_t obj_layer[MODE1_GBA_WIDTH],
    uint8_t obj_priority[MODE1_GBA_WIDTH],
    uint16_t dispcnt);
void virtuappu_mode1_render_frame(const PPUMemory *ppu);

/* Precompute the per-line affine BG2 internal reference point for one frame
 * (the #132 hardware latch). Pure function over per-line, post-HBlank-DMA
 * inputs, so the result can be consumed by the parallel render pass:
 *   - reload the internal reference from BG2X/BG2Y whenever a line's I/O value
 *     differs from the previous line's (a CPU/DMA write, e.g. the Deepwood
 *     barrel's per-scanline HBlank DMA);
 *   - otherwise advance it by dmx(pb)/dmy(pd) each scanline.
 * init_ref_{x,y} is the frame-start (pre-callback) reference; line_ref_{x,y}
 * are the post-callback references per line; out_ref_{x,y} receive the value to
 * render each line with. Exposed for unit testing (tools/ppu_affine_test.c). */
void virtuappu_mode1_affine_precompute(
    int height,
    int32_t init_ref_x, int32_t init_ref_y,
    const int32_t *line_ref_x, const int32_t *line_ref_y,
    const int16_t *line_pb, const int16_t *line_pd,
    int32_t *out_ref_x, int32_t *out_ref_y);

/* Sub-pixel re-render of OAM affine sprites into a (240*scale x 160*scale)
 * buffer. Called by the PC port at internal-render-scale > 1 after the
 * standard frame has been S*S nearest-replicated into `dst`. */
void virtuappu_mode1_render_affine_obj_overlay(uint32_t *dst, int dst_w, int dst_h, int scale);

#ifdef __cplusplus
}
#endif
