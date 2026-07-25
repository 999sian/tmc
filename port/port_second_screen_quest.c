#include "port_second_screen_quest.h"

#include "item.h"
#include "itemMetaData.h"
#include "port_rom.h"
#include "port_second_screen_render.h"
#include "port_second_screen_theme.h"
#include "region.h"

#include <string.h>

/*
 * The pause menu's QUEST STATUS screen, rebuilt from ROM so the panel's tab
 * is the screen pressing START shows rather than a paraphrase of it.
 *
 * Which screen, exactly: PauseMenuScreen_2 (src/menu/pauseMenu.c's
 * PauseMenu2 — the one src/menu/pauseMenu.c:123 calls "the QUEST STATUS
 * screen"). gUnk_08128A38[2].unk0 = 1 selects gUnk_08128AD8[1] =
 * { paletteGroup 183, gfxGroup 91, dispcnt 0x400, bg1Control 0x1C05,
 * bg2Control 0x1D03 } (src/data/figurineMenuData.c), which sub_080A4DB8
 * applies after sub_080A4D34 has loaded the shared pause chrome. That makes
 * the screen, in the order the GBA composes it:
 *
 *   BG3  parchment backdrop: gfx group 86 tiles @ 0x06008000 (charBase 2)
 *        plus group 86's EWRAM tilemap. NOT rebuilt here — the panel already
 *        paints the same art through Port_SecondScreenTheme_DrawBackdrop,
 *        which stamps the doodle lattice continuously instead of restarting
 *        it at every screen edge.
 *   BG2  the carved slab with the quest wells: group 91 tiles @ 0x06000000
 *        (charBase 0) plus group 91's EWRAM tilemap copy (gBG2Buffer).
 *   OBJ  sub_080A5128 draws the title banner and the tab arrows (sprite
 *        SPRITE_PAUSE_MISC frames 0/1/2); sub_080A57F4 draws one frame per
 *        occupied slot of gUnk_08128C94 (gUnk_08128C14 on JP), the slots
 *        being filled by sub_080A5594.
 *   BG1  off on this screen (dispcnt bit 9 clear); BG0 is the menu's live
 *        text layer, which a passive panel has nothing to put in.
 *
 *   Palettes load 11, 12 (LoadGfxGroups), 181 (sub_080A4D34), 183
 *   (sub_080A4DB8) — later loads win, like the shared gPaletteBuffer.
 *
 * Static vs live. Everything that does not depend on the save — slab, banner,
 * arrows, and the two always-present SLEEP / SAVE plates (sub_080A5594 fills
 * their slots through gGenericMenu.unk14/unk15, which alias slots 4 and 5) —
 * composes ONCE into a private 240x160 RGBA layer that is published only when
 * complete. Per call that layer is copied, the save-dependent slots are
 * stamped on top at their own table positions, and the result is scaled into
 * the caller's rect.
 *
 * What the snapshot can fill, and what it cannot. sub_080A5594 fills sixteen
 * slots; the published snapshot carries only two of those quantities:
 *   slot 0     kinstone bag    <- kinstoneBag / kinstoneFused
 *   slots 9-12 four elements   <- elements, placed through the same
 *                                gItemMetaData[item].menuSlot the menu uses
 * The rest (heart pieces, sword-technique count, shells / Carlov medal, the
 * three carried quest items, grip ring / bracelets / flippers) are not in
 * SecondScreenSnapshot and are not derivable from what is, so their wells
 * stay empty — which is exactly how the real screen looks before those are
 * collected. They are omitted rather than guessed on purpose: this module
 * reads ROM constants and its parameters only, never live engine state.
 *
 * Also deliberately absent: the blinking slot cursor. It marks the pause
 * menu's selection, and a panel that cannot be navigated has none.
 *
 * Threading: the layer is built lazily by whoever draws first — by design
 * only the second-screen render thread — into a private buffer that is
 * published through one pointer-sized store once complete, and immutable
 * afterwards. Same contract (and same caveat about adding a second reader
 * thread) as port_second_screen_worldmap.c's image.
 */

/* src/common.c (appended accessors — ROM-const reads only), the same faces
 * the other second-screen art modules decode through. */
extern const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail);
extern const u8* Port_GetRawPaletteGroupEntryData(u32 group, u32 entryIdx, u32* outNumColors,
                                                  u32* outDestPaletteNum);

