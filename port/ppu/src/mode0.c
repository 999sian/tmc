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

#include "cpu/mode0.h"

#include <string.h>

#include "virtuappu.h"

_Static_assert(sizeof(Mode0Layout) <= MODE0_VRAM_MAX_BYTES, "Mode0Layout exceeds 4MB");
_Static_assert(sizeof(Mode0TileEntry) == 4u, "Mode0TileEntry must stay 32-bit");

static Mode0Layout *mode0_get_layout(void)
{
    return (Mode0Layout *)virtuappu_vram;
}

Mode0TileEntry mode0_make_tile_entry(
    uint16_t tile_index,
    uint8_t palette_index,
    uint8_t priority,
    bool hflip,
    bool vflip,
    bool mosaic_enable)
{
    return ((uint32_t)tile_index & 0xFFFFu) |
           ((uint32_t)palette_index << 16u) |
           (((uint32_t)priority & 0x7u) << 24u) |
           (hflip ? MODE0_TILE_HFLIP : 0u) |
           (vflip ? MODE0_TILE_VFLIP : 0u) |
           (mosaic_enable ? MODE0_TILE_MOSAIC : 0u);
}

void virtuappu_mode0_set_palette16(size_t palette_bank_index, size_t palette_index_in_bank, const Mode0Palette16Rgb888 *palette)
{
    Mode0Layout *layout = mode0_get_layout();

    if (palette == NULL || palette_bank_index >= MODE0_PALETTE_256_BANKS || palette_index_in_bank >= 16u) {
        return;
    }

    layout->palettes[palette_bank_index].palettes[palette_index_in_bank] = *palette;
}

void virtuappu_mode0_set_palette256(size_t palette_bank_index, const Mode0Palette256Rgb888 *palette)
{
    Mode0Layout *layout = mode0_get_layout();

    if (palette == NULL || palette_bank_index >= MODE0_PALETTE_256_BANKS) {
        return;
    }

    layout->palettes[palette_bank_index] = *palette;
}

void virtuappu_mode0_set_gfx_data(const uint8_t *data, size_t size, size_t offset)
{
    Mode0Layout *layout = mode0_get_layout();

    if (data == NULL || offset + size > sizeof(layout->gfx_data)) {
        return;
    }

    memcpy(&layout->gfx_data[offset], data, size);
}

void virtuappu_mode0_set_tilemap_entry(size_t bg_index, size_t entry_index, Mode0TileEntry entry)
{
    Mode0Layout *layout = mode0_get_layout();

    if (bg_index >= MODE0_BG_COUNT || entry_index >= MODE0_TILEMAP_ENTRIES_PER_BG) {
        return;
    }

    layout->tilemaps[bg_index][entry_index] = entry;
}

void virtuappu_mode0_set_bg_entry(size_t bg_index, const Mode0BgEntry *bg_entry)
{
    Mode0Layout *layout = mode0_get_layout();

    if (bg_entry == NULL || bg_index >= MODE0_BG_COUNT) {
        return;
    }

    layout->bg[bg_index] = *bg_entry;
}

void virtuappu_mode0_set_oam_entry(size_t oam_index, const Mode0OAMEntry *oam_entry)
{
    Mode0Layout *layout = mode0_get_layout();

    if (oam_entry == NULL || oam_index >= MODE0_OAM_COUNT) {
        return;
    }

    layout->oam[oam_index] = *oam_entry;
}

void virtuappu_mode0_set_ppu_regs(const Mode0PPURegs *regs)
{
    Mode0Layout *layout = mode0_get_layout();

    if (regs == NULL) {
        return;
    }

    layout->regs = *regs;
}

void virtuappu_mode0_set_bg_line_scroll(size_t bg_index, size_t line_index, const Mode0LineScroll *line_scroll)
{
    Mode0Layout *layout = mode0_get_layout();

    if (line_scroll == NULL || bg_index >= MODE0_BG_COUNT || line_index >= MODE0_MAX_LINES) {
        return;
    }

    layout->bg_line_scroll[bg_index][line_index] = *line_scroll;
}

