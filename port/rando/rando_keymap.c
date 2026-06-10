/*
 * Curated runtime keys for .logic ground-item locations (rando_keymap.h).
 *
 * MinishMaker's `.logic` file identifies dungeon ground items (DUNGEONRUPEE /
 * DUNGEONUNDERWATER / DUNGEONSMALLUNDERWATER / DUNGEONPOT / DUNGEONHP and
 * the FORTRESS- / PALACE-prefixed variants) by a precise EU-ROM patch
 * address instead of an area-room-index triple, so the native engine cannot
 * key them by itself.
 * This table maps each such location name to the runtime key the in-game
 * hook builds at pickup:
 *
 *     key = area << 16 | room << 8 | (ItemOnGroundEntity.flag & 0xff)
 *
 * (src/object/itemOnGround.c pickup hook; pot drops inherit the pot's flag
 * via sub_0808288C, src/object/pot.c.)
 *
 * Every row below was verified against repo data — no guesses:
 *  1. Room attribution: the USA baserom.gba room property tables (gAreaTable
 *     pointer array at ROM 0xD50FC, the same tables port_rom.c resolves at
 *     runtime; per-area table bounds from kAreaTableOffsets in
 *     port_rom_tables.c). Entity lists are room properties 0/1/2
 *     (EntityData[16] records, src/room.c LoadRoom); the flag is the
 *     spritePtr high half (RegisterRoomEntity, src/room.c).
 *  2. Address match: each dungeon's entity-data region has a constant EU->USA
 *     file-offset delta (DWS +0x8C4, CoF +0x8E4, ToD/PoW/FoW-HP +0x904,
 *     FoW 0xF3xxx +0x9DC). For every row, .logic EU address + delta lands
 *     exactly on the item-type byte (record offset + 3) of a GROUND_ITEM/POT
 *     record found in step 1, and where the .logic names a distinct vanilla
 *     item (Rupee50, Shells, SmallKey, Kinstone, PieceOfHeart) the record's
 *     item id matches it.
 *  3. (area, room) sanity: chest TileEntities (room property 3) of the same
 *     rooms reproduce the .logic chest keys' vanilla items (MoleMitts,
 *     Rupee100, DungeonMap, ...).
 *
 * Comments cite the USA EntityData record offset and the .logic EU address.
 */

#include "rando/rando_keymap.h"

#include "rando/rando_logic.h"

#include <stdint.h>
#include <stdio.h>

typedef struct RandoKeymapEntry {
    const char* location; /* .logic location name (build/default.logic) */
    uint8_t area;
    uint8_t room;
    uint8_t flag; /* ItemOnGroundEntity.flag low byte (room entity data) */
} RandoKeymapEntry;