/* src/affine.c — frame OBJ piece list for (sprite, frame). */
extern void* sub_080AD8F0(u32 sprite, u32 frame);

/* Resolved ROM group table (port_rom.c) — for the EWRAM-destined tilemap
 * record, which Port_ResolveGfxGroupVram's VRAM remit does not cover. */
extern const void* gGfxGroups[];
extern const u8* gGlobalGfxAndPalettes;

/* Quest-screen slot table (data/const/subtask.s): 16 entries of
 * { up, down, left, right, cursorFrameBase, itemFrameBase, x, y }.
 * gUnk_08128C14 is the language-0 table and gUnk_08128C94 every other
 * language's; sub_080A57F4 picks between them on gSaveHeader->language, which
 * is save state — the ROM-const equivalent is the region, since language 0
 * only exists on the JP cart. */
extern const u8 gUnk_08128C14[];
extern const u8 gUnk_08128C94[];

#define QUEST_W 240 /* GBA LCD: menu screens always compose the native canvas */
#define QUEST_H 160

#define QUEST_GFX_GROUP 91u          /* pause screen 2's own tiles + tilemap */
#define QUEST_TILES_DEST 0x06000000u /* bg2Control 0x1D03 -> charBase 0 */
#define OBJ_VRAM_BASE 0x06010000u

/* Palette groups applied on the way into the quest screen, in load order. */
static const u8 kQuestPaletteGroups[] = { 11u, 12u, 181u, 183u };
/* OBJ VRAM state at that point, latest load first. */
static const u8 kQuestVramGroups[] = { 91u, 86u, 23u, 16u };

#define SPRITE_PAUSE_MISC (REGION_IS_EU ? 0x1FAu : 0x1FBu)

/* sub_080A5128's fixed draws for a normal screen (its `default` arm): the
 * title banner at (0x40, 0x10) and the tab arrows at (0x10, 0x48) and
 * (0xE0, 0x48), all with gOamCmd._8 = 0x400. */
#define BANNER_X 0x40
#define BANNER_Y 0x10
#define ARROW_L_X 0x10
#define ARROW_R_X 0xE0
#define ARROW_Y 0x48
#define CHROME_OAM_EXTRA 0x400u

/* sub_080A57F4's slot draws: gOamCmd._8 = 0xE800 for the frames that come
 * straight out of OBJ VRAM (the counters and the two button plates). */
#define SLOT_OAM_EXTRA 0xE800u

#define SLOT_KINSTONE_BAG 0
#define SLOT_SLEEP 4
#define SLOT_SAVE 5
#define SLOT_BUTTON_VALUE 1 /* both button slots hold 1 */
#define QUEST_SLOT_COUNT 16

/* Frame id for a slot's contents, per sub_080A57F4: value + 9 + unk5. */
#define SLOT_FRAME(entry, value) ((u32)(value) + 9u + (u32)(entry)[5])

/* The icon renderer maps a 16x16 box origin onto the game's OAM command
 * position: the body piece sits at (-8, -13) of it. */
#define ICON_BOX_DX 8
#define ICON_BOX_DY 13

/* Standard GBA OBJ dimensions by (shape, size) — hardware constants. */
static const u8 kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static uint32_t sStatic[QUEST_W * QUEST_H];
static uint32_t sFrame[QUEST_W * QUEST_H];
static const uint32_t* volatile sPublished = NULL;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static const u8* SlotEntry(int slot) {
    const u8* table = REGION_IS_JP ? gUnk_08128C14 : gUnk_08128C94;
    if (table == NULL || slot < 0 || slot >= QUEST_SLOT_COUNT) {
        return NULL;
    }
    return table + (size_t)slot * 8u;
}

/* First EWRAM-destined record of a gfx group — the menu screens stage their
 * BG tilemaps through buffers whose absolute address is a per-region link
 * detail, so the record is matched by destination region (the same walk
 * port_second_screen_worldmap.c does for the map screen). */
static const u8* QuestTilemap(u32 group, u32* outLen) {
    const u8* rec;
    int i;

    *outLen = 0;
    if (group >= 133u || gGlobalGfxAndPalettes == NULL) { /* GFX_GROUPS_COUNT_MAX */
        return NULL;
    }
    rec = (const u8*)gGfxGroups[group];
    if (rec == NULL) {
        return NULL;
    }
    for (i = 0; i < 32; i++, rec += 12) {
        u32 raw0 = Port_ReadU32(rec);
        u32 dest = Port_ReadU32(rec + 4);
        s32 size = (s32)Port_ReadU32(rec + 8);
        if (((raw0 >> 24) & 0xFu) == 0xDu) {
            return NULL;
        }
        if ((dest >> 24) == 0x02u && size > 0) {
            *outLen = (u32)size;
            return gGlobalGfxAndPalettes + (raw0 & 0xFFFFFFu);
        }
        if (((raw0 >> 24) & 0x80u) == 0) {
            return NULL;
        }
    }
    return NULL;
}

