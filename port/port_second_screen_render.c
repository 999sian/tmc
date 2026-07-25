#include "port_second_screen_render.h"

#include "port_gba_mem.h"
#include "port_rom.h"
#include "sprite.h"
#include "structures.h"

#include <stdio.h>

/* Defined in src/common.c, next to LoadPaletteGroup — needs the file-private
 * gPaletteGroups/PaletteGroup ROM table, so it lives there rather than here.
 * Returns raw RGB555 bytes for a palette group straight from ROM/asset data,
 * independent of whatever's currently sitting in the live, shared
 * gPaletteBuffer (pause-menu screens reload/repurpose those banks
 * constantly for unrelated UI, so reading gPaletteBuffer directly would show
 * stale/wrong colors depending on what the main screen last did). */
extern const uint8_t* Port_GetRawPaletteGroupData(uint32_t group, uint32_t* outNumColors);

/* Mirrors src/menu/pauseMenu.c's GetSpriteAnimation322 (static there, so not
 * directly callable) — gSpriteAnimations_322 is a plain linked global. */
extern Frame* gSpriteAnimations_322[];
#define SPRITE_ANIM_322_COUNT 128

/* Palette group 182 — the main "Items" equip-select screen's icon palette
 * (destPaletteNum 3, per src/menu/pauseMenu.c:567's `gOamCmd._8 = 0x800 |
 * color << 0xc | ...` with color=3). That's the icon coloring players see
 * most often, so it's the natural default for a panel that isn't tied to
 * any particular pause-menu tab being open. */
#define ICON_PALETTE_GROUP 182u

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

void Port_SecondScreenRender_DrawItemIcon(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, uint8_t itemId) {
    if (scale < 1) {
        scale = 1;
    }
    if (itemId == 0 || itemId >= SPRITE_ANIM_322_COUNT) {
        return;
    }

    Frame* animation = (Frame*)port_resolve_addr((uintptr_t)gSpriteAnimations_322[itemId]);
    if (animation == NULL) {
        return;
    }

    const SpritePtr* sprite = Port_GetSpritePtr(322); /* decimal, matches gSpriteAnimations_322's naming — NOT 0x322 */
    if (sprite == NULL || sprite->frames == NULL || sprite->ptr == NULL) {
        fprintf(stderr, "[second_screen_render] itemId %u: sprite=%p frames=%p ptr=%p\n", itemId, (void*)sprite,
                sprite ? (void*)sprite->frames : NULL, sprite ? sprite->ptr : NULL);
        return;
    }

    const SpriteFrame* frame = &sprite->frames[animation->index];

    uint32_t numColors = 0;
    const uint8_t* palette = Port_GetRawPaletteGroupData(ICON_PALETTE_GROUP, &numColors);
    if (palette == NULL) {
        fprintf(stderr, "[second_screen_render] itemId %u: no palette data (group %u)\n", itemId,
                ICON_PALETTE_GROUP);
        return;
    }
    const uint16_t* palette16 = (const uint16_t*)palette;

    /* Every icon in sprite sheet 322 is a 2x2 tile (16x16px) block. */
    const int32_t tilesPerSide = 2;
    const uint8_t* tileData = (const uint8_t*)sprite->ptr;

    for (int32_t ty = 0; ty < tilesPerSide; ty++) {
        for (int32_t tx = 0; tx < tilesPerSide; tx++) {
            uint32_t tileIndex = (uint32_t)frame->firstTileIndex + (uint32_t)(ty * tilesPerSide + tx);
            const uint8_t* tile = tileData + (size_t)tileIndex * 32u;
            for (int32_t py = 0; py < 8; py++) {
                for (int32_t px = 0; px < 8; px++) {
                    uint8_t packed = tile[py * 4 + px / 2];
                    uint8_t colorIndex = (px & 1) ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0Fu);
                    if (colorIndex == 0 || colorIndex >= numColors) {
                        continue; /* 0 == transparent */
                    }
                    uint32_t rgba = Rgb555ToRgba8888(palette16[colorIndex]);
                    int32_t srcX = tx * 8 + px;
                    int32_t srcY = ty * 8 + py;
                    /* Nearest-neighbor upscale: each source pixel becomes a
                     * scale x scale block, matching the GBA's own blocky
                     * look instead of smoothing a 16x16 icon into mush. */
                    for (int32_t sy = 0; sy < scale; sy++) {
                        int32_t destY = y + srcY * scale + sy;
                        if (destY < 0 || destY >= bufHeight) {
                            continue;
                        }
                        for (int32_t sx = 0; sx < scale; sx++) {
                            int32_t destX = x + srcX * scale + sx;
                            if (destX < 0 || destX >= bufWidth) {
                                continue;
                            }
                            pixels[(size_t)destY * (size_t)stride + (size_t)destX] = rgba;
                        }
                    }
                }
            }
        }
    }
}