static const RandoKeymapEntry kGroundItemKeys[] = {
    /* --- Deepwood Shrine (area 0x48, EU+0x8C4) --------------------------- */
    /* USA rec 0x0DE6C4: GI HeartPiece flag 0x4F; EU 0x0DDE03+0x8C4=0x0DE6C7=rec+3 */
    { "Deepwood_1F_BlueWarp_HP", 0x48, 0x01, 0x4F },
    /* USA rec 0x0DEAB8: GI HeartPiece flag 0x52; EU 0x0DE1F7+0x8C4=0x0DEABB=rec+3 */
    { "Deepwood_1F_Madderpillar_HP", 0x48, 0x05, 0x52 },

    /* --- Cave of Flames (area 0x50, EU+0x8E4) ---------------------------- */
    /* USA recs 0x0E03CC..0x0E040C: 5x GI Rupee1 flags 0x3E..0x42;
     * EU 0x0DFAEB..0x0DFB2B (+0x10 stride) +0x8E4 = rec+3 each */
    { "CoF_1F_Item1", 0x50, 0x05, 0x3E },
    { "CoF_1F_Item2", 0x50, 0x05, 0x3F },
    { "CoF_1F_Item3", 0x50, 0x05, 0x40 },
    { "CoF_1F_Item4", 0x50, 0x05, 0x41 },
    { "CoF_1F_Item5", 0x50, 0x05, 0x42 },
    /* USA rec 0x0E0580: GI HeartPiece flag 0x3C; EU 0x0DFC9F+0x8E4=0x0E0583=rec+3 */
    { "CoF_B1_HP", 0x50, 0x06, 0x3C },

    /* --- Fortress of Winds (areas 0x18/0x58) ----------------------------- */
    /* USA rec 0x0F4840: GI Rupee50 flag 0x4E (item matches .logic Rupee50);
     * EU 0x0F3E67+0x9DC=0x0F4843=rec+3 */
    { "Fortress_Entrance_1F_RightItem", 0x18, 0x00, 0x4E },
    /* USA recs 0x0F4910..0x0F4970: 7x GI rupee flags 0x55..0x5B;
     * EU 0x0F3F37..0x0F3F97 (+0x10 stride) +0x9DC = rec+3 each */
    { "Fortress_Left_2F_Item1", 0x18, 0x01, 0x55 },
    { "Fortress_Left_2F_Item2", 0x18, 0x01, 0x56 },
    { "Fortress_Left_2F_Item3", 0x18, 0x01, 0x57 },
    { "Fortress_Left_2F_Item4", 0x18, 0x01, 0x58 },
    { "Fortress_Left_2F_Item5", 0x18, 0x01, 0x59 },
    { "Fortress_Left_2F_Item6", 0x18, 0x01, 0x5A },
    { "Fortress_Left_2F_Item7", 0x18, 0x01, 0x5B },
    /* USA rec 0x0F49A0: POT Rupee50 drop-mode(type2&0xff==2) flag 0x53
     * (item matches .logic Rupee50); EU 0x0F3FC7+0x9DC=0x0F49A3=rec+3 */
    { "Fortress_BackRight_DigRoom_TopPot", 0x18, 0x01, 0x53 },
    /* USA rec 0x0F49B0: POT Shells drop-mode flag 0x54 (item matches .logic
     * Shells); EU 0x0F3FD7+0x9DC=0x0F49B3=rec+3 */
    { "Fortress_BackRight_DigRoom_BottomPot", 0x18, 0x01, 0x54 },
    /* USA rec 0x0E36D8: GI HeartPiece flag 0x47; EU 0x0E2DD7+0x904=0x0E36DB=rec+3 */
    { "Fortress_Entrance_1F_RightHP", 0x58, 0x24, 0x47 },

    /* --- Temple of Droplets (area 0x60, EU+0x904) ------------------------ */
    /* USA recs 0x0E3E94..0x0E3EE4: 6x GI Rupee5 (underwater subtype 8)
     * flags 0x95..0x9A; EU 0x0E3593..0x0E35E3 +0x904 = rec+3 each */
    { "Droplets_LeftPath_B1_Waterfall_Underwater1", 0x60, 0x06, 0x95 },
    { "Droplets_LeftPath_B1_Waterfall_Underwater2", 0x60, 0x06, 0x96 },
    { "Droplets_LeftPath_B1_Waterfall_Underwater3", 0x60, 0x06, 0x97 },
    { "Droplets_LeftPath_B1_Waterfall_Underwater4", 0x60, 0x06, 0x98 },
    { "Droplets_LeftPath_B1_Waterfall_Underwater5", 0x60, 0x06, 0x99 },
    { "Droplets_LeftPath_B1_Waterfall_Underwater6", 0x60, 0x06, 0x9A },
    /* USA recs 0x0E488C..0x0E48CC: 5x GI Rupee1 flags 0x85..0x89;
     * EU 0x0E3F8B..0x0E3FCB +0x904 = rec+3 each. Same room as the .logic
     * chest key 0x60-0x0D-0x00 (Waterfall BigChest, DungeonMap verified). */
    { "Droplets_LeftPath_B1_UnderpassItem1", 0x60, 0x0D, 0x85 },
    { "Droplets_LeftPath_B1_UnderpassItem2", 0x60, 0x0D, 0x86 },
    { "Droplets_LeftPath_B1_UnderpassItem3", 0x60, 0x0D, 0x87 },
    { "Droplets_LeftPath_B1_UnderpassItem4", 0x60, 0x0D, 0x88 },
    { "Droplets_LeftPath_B1_UnderpassItem5", 0x60, 0x0D, 0x89 },
    /* USA rec 0x0E4374: POT Kinstone drop-mode flag 0x39 (item matches
     * .logic Kinstone.GreenC); EU 0x0E3A73+0x904=0x0E4377=rec+3 */
    { "Droplets_RightPath_B1_Pot", 0x60, 0x0A, 0x39 },
    /* USA recs 0x0E5140..0x0E5180: 5x GI Rupee1 flags 0x8A..0x8E;
     * EU 0x0E483F..0x0E487F +0x904 = rec+3 each */
    { "Droplets_RightPath_B2_UnderpassItem1", 0x60, 0x25, 0x8A },
    { "Droplets_RightPath_B2_UnderpassItem2", 0x60, 0x25, 0x8B },
    { "Droplets_RightPath_B2_UnderpassItem3", 0x60, 0x25, 0x8C },
    { "Droplets_RightPath_B2_UnderpassItem4", 0x60, 0x25, 0x8D },
    { "Droplets_RightPath_B2_UnderpassItem5", 0x60, 0x25, 0x8E },
    /* USA recs 0x0E61E0..0x0E6230: 6x GI Rupee5 (underwater) flags 0x8F..0x94;
     * EU 0x0E58DF..0x0E592F +0x904 = rec+3 each */
    { "Droplets_LeftPath_B2_Waterfall_Underwater1", 0x60, 0x31, 0x8F },
    { "Droplets_LeftPath_B2_Waterfall_Underwater2", 0x60, 0x31, 0x90 },
    { "Droplets_LeftPath_B2_Waterfall_Underwater3", 0x60, 0x31, 0x91 },
    { "Droplets_LeftPath_B2_Waterfall_Underwater4", 0x60, 0x31, 0x92 },
    { "Droplets_LeftPath_B2_Waterfall_Underwater5", 0x60, 0x31, 0x93 },
    { "Droplets_LeftPath_B2_Waterfall_Underwater6", 0x60, 0x31, 0x94 },
    /* USA rec 0x0E64C8: GI SmallKey (underwater) flag 0x7A (item matches
     * .logic SmallKey.0x1B); EU 0x0E5BC7+0x904=0x0E64CB=rec+3 */
    { "Droplets_LeftPath_B2_Underwater_Pot", 0x60, 0x34, 0x7A },

    /* --- Palace of Winds (area 0x70, EU+0x904) --------------------------- */
    /* USA rec 0x0E80A8: GI HeartPiece flag 0x80; EU 0x0E77A7+0x904=0x0E80AB=rec+3 */
    { "Palace_2ndHalf_4F_HP", 0x70, 0x0F, 0x80 },
    /* USA recs 0x0E9420..0x0E9460: 5x GI Rupee1 flags 0x5A..0x5E;
     * EU 0x0E8B1F..0x0E8B5F +0x904 = rec+3 each */
    { "Palace_1stHalf_2F_Item1", 0x70, 0x21, 0x5A },
    { "Palace_1stHalf_2F_Item2", 0x70, 0x21, 0x5B },
    { "Palace_1stHalf_2F_Item3", 0x70, 0x21, 0x5C },
    { "Palace_1stHalf_2F_Item4", 0x70, 0x21, 0x5D },
    { "Palace_1stHalf_2F_Item5", 0x70, 0x21, 0x5E },
};

#define RANDO_KEYMAP_COUNT (sizeof(kGroundItemKeys) / sizeof(kGroundItemKeys[0]))

void Rando_Keymap_Apply(void) {
    unsigned bound = 0;
    if (!RandoLogic_IsLoaded()) {
        return;
    }
    for (unsigned i = 0; i < (unsigned)RANDO_KEYMAP_COUNT; ++i) {
        const RandoKeymapEntry* e = &kGroundItemKeys[i];
        uint32_t key = ((uint32_t)e->area << 16) | ((uint32_t)e->room << 8) | (uint32_t)e->flag;
        if (RandoLogic_BindRuntimeKey(e->location, key)) {
            ++bound;
        }
    }
    fprintf(stderr, "[RANDO] keymap: bound %u/%u ground-item locations\n", bound,
            (unsigned)RANDO_KEYMAP_COUNT);
}