/* LoadPaletteGroup's effect on the BG half of gPaletteBuffer. */
static void ApplyPaletteGroup(u32 group, uint16_t* bgPal) {
    u32 e;
    for (e = 0; e < 8; e++) {
        u32 numColors = 0, destBank = 0, c;
        const u8* p = Port_GetRawPaletteGroupEntryData(group, e, &numColors, &destBank);
        if (p == NULL) {
            return;
        }
        if (destBank >= 16) {
            continue; /* OBJ bank — the BG layer only wants the BG half */
        }
        for (c = 0; c < numColors && destBank * 16u + c < 256u; c++) {
            bgPal[destBank * 16u + c] = (uint16_t)(p[c * 2] | (p[c * 2 + 1] << 8));
        }
    }
}

/* Paints the 32x20 BG2 tilemap into the layer. Color 0 stays transparent, so
 * the caller's backdrop shows through the slab's rounded outline. */
static void DrawBgLayer(const u8* tilemap, u32 mapLen, const u8* tiles, u32 tilesLen,
                        const uint16_t* bgPal) {
    u32 tileCount = tilesLen / 32u;
    int32_t x, y;
    for (y = 0; y < QUEST_H; y++) {
        u32 tileRow = ((u32)y >> 3) & 31u;
        for (x = 0; x < QUEST_W; x++) {
            u32 tileCol = ((u32)x >> 3) & 31u;
            u32 entryOff = (tileRow * 32u + tileCol) * 2u;
            u16 entry = (entryOff + 2u <= mapLen) ? Port_ReadU16(tilemap + entryOff) : 0;
            u32 tileId = entry & 0x3FFu;
            int32_t inX = x & 7, inY = y & 7;
            u8 packed, colorIndex;
            if (tileId >= tileCount) {
                continue;
            }
            if (entry & 0x400u) inX = 7 - inX;
            if (entry & 0x800u) inY = 7 - inY;
            packed = tiles[tileId * 32u + (u32)inY * 4u + ((u32)inX >> 1)];
            colorIndex = (inX & 1) ? (u8)(packed >> 4) : (u8)(packed & 0xFu);
            if (colorIndex == 0) {
                continue;
            }
            sStatic[(size_t)y * QUEST_W + (size_t)x] =
                Rgb555ToRgba8888(bgPal[(((u32)entry >> 12) & 0xFu) * 16u + colorIndex]);
        }
    }
}

/* One DrawDirect frame, tiles resolved out of the gfx groups the screen keeps
 * loaded. Piece format and attr2 math per RenderSpritePieces (port_draw.c);
 * pieces are drawn in reverse so the first (topmost OAM) wins overlaps. */
