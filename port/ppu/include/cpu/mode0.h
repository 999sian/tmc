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
#include <stddef.h>
#include <stdint.h>

#include "../ppu_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MODE0_VRAM_MAX_BYTES = 4u * 1024u * 1024u,
    MODE0_BG_COUNT = 4u,
    MODE0_OAM_COUNT = 512u,
    MODE0_TILEMAP_ENTRIES_PER_BG = 12000u,
    MODE0_MAX_LINES = 360u,
    MODE0_PALETTE_256_BANKS = 6u,
    MODE0_OBJ_AFFINE_COUNT = 64u
};

typedef struct Mode0Rgb888 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Mode0Rgb888;

typedef struct Mode0Palette16Rgb888 {
    Mode0Rgb888 colors[16];
} Mode0Palette16Rgb888;

typedef struct Mode0Palette256Rgb888 {
    Mode0Palette16Rgb888 palettes[16];
} Mode0Palette256Rgb888;

typedef uint32_t Mode0TileEntry;

enum {
    MODE0_TILE_HFLIP = 1u << 27,
    MODE0_TILE_VFLIP = 1u << 28,
    MODE0_TILE_MOSAIC = 1u << 29
};

enum {
    MODE0_BG_FLAG_ENABLED = 1u << 0,
    MODE0_BG_FLAG_BPP8 = 1u << 1,
    MODE0_BG_FLAG_WRAP_X = 1u << 2,
    MODE0_BG_FLAG_WRAP_Y = 1u << 3,
    MODE0_BG_FLAG_AFFINE = 1u << 4,
    MODE0_BG_FLAG_MOSAIC = 1u << 5
};

typedef struct Mode0Affine2x2_8_8 {
    int16_t a;
    int16_t b;
    int16_t c;
    int16_t d;
} Mode0Affine2x2_8_8;

typedef struct Mode0BgEntry {
    uint16_t tile_base;
    uint16_t palette_index;
    int16_t scroll_x;
    int16_t scroll_y;
    uint16_t flags;
    uint8_t layer_priority;
    uint8_t mosaic_size_x;
    uint8_t mosaic_size_y;
    uint8_t pad0;
    Mode0Affine2x2_8_8 matrix;
    int32_t tx;
    int32_t ty;
} Mode0BgEntry;

enum {
    MODE0_OAM_FLAG_ENABLED = 1u << 0,
    MODE0_OAM_FLAG_BPP8 = 1u << 1,
    MODE0_OAM_FLAG_HFLIP = 1u << 2,
    MODE0_OAM_FLAG_VFLIP = 1u << 3,
    MODE0_OAM_FLAG_MOSAIC = 1u << 4,
    MODE0_OAM_FLAG_AFFINE = 1u << 5,
    MODE0_OAM_FLAG_DOUBLE_SIZE = 1u << 6,
    MODE0_OAM_FLAG_SEMI_TRANSP = 1u << 7,
    MODE0_OAM_FLAG_OBJ_WINDOW = 1u << 8
};

typedef struct Mode0ObjAffine {
    Mode0Affine2x2_8_8 matrix;
} Mode0ObjAffine;

typedef struct Mode0OAMEntry {
    int16_t y;
    int16_t x;
    uint8_t height_blocks;
    uint8_t width_blocks;
    uint16_t palette_index;
    uint16_t tile_index;
    uint8_t priority;
    uint8_t affine_index;
    uint16_t flags;
} Mode0OAMEntry;

enum {
    MODE0_LAYER_BG0 = 1u << 0,
    MODE0_LAYER_BG1 = 1u << 1,
    MODE0_LAYER_BG2 = 1u << 2,
    MODE0_LAYER_BG3 = 1u << 3,
    MODE0_LAYER_OBJ = 1u << 4,
    MODE0_LAYER_COLORMATH = 1u << 5
};

typedef struct Mode0WindowRect {
    uint16_t x1;
    uint16_t x2;
    uint16_t y1;
    uint16_t y2;
} Mode0WindowRect;

typedef struct Mode0WindowCtrl {
    Mode0WindowRect rect;
    uint16_t enable_mask;
    uint16_t flags;
} Mode0WindowCtrl;

typedef struct Mode0ColorMathCtrl {
    uint8_t mode;
    uint8_t eva;
    uint8_t evb;
    uint8_t half;
    uint16_t target_a;
    uint16_t target_b;
    uint8_t fade_to_white;
    uint8_t fade_to_black;
    uint8_t fade_factor;
    uint8_t pad;
} Mode0ColorMathCtrl;

typedef struct Mode0PPURegs {
    Mode0Rgb888 backdrop_color;
    uint16_t master_enable_mask;
    Mode0WindowCtrl win0;
    Mode0WindowCtrl win1;
    uint16_t outside_enable_mask;
    uint16_t use_obj_window;
    Mode0ColorMathCtrl color_math;
} Mode0PPURegs;

typedef struct Mode0LineScroll {
    int16_t scroll_x;
    int16_t scroll_y;
} Mode0LineScroll;

typedef struct Mode0LineAffineTxTy {
    int32_t tx;
    int32_t ty;
} Mode0LineAffineTxTy;

typedef struct Mode0Layout {
    Mode0PPURegs regs;
    Mode0BgEntry bg[MODE0_BG_COUNT];
    Mode0TileEntry tilemaps[MODE0_BG_COUNT][MODE0_TILEMAP_ENTRIES_PER_BG];
    uint8_t gfx_data[2u * 1024u * 1024u];
    Mode0Palette256Rgb888 palettes[MODE0_PALETTE_256_BANKS];
    Mode0ObjAffine obj_affine[MODE0_OBJ_AFFINE_COUNT];
    Mode0OAMEntry oam[MODE0_OAM_COUNT];
    Mode0LineScroll bg_line_scroll[MODE0_BG_COUNT][MODE0_MAX_LINES];
    Mode0LineAffineTxTy bg_line_affine[MODE0_BG_COUNT][MODE0_MAX_LINES];
} Mode0Layout;

Mode0TileEntry mode0_make_tile_entry(
    uint16_t tile_index,
    uint8_t palette_index,
    uint8_t priority,
    bool hflip,
    bool vflip,
    bool mosaic_enable);

void virtuappu_mode0_set_palette16(size_t palette_bank_index, size_t palette_index_in_bank, const Mode0Palette16Rgb888 *palette);
void virtuappu_mode0_set_palette256(size_t palette_bank_index, const Mode0Palette256Rgb888 *palette);
void virtuappu_mode0_set_gfx_data(const uint8_t *data, size_t size, size_t offset);
void virtuappu_mode0_set_tilemap_entry(size_t bg_index, size_t entry_index, Mode0TileEntry entry);
void virtuappu_mode0_set_bg_entry(size_t bg_index, const Mode0BgEntry *bg_entry);
void virtuappu_mode0_set_oam_entry(size_t oam_index, const Mode0OAMEntry *oam_entry);
void virtuappu_mode0_set_ppu_regs(const Mode0PPURegs *regs);
void virtuappu_mode0_set_bg_line_scroll(size_t bg_index, size_t line_index, const Mode0LineScroll *line_scroll);
void virtuappu_mode0_set_bg_line_affine_tx_ty(size_t bg_index, size_t line_index, const Mode0LineAffineTxTy *line_affine);
void virtuappu_mode0_render_frame(const PPUMemory *ppu);

#ifdef __cplusplus
}
#endif
