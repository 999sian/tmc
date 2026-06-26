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

#include "cpu/mode7.h"

#include <stddef.h>

#include "virtuappu.h"

typedef struct Mode7SpriteCandidate {
    uint8_t x;
    uint8_t tile;
    uint8_t attributes;
    uint8_t line;
    uint8_t index;
} Mode7SpriteCandidate;

_Static_assert(sizeof(Mode7Layout) <= VIRTUAPPU_VRAM_SIZE, "Mode7Layout exceeds VRAM storage");

static const Mode7Layout *mode7_get_layout(void)
{
    return (const Mode7Layout *)virtuappu_vram;
}

static uint8_t mode7_vram_read(const Mode7Layout *layout, uint16_t addr)
{
    if (addr < 0x8000u || addr >= 0xA000u) {
        return 0u;
    }

    return layout->vram[addr - 0x8000u];
}

static uint32_t mode7_palette_color(uint8_t palette, uint8_t color_id)
{
    static const uint32_t dmg_palette[4] = {
        0xFF9BBC0Fu,
        0xFF8BAC0Fu,
        0xFF306230u,
        0xFF0F380Fu
    };
    uint8_t shade = (uint8_t)((palette >> (color_id * 2u)) & 0x03u);

    return dmg_palette[shade];
}

static uint8_t mode7_fetch_tile_color(
    const Mode7Layout *layout,
    uint16_t tile_map_base,
    uint16_t tile_data_base,
    int signed_indexing,
    uint8_t x,
    uint8_t y)
{
    uint8_t tile_x = (uint8_t)(x / 8u);
    uint8_t tile_y = (uint8_t)(y / 8u);
    uint16_t map_index = (uint16_t)(tile_y * 32u + tile_x);
    uint16_t map_addr = (uint16_t)(tile_map_base + map_index);
    uint8_t tile_index = mode7_vram_read(layout, map_addr);
    int32_t tile_id = signed_indexing ? (int8_t)tile_index : tile_index;
    uint16_t tile_addr = (uint16_t)(tile_data_base + tile_id * 16);
    uint8_t row = (uint8_t)(y % 8u);
    uint16_t tile_row_addr = (uint16_t)(tile_addr + row * 2u);
    uint8_t low = mode7_vram_read(layout, tile_row_addr);
    uint8_t high = mode7_vram_read(layout, (uint16_t)(tile_row_addr + 1u));
    uint8_t col = (uint8_t)(x % 8u);
    uint8_t bit = (uint8_t)(7u - col);

    return (uint8_t)((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
}

static uint8_t mode7_eval_sprites(
    const Mode7Layout *layout,
    uint8_t ly,
    uint8_t sprite_height,
    Mode7SpriteCandidate *out_sprites)
{
    Mode7SpriteCandidate candidates[40];
    uint8_t candidate_count = 0u;
    uint8_t i;

    for (i = 0u; i < 40u; ++i) {
        uint8_t y = layout->oam[i * 4u];
        uint8_t x = layout->oam[i * 4u + 1u];
        uint8_t tile = layout->oam[i * 4u + 2u];
        uint8_t attr = layout->oam[i * 4u + 3u];
        int sprite_y = (int)y - 16;
        Mode7SpriteCandidate candidate;
        int j;

        if ((int)ly < sprite_y || (int)ly >= sprite_y + sprite_height) {
            continue;
        }
        if (x == 0u || x >= 168u) {
            continue;
        }

        candidate.x = x;
        candidate.tile = tile;
        candidate.attributes = attr;
        candidate.line = (uint8_t)(ly - sprite_y);
        if ((attr & 0x40u) != 0u) {
            candidate.line = (uint8_t)((sprite_height - 1u) - candidate.line);
        }
        candidate.index = i;
        candidates[candidate_count++] = candidate;

        for (j = (int)candidate_count - 1; j > 0; --j) {
            Mode7SpriteCandidate current = candidates[j];
            Mode7SpriteCandidate previous = candidates[j - 1];
            if (previous.x < current.x || (previous.x == current.x && previous.index < current.index)) {
                break;
            }
            candidates[j] = previous;
            candidates[j - 1] = current;
        }
    }

    if (candidate_count > 10u) {
        candidate_count = 10u;
    }

    for (i = 0u; i < candidate_count; ++i) {
        out_sprites[i] = candidates[i];
    }

    return candidate_count;
}

void virtuappu_mode7_render_frame(const PPUMemory *ppu)
{
    const Mode7Layout *layout = mode7_get_layout();
    const Mode7GBRegs *regs = &layout->regs;
    uint8_t y;
    const size_t pitch =
        (ppu != NULL && ppu->frame_pitch >= MODE7_GB_SCREEN_WIDTH)
            ? (size_t)ppu->frame_pitch
            : (size_t)MODE7_GB_SCREEN_WIDTH;

    if ((regs->lcdc & MODE7_LCDC_ENABLE) == 0u) {
        uint32_t clear_color = mode7_palette_color(regs->bgp, 0u);
        for (y = 0u; y < MODE7_GB_SCREEN_HEIGHT; ++y) {
            for (uint8_t x = 0u; x < MODE7_GB_SCREEN_WIDTH; ++x) {
                virtuappu_frame_buffer[(size_t)y * pitch + x] = clear_color;
            }
        }
        return;
    }

    for (y = 0u; y < MODE7_GB_SCREEN_HEIGHT; ++y) {
        Mode7SpriteCandidate sprites[10];
        uint8_t sprite_count = 0u;
        uint8_t x;

        if ((regs->lcdc & MODE7_LCDC_OBJ_ENABLE) != 0u) {
            uint8_t sprite_height = (regs->lcdc & MODE7_LCDC_OBJ_SIZE) ? 16u : 8u;
            sprite_count = mode7_eval_sprites(layout, y, sprite_height, sprites);
        }

        for (x = 0u; x < MODE7_GB_SCREEN_WIDTH; ++x) {
            uint8_t bg_color_id = 0u;
            uint32_t bg_color = mode7_palette_color(regs->bgp, 0u);
            uint32_t final_color;

            if ((regs->lcdc & MODE7_LCDC_BG_ENABLE) != 0u) {
                uint16_t tile_map_base = (regs->lcdc & MODE7_LCDC_BG_TILE_MAP) ? 0x9C00u : 0x9800u;
                uint16_t tile_data_base = (regs->lcdc & MODE7_LCDC_BG_WINDOW_TILE_DATA) ? 0x8000u : 0x9000u;
                int signed_indexing = (regs->lcdc & MODE7_LCDC_BG_WINDOW_TILE_DATA) == 0u;
                uint8_t bg_x = (uint8_t)(x + regs->scx);
                uint8_t bg_y = (uint8_t)(y + regs->scy);

                bg_color_id = mode7_fetch_tile_color(layout, tile_map_base, tile_data_base, signed_indexing, bg_x, bg_y);

                if ((regs->lcdc & MODE7_LCDC_WINDOW_ENABLE) != 0u && regs->wy <= y) {
                    uint8_t window_x_origin = (regs->wx > 7u) ? (uint8_t)(regs->wx - 7u) : 0u;
                    if (x >= window_x_origin && regs->wx <= 166u) {
                        uint8_t window_x = (uint8_t)(x - window_x_origin);
                        uint8_t window_y = (uint8_t)(y - regs->wy);
                        uint16_t window_map_base = (regs->lcdc & MODE7_LCDC_WINDOW_TILE_MAP) ? 0x9C00u : 0x9800u;
                        bg_color_id = mode7_fetch_tile_color(layout, window_map_base, tile_data_base, signed_indexing, window_x, window_y);
                    }
                }

                bg_color = mode7_palette_color(regs->bgp, bg_color_id);
            }

            final_color = bg_color;

            if ((regs->lcdc & MODE7_LCDC_OBJ_ENABLE) != 0u && sprite_count > 0u) {
                uint8_t sprite_height = (regs->lcdc & MODE7_LCDC_OBJ_SIZE) ? 16u : 8u;
                uint8_t i;

                for (i = 0u; i < sprite_count; ++i) {
                    int screen_x = (int)sprites[i].x - 8;
                    uint8_t pixel_x;
                    uint8_t attributes;
                    uint8_t tile_index;
                    uint8_t line_in_sprite;
                    uint16_t tile_addr;
                    uint16_t row_addr;
                    uint8_t low;
                    uint8_t high;
                    uint8_t bit;
                    uint8_t color_id;
                    uint8_t palette;

                    if (x < screen_x || x >= screen_x + 8) {
                        continue;
                    }

                    pixel_x = (uint8_t)(x - screen_x);
                    attributes = sprites[i].attributes;
                    if ((attributes & 0x20u) != 0u) {
                        pixel_x = (uint8_t)(7u - pixel_x);
                    }

                    tile_index = sprites[i].tile;
                    line_in_sprite = sprites[i].line;
                    if (sprite_height == 16u) {
                        tile_index = (uint8_t)((tile_index & 0xFEu) | (line_in_sprite >= 8u));
                        line_in_sprite &= 0x07u;
                    }

                    tile_addr = (uint16_t)(0x8000u + tile_index * 16u);
                    row_addr = (uint16_t)(tile_addr + line_in_sprite * 2u);
                    low = mode7_vram_read(layout, row_addr);
                    high = mode7_vram_read(layout, (uint16_t)(row_addr + 1u));
                    bit = (uint8_t)(7u - pixel_x);
                    color_id = (uint8_t)((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));

                    if (color_id == 0u) {
                        continue;
                    }

                    palette = (attributes & 0x10u) ? regs->obp1 : regs->obp0;
                    if ((attributes & 0x80u) != 0u && bg_color_id != 0u) {
                        final_color = bg_color;
                    } else {
                        final_color = mode7_palette_color(palette, color_id);
                    }
                    break;
                }
            }

            virtuappu_frame_buffer[(size_t)y * pitch + x] = final_color;
        }
    }
}