static void DrawObjFrame(uint32_t* layer, u32 sprite, u32 frame, int32_t cmdX, int32_t cmdY,
                         u32 oamExtra) {
    const u8* frameData = (const u8*)sub_080AD8F0(sprite, frame);
    u32 count, baseTile = oamExtra & 0x3FFu, basePal = (oamExtra >> 12) & 0xFu;
    int32_t i;

    if (frameData == NULL) {
        return;
    }
    count = frameData[0];
    if (count == 0 || count > 16) {
        return;
    }
    for (i = (int32_t)count - 1; i >= 0; i--) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3u, size = (p[2] >> 4) & 3u;
        int hflip = (p[2] & 0x04u) != 0, vflip = (p[2] & 0x08u) != 0;
        u32 tileIdx = baseTile + (u32)p[3] + (((u32)p[4] & 3u) << 8);
        u32 palBank = ((((p[2] & 1u) ? 0u : basePal)) + ((u32)p[4] >> 4)) & 15u;
        const uint16_t* pal = Port_SecondScreenTheme_ObjPalette(palBank);
        const u8* tiles = NULL;
        int32_t pw, ph, wTiles, hTiles, tx, ty, yy, xx;
        u32 g, avail = 0;

        if (shape == 3 || pal == NULL) {
            continue;
        }
        pw = kObjW[shape][size];
        ph = kObjH[shape][size];
        wTiles = pw / 8;
        hTiles = ph / 8;
        for (g = 0; g < sizeof(kQuestVramGroups) && tiles == NULL; g++) {
            tiles = Port_ResolveGfxGroupVram(kQuestVramGroups[g], OBJ_VRAM_BASE + tileIdx * 32u, &avail);
            if (tiles != NULL && avail < (u32)(wTiles * hTiles) * 32u) {
                tiles = NULL;
            }
        }
        if (tiles == NULL) {
            continue;
        }
        for (ty = 0; ty < hTiles; ty++) {
            for (tx = 0; tx < wTiles; tx++) {
                const u8* tile = tiles + (ty * wTiles + tx) * 32;
                for (yy = 0; yy < 8; yy++) {
                    for (xx = 0; xx < 8; xx++) {
                        u8 packed = tile[yy * 4 + xx / 2];
                        u8 idx = (xx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                        int32_t dx = tx * 8 + xx, dy = ty * 8 + yy;
                        if (idx == 0) {
                            continue;
                        }
                        if (hflip) dx = pw - 1 - dx;
                        if (vflip) dy = ph - 1 - dy;
                        dx += cmdX + (int32_t)(int8_t)p[0];
                        dy += cmdY + (int32_t)(int8_t)p[1];
                        if (dx < 0 || dy < 0 || dx >= QUEST_W || dy >= QUEST_H) {
                            continue;
                        }
                        layer[(size_t)dy * QUEST_W + (size_t)dx] = Rgb555ToRgba8888(pal[idx]);
                    }
                }
            }
        }
    }
}

/* Composes the save-independent layer. Returns 1 when the whole screen
 * decoded; a partial result is never published, so callers simply retry. */
static int BuildStaticLayer(void) {
    uint16_t bgPal[16 * 16];
    u32 tilesLen = 0, mapLen = 0;
    const u8* tiles = Port_ResolveGfxGroupVram(QUEST_GFX_GROUP, QUEST_TILES_DEST, &tilesLen);
    const u8* tilemap = QuestTilemap(QUEST_GFX_GROUP, &mapLen);
    const u8* sleepEntry = SlotEntry(SLOT_SLEEP);
    const u8* saveEntry = SlotEntry(SLOT_SAVE);
    size_t i;
    int32_t opaque = 0;

    if (tiles == NULL || tilemap == NULL || tilesLen < 32 || mapLen < 64 || sleepEntry == NULL ||
        saveEntry == NULL) {
        return 0;
    }

    memset(bgPal, 0, sizeof(bgPal));
    for (i = 0; i < sizeof(kQuestPaletteGroups); i++) {
        ApplyPaletteGroup(kQuestPaletteGroups[i], bgPal);
    }

    memset(sStatic, 0, sizeof(sStatic));
    DrawBgLayer(tilemap, mapLen, tiles, tilesLen, bgPal);

    /* sub_080A5128's chrome, then the two button plates. */
    DrawObjFrame(sStatic, SPRITE_PAUSE_MISC, 0, BANNER_X, BANNER_Y, CHROME_OAM_EXTRA);
    DrawObjFrame(sStatic, SPRITE_PAUSE_MISC, 1, ARROW_L_X, ARROW_Y, CHROME_OAM_EXTRA);
    DrawObjFrame(sStatic, SPRITE_PAUSE_MISC, 2, ARROW_R_X, ARROW_Y, CHROME_OAM_EXTRA);
    DrawObjFrame(sStatic, SPRITE_PAUSE_MISC, SLOT_FRAME(sleepEntry, SLOT_BUTTON_VALUE), sleepEntry[6],
                 sleepEntry[7], SLOT_OAM_EXTRA);
    DrawObjFrame(sStatic, SPRITE_PAUSE_MISC, SLOT_FRAME(saveEntry, SLOT_BUTTON_VALUE), saveEntry[6],
                 saveEntry[7], SLOT_OAM_EXTRA);

    /* Sanity: the slab covers most of the canvas. A near-empty layer means a
     * group record moved and the decode silently produced nothing — better to
     * leave the caller on its fallback than to publish that. */
    for (i = 0; i < (size_t)(QUEST_W * QUEST_H); i++) {
        if ((sStatic[i] >> 24) != 0) {
            opaque++;
        }
    }
    return opaque >= QUEST_W * QUEST_H / 4;
}