void virtuappu_mode0_set_bg_line_affine_tx_ty(size_t bg_index, size_t line_index, const Mode0LineAffineTxTy *line_affine)
{
    Mode0Layout *layout = mode0_get_layout();

    if (line_affine == NULL || bg_index >= MODE0_BG_COUNT || line_index >= MODE0_MAX_LINES) {
        return;
    }

    layout->bg_line_affine[bg_index][line_index] = *line_affine;
}

static void mode0_get_bg_32px(uint8_t bg_index, size_t line, size_t x_pixel_offset, uint32_t *out_pixels)
{
    Mode0Layout *layout = mode0_get_layout();
    int32_t sx;
    int32_t sy;
    size_t i;

    if (bg_index >= MODE0_BG_COUNT || out_pixels == NULL) {
        return;
    }

    sx = layout->bg[bg_index].scroll_x + layout->bg_line_scroll[bg_index][line].scroll_x;
    sy = layout->bg[bg_index].scroll_y + layout->bg_line_scroll[bg_index][line].scroll_y;
    (void)sx;
    (void)sy;
    (void)x_pixel_offset;

    for (i = 0; i < 32u; ++i) {
        out_pixels[i] = 0u;
    }
}

static void mode0_render_bg(uint8_t index, uint64_t *opaque_mask, uint32_t *scanline_layers, const PPUMemory *ppu, size_t line)
{
    const size_t width = (size_t)ppu->frame_width;
    const size_t block_count = (width + 31u) / 32u;
    const size_t layer_base = (size_t)index * width;
    size_t block;

    for (block = 0; block < block_count; ++block) {
        uint64_t bit = (block < 64u) ? ((uint64_t)1u << block) : 0u;
        size_t x0 = block * 32u;
        uint32_t *dst = &scanline_layers[layer_base + x0];
        size_t count = (x0 + 32u <= width) ? 32u : (width - x0);
        uint32_t any_not_opaque = 0u;
        size_t i;

        if (block < 64u && ((*opaque_mask & bit) != 0u)) {
            continue;
        }

        mode0_get_bg_32px(index, line, x0, dst);

        for (i = 0; i < count; ++i) {
            any_not_opaque |= (dst[i] & 0xFF000000u) ^ 0xFF000000u;
        }

        if (block < 64u && any_not_opaque == 0u) {
            *opaque_mask |= bit;
        }
    }
}

static void mode0_composite_and_oam(const uint32_t *scanline_layers, const PPUMemory *ppu, size_t line)
{
    const size_t width = (size_t)ppu->frame_width;
    const size_t pitch = (ppu->frame_pitch != 0u && ppu->frame_pitch >= ppu->frame_width)
                             ? (size_t)ppu->frame_pitch
                             : width;
    const uint32_t *bg0 = &scanline_layers[0];
    size_t x;

    for (x = 0; x < width; ++x) {
        virtuappu_frame_buffer[line * pitch + x] = bg0[x];
    }
}

void virtuappu_mode0_render_frame(const PPUMemory *ppu)
{
    size_t width;
    size_t padded_width;
    int line;

    if (ppu == NULL || ppu->frame_width == 0u || ppu->frame_width > VIRTUAPPU_MAX_FRAME_WIDTH) {
        return;
    }

    width = (size_t)ppu->frame_width;
    if (ppu->frame_pitch != 0u &&
        (ppu->frame_pitch < ppu->frame_width || ppu->frame_pitch > VIRTUAPPU_MAX_FRAME_WIDTH)) {
        return;
    }
    padded_width = (width + 31u) & ~31u;

#ifdef USE_OPENMP
#pragma omp parallel for
#endif
    for (line = 0; line < MODE0_MAX_LINES; ++line) {
        uint32_t scanline_layers[MODE0_BG_COUNT * (VIRTUAPPU_MAX_FRAME_WIDTH + 31u)];
        uint64_t opaque_mask = 0u;
        uint8_t bg_index;
        size_t scanline = (size_t)line;

        memset(scanline_layers, 0, sizeof(uint32_t) * MODE0_BG_COUNT * padded_width);

        for (bg_index = 0; bg_index < MODE0_BG_COUNT; ++bg_index) {
            mode0_render_bg(bg_index, &opaque_mask, scanline_layers, ppu, scanline);
        }

        mode0_composite_and_oam(scanline_layers, ppu, scanline);
    }
}