/* Kinstone bag tier, the ladder sub_080A5594 walks over the bag's contents.
 * Owning the bag is what puts anything in the slot at all; the snapshot has
 * no "owns bag" flag, so any piece held or fused stands in for it — the same
 * evidence the player already has on screen. */
static u32 KinstoneBagTier(const SecondScreenSnapshot* snap) {
    if (snap->kinstoneBag == 0 && snap->kinstoneFused == 0) {
        return 0;
    }
    if (snap->kinstoneBag >= 0x50) {
        return 4;
    }
    if (snap->kinstoneBag >= 0x28) {
        return 3;
    }
    if (snap->kinstoneBag >= 10) {
        return 2;
    }
    return 1;
}

/* Stamps the save-dependent slots onto the working copy of the layer. */
static void DrawLiveSlots(const SecondScreenSnapshot* snap) {
    u32 tier = KinstoneBagTier(snap);
    u32 i;

    if (tier != 0) {
        const u8* entry = SlotEntry(SLOT_KINSTONE_BAG);
        if (entry != NULL) {
            DrawObjFrame(sFrame, SPRITE_PAUSE_MISC, SLOT_FRAME(entry, tier), entry[6], entry[7],
                         SLOT_OAM_EXTRA);
        }
    }

    /* The four elements, placed the way sub_080A5594 places every quest item:
     * gItemMetaData[item].menuSlot picks the well, and the icon is the item's
     * own sprite-322 frame (the >= 0x34 arm of sub_080A57F4). */
    for (i = 0; i < 4; i++) {
        u32 item = (u32)ITEM_EARTH_ELEMENT + i;
        u32 slot;
        const u8* entry;
        if ((snap->elements & (1u << i)) == 0) {
            continue;
        }
        slot = gItemMetaData[item].menuSlot;
        entry = SlotEntry((int)slot);
        if (entry == NULL) {
            continue;
        }
        Port_SecondScreenRender_DrawItemIcon(sFrame, QUEST_W, QUEST_H, QUEST_W,
                                             (int32_t)entry[6] - ICON_BOX_DX,
                                             (int32_t)entry[7] - ICON_BOX_DY, 1, (uint8_t)item);
    }
}

int Port_SecondScreenQuest_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                const SecondScreenSnapshot* snap, uint32_t tick) {
    int32_t scale, drawW, drawH, originX, originY, x, y;

    (void)tick; /* the screen's only animation is its selection cursor */

    if (pixels == NULL || snap == NULL || dstW <= 0 || dstH <= 0) {
        return 0;
    }
    if (!Port_SecondScreenTheme_Ready()) {
        return 0; /* the parchment under the slab comes from the theme */
    }
    if (sPublished == NULL) {
        if (!BuildStaticLayer()) {
            return 0; /* ROM tables not resolved yet — retry next frame */
        }
        sPublished = sStatic;
    }

    /* Integer scale only: the slab's rim, the plate keylines and the counter
     * glyphs are all one art pixel wide, and a fractional nearest-neighbor
     * step drops whole rows of them. */
    scale = dstW / QUEST_W;
    if (dstH / QUEST_H < scale) {
        scale = dstH / QUEST_H;
    }
    if (scale < 1) {
        scale = 1;
    }
    drawW = QUEST_W * scale;
    drawH = QUEST_H * scale;
    originX = dstX + (dstW - drawW) / 2;
    originY = dstY + (dstH - drawH) / 2;

    /* The parchment the screen sits on, over the whole rect rather than just
     * behind the slab, so the doodle lattice runs unbroken across the panel. */
    Port_SecondScreenTheme_DrawBackdrop(pixels, bufW, bufH, stride, dstX, dstY, dstX + dstW,
                                        dstY + dstH, scale);

    memcpy(sFrame, sPublished, sizeof(sFrame));
    DrawLiveSlots(snap);

    for (y = 0; y < drawH; y++) {
        int32_t py = originY + y;
        const uint32_t* row = sFrame + (size_t)(y / scale) * QUEST_W;
        if (py < 0 || py >= bufH) {
            continue;
        }
        for (x = 0; x < drawW; x++) {
            int32_t px = originX + x;
            uint32_t c = row[x / scale];
            if ((c >> 24) == 0 || px < 0 || px >= bufW) {
                continue;
            }
            pixels[(size_t)py * (size_t)stride + (size_t)px] = c;
        }
    }
    return 1;
}
