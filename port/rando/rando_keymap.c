/*
 * Curated runtime keys for .logic ground-item locations (rando_keymap.h).
 *
 * The `.logic` format identifies dungeon ground items (DUNGEONRUPEE /
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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct RandoKeymapEntry {
    const char* location; /* .logic location name (build/default.logic) */
    uint8_t area;
    uint8_t room;
    uint8_t flag; /* ItemOnGroundEntity.flag low byte (room entity data) */
} RandoKeymapEntry;

typedef struct RandoScriptedKeyEntry {
    const char* location; /* .logic location name */
    uint32_t key;         /* Rando_BuildScriptedKey(...) */
} RandoScriptedKeyEntry;

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

    /* ================== 1:1 pass — overworld & remaining dungeon items ==
     * Same verification method as above: USA record found in the room's
     * entity lists (gAreaTable walk), record+3 = item-type byte matches the
     * .logic vanilla item, and EU addr - (rec+3) equals the surrounding data
     * block's constant EU->USA delta. Flags are spritePtr>>16 (ground items
     * / EnemyItem / ice blocks) or spritePtr&0xFFFF (FallingItemManager,
     * src/manager/fallingItemManager.c copies sp.low into the spawned
     * ground item's flag).                                                 */

    /* --- Dojos (area 0x25, EU-0x8A4) ------------------------------------ */
    /* USA recs 0xD7DCC/0xD816C/0xD825C/0xD83A4: GI HeartPiece flags
     * 0x80/0x7F/0x83/0x82 in rooms 0x00/0x04/0x05/0x06 */
    { "Crenel_Dojo_HP", 0x25, 0x00, 0x80 },
    { "Swamp_Dojo_HP", 0x25, 0x04, 0x7F },
    { "Castle_Dojo_HP", 0x25, 0x05, 0x83 },
    { "Hylia_Dojo_HP", 0x25, 0x06, 0x82 },

    /* --- Town house interiors ------------------------------------------- */
    /* USA rec 0xD6F78: GI HeartPiece flag 0xB8 (inn backdoor, EU-0x8A4) */
    { "Town_Inn_BackdoorHP", 0x21, 0x0A, 0xB8 },
    /* USA rec 0xF5E48: GI HeartPiece flag 0xB4 (music house, EU-0xA44) */
    { "Town_MusicHouse_HP", 0x23, 0x05, 0xB4 },

    /* --- Minish paths (area 0x11, EU-0x8A4) ----------------------------- */
    { "Town_School_Path_HP", 0x11, 0x02, 0x7E },
    { "LonLon_Path_HP", 0x11, 0x03, 0xBA },

    /* --- Royal Valley graves / Castor caves ----------------------------- */
    /* USA rec 0xD9388: GI HeartPiece flag 0x5D (EU-0x8A4) */
    { "Valley_GraveyardLeftGrave_HP", 0x34, 0x00, 0x5D },
    /* USA rec 0xDA1B8: GI HeartPiece flag 0x38 (EU-0x8B4) */
    { "Swamp_NearWaterfall_CaveHP", 0x2A, 0x04, 0x38 },

    /* --- Minish caves / village (EU-0x8C4) ------------------------------ */
    { "Ruins_MinishCave_HP", 0x35, 0x03, 0x7E },
    { "SouthField_MinishSize_WaterHole_HP", 0x35, 0x04, 0x81 },
    { "MinishWoods_FlipperHole_HP", 0x35, 0x09, 0x7A },
    { "MinishVillage_HP", 0x01, 0x01, 0xC2 },

    /* --- Melari's Mine dig spots (area 0x10 room 0, EU-0x8C4) ----------- */
    { "Crenel_Melari_UpperTop_MiddleDig", 0x10, 0x00, 0xBA },
    { "Crenel_Melari_UpperTop_RightDig", 0x10, 0x00, 0xBC },
    { "Crenel_Melari_UpperMiddle_RightDig", 0x10, 0x00, 0xBD },
    { "Crenel_Melari_UpperMiddle_LeftDig", 0x10, 0x00, 0xBF },

    /* --- Cloud Tops (area 0x08, EU-0x8C4) -------------------------------- */
    /* Kill rewards: FallingItemManager recs 0xDD7A0/0xDD7B0 (item 0x5C
     * golden kinstone, room 0x02); north y=0x38, south y=0x2E8 */
    { "Clouds_North_Kill", 0x08, 0x02, 0xF4 },
    { "Clouds_South_Kill", 0x08, 0x02, 0xF6 },
    /* Dig kinstones: 7x GI recs 0xDD40C..0xDD46C (room 0x01), EU
     * 0xDCB4B..0xDCBAB in .logic order */
    { "Clouds_NorthWest_DigSpot", 0x08, 0x01, 0xE5 },
    { "Clouds_NorthEast_DigSpot", 0x08, 0x01, 0xE6 },
    { "Clouds_South_MiddleDigSpot", 0x08, 0x01, 0xE7 },
    { "Clouds_SouthEast_TopDigSpot", 0x08, 0x01, 0xE8 },
    { "Clouds_South_DigSpot", 0x08, 0x01, 0xE9 },
    { "Clouds_South_RightDigSpot", 0x08, 0x01, 0xEA },
    { "Clouds_SouthEast_BottomDigSpot", 0x08, 0x01, 0xEB },

    /* --- Hyrule Field caves (area 0x03, EU-0xA44/-0xA54) ----------------- */
    /* USA recs 0xF7730/0xF7740 (room 0x05, LonLon cape cave): HP + buried
     * Rupee50; 0xF7C60 (room 0x06): buried Rupee50 */
    { "Hylia_CapeCave_LonLonHP", 0x03, 0x05, 0x7E },
    { "LonLon_DigSpot", 0x03, 0x05, 0x7F },
    { "NorthField_DigSpot", 0x03, 0x06, 0x8F },

    /* --- Lake Hylia (area 0x0B room 0, EU-0x9DC) ------------------------- */
    { "Hylia_SmallIsland_HP", 0x0B, 0x00, 0x08 },
    { "Hylia_BottomHP", 0x0B, 0x00, 0x0A },

    /* --- Minish Woods (area 0x00 room 0, EU-0x9DC) ----------------------- */
    { "MinishWoods_TopHP", 0x00, 0x00, 0x3C },
    { "MinishWoods_BottomHP", 0x00, 0x00, 0x3D },

    /* --- Dig caves (EU-0x9DC) -------------------------------------------- */
    /* USA rec 0xF4678: GI Rupee20 flag 0x46 (Eastern Hills farm dig cave) */
    { "Hills_FarmDigCave_Item", 0x13, 0x00, 0x46 },
    /* USA rec 0xF4580: GI HeartPiece flag 0x45 (Crenel dig cave) */
    { "Crenel_DigCave_HP", 0x14, 0x00, 0x45 },

    /* --- Hyrule Town minish caves (area 0x62) ---------------------------- */
    /* USA rec 0xEFD70: GI HeartPiece flag 0xC3 (fountain, EU-0x9BC);
     * 0xF0164: GI Rupee50 underwater flag 0x13 (under library, EU-0x9CC) */
    { "Town_Fountain_HP", 0x62, 0x00, 0xC3 },
    { "Town_UnderLibrary_Underwater", 0x62, 0x10, 0x13 },

    /* --- Veil Falls region (EU-0xAA4/-0xAB4) ------------------------------ */
    /* USA rec 0xF90EC: GI HeartPiece flag 0x7B (north field cave, area 0x32) */
    { "NorthField_HP", 0x32, 0x15, 0x7B },
    /* VEIL_FALLS area 0x0A room 0 block (EU-0xAB4): HP pair + rocks + digs */
    { "Falls_Entrance_HP", 0x0A, 0x00, 0xA1 },
    { "FallsLower_HP", 0x0A, 0x00, 0xAB },
    { "FallsLower_RockItem1", 0x0A, 0x00, 0xA3 },
    { "FallsLower_RockItem2", 0x0A, 0x00, 0xA4 },
    { "FallsLower_RockItem3", 0x0A, 0x00, 0xA5 },
    { "Falls_NorthDigSpot", 0x0A, 0x00, 0xA8 },
    { "Falls_SouthDigSpot", 0x0A, 0x00, 0xAA },
    /* Falls rupee cave (area 0x33 room 0x08): 15x GI recs 0xF99D8..0xF9AB8
     * (EU 0xF8F27..0xF9007, uniform EU-0xAB4; item types match per record) */
    { "Falls_RupeeCave_TopTop", 0x33, 0x08, 0x4D },
    { "Falls_RupeeCave_TopLeft", 0x33, 0x08, 0x4E },
    { "Falls_RupeeCave_TopMiddle", 0x33, 0x08, 0x4F },
    { "Falls_RupeeCave_TopRight", 0x33, 0x08, 0x50 },
    { "Falls_RupeeCave_TopBottom", 0x33, 0x08, 0x51 },
    { "Falls_RupeeCave_SideTop", 0x33, 0x08, 0x52 },
    { "Falls_RupeeCave_SideLeft", 0x33, 0x08, 0x53 },
    { "Falls_RupeeCave_SideRight", 0x33, 0x08, 0x54 },
    { "Falls_RupeeCave_SideBottom", 0x33, 0x08, 0x55 },
    { "Falls_RupeeCave_Underwater_TopLeft", 0x33, 0x08, 0x56 },
    { "Falls_RupeeCave_Underwater_TopRight", 0x33, 0x08, 0x57 },
    { "Falls_RupeeCave_Underwater_MiddleLeft", 0x33, 0x08, 0x58 },
    { "Falls_RupeeCave_Underwater_MiddleRight", 0x33, 0x08, 0x59 },
    { "Falls_RupeeCave_Underwater_BottomLeft", 0x33, 0x08, 0x5A },
    { "Falls_RupeeCave_Underwater_BottomRight", 0x33, 0x08, 0x5B },

    /* --- Mt Crenel (EU-0xAB4/-0xABC) -------------------------------------- */
    { "CrenelBase_EntranceVine", 0x06, 0x04, 0x4B },
    { "CrenelBase_FairyCave_Item1", 0x26, 0x09, 0x43 },
    { "CrenelBase_FairyCave_Item2", 0x26, 0x09, 0x44 },
    { "CrenelBase_FairyCave_Item3", 0x26, 0x09, 0x45 },
    { "CrenelBase_WaterCave_HP", 0x26, 0x08, 0x40 },
    { "Crenel_FairyCave_HP", 0x26, 0x05, 0x7D },

    /* --- Fight drops (FallingItemManager, flag = spritePtr&0xFFFF) ------- */
    /* USA rec 0xDEDDC (EU 0xDE51B-0x8C4+3): SmallKey, Deepwood room 0x08 */
    { "Deepwood_1F_East_MulldozerFight_Item", 0x48, 0x08, 0x30 },
    /* USA rec 0xE5ECC (EU 0xE55CB-0x904+3): SmallKey, ToD room 0x2F */
    { "Droplets_RightPath_B2_Mulldozers_ItemDrop", 0x60, 0x2F, 0x6F },
    /* USA recs 0xE9270/0xE7AA0 (EU-0x904): SmallKeys, PoW rooms 0x20/0x08 */
    { "Palace_1stHalf_3F_PotPuzzle_ItemDrop", 0x70, 0x20, 0x59 },
    { "Palace_1stHalf_5F_BallAndChainSoldiers_ItemDrop", 0x70, 0x08, 0x41 },
    /* USA recs 0xE340C/0xE354C: SmallKeys in WEST_STAIRS_1F (0x20) /
     * EAST_STAIRS_1F (0x22); the fowLeftItem/fowRightItem define targets */
    { "Fortress_Left_3F_ItemDrop", 0x58, 0x20, 0x3F },
    { "Fortress_Right_3F_ItemDrop", 0x58, 0x22, 0x41 },
    /* USA rec 0xE276C (EU 0xE1E8B-0x8E4+3): SmallKey in
     * PILLAR_CLONE_BUTTONS (.logic FoWStatueCloneSwitch room) */
    { "Fortress_BackRight_Statue_ItemDrop", 0x58, 0x14, 0x2E },
    /* USA rec 0xF4C28 (EU 0xF424F-0x9DC+3): SmallKey; aliased property list
     * (0x18,0x04)==(0x58,0x06); area 0x18 hosts these fortress sub-rooms
     * (same precedent as the verified Fortress_Left_2F rows above) */
    { "Fortress_BackRight_Minish_ItemDrop", 0x18, 0x04, 0x64 },

    /* --- Royal Crypt (area 0x68, EU-0x904) -------------------------------- */
    /* FallingItemManager recs 0xE6C58/0xE6CA8 (room 0x04, key-block room) */
    { "Crypt_LeftItem", 0x68, 0x04, 0xB6 },
    { "Crypt_RightItem", 0x68, 0x04, 0xB7 },
    /* EnemyItem recs 0xE718C/0xE71AC (room 0x08, entrance): gibdo-bound
     * drops, t2 item bytes Bombs5/SmallKey match the .logic vanilla items */
    { "Crypt_Gibdo_LeftItem", 0x68, 0x08, 0xC4 },
    { "Crypt_Gibdo_RightItem", 0x68, 0x08, 0xC5 },

    /* --- ToD entrance ice blocks (smallIceBlock.c releases the key via
     * CreateGroundItemWithFlags(this->flag)) ------------------------------ */
    /* USA rec 0xE4DA0: SMALL_ICE_BLOCK type 2 (BigKey) flag 0x4F in room
     * 0x20 (BOSS_KEY); 0xE4E60: type 1 (SmallKey) flag 0x52 in room 0x21
     * (NORTH_SMALL_KEY) */
    { "Droplets_Entrance_B2_WestIceblock", 0x60, 0x20, 0x4F },
    { "Droplets_Entrance_B2_EastIceblock", 0x60, 0x21, 0x52 },

    /* --- Boss heart containers (heartContainer.c pickup hook keys
     * area-room-(flag&0xFF); records from the boss rooms' state-change
     * entity lists, flag = spritePtr&0xFFFF persistence flag) ------------- */
    { "Deepwood_BossItem", 0x49, 0x00, 0x47 },
    { "CoF_BossItem", 0x51, 0x00, 0x3A },
    { "Fortress_BossItem", 0x58, 0x16, 0x32 },
    { "Droplets_BossItem", 0x60, 0x0E, 0x40 },
    { "Palace_BossItem", 0x70, 0x00, 0x7D },

    /* --- Chest TileEntities with precise .logic addresses ----------------- */
    /* USA TileEntity 0xD9410 (EU 0xD8A86-0x98C+2): SMALL_CHEST Shells,
     * ROYAL_VALLEY_GRAVES room 0x01, chest index 0 (chestSpawner hook keys
     * area-room-chestIndex) */
    { "Valley_LostWoods_Chest", 0x34, 0x01, 0x00 },

    /* --- Smith house floor items (spawned by the PC port when a real
     * .logic places rewards there — roomInit.c LinksHouseSmith hook; flags
     * 0xE0/0xE1 are unused across every LOCAL_BANK_2 area's entity data) --- */
    { "Smith_Floor_Item1", 0x22, 0x11, 0xE0 },
    { "Smith_Floor_Item2", 0x22, 0x11, 0xE1 },
};

#define RANDO_KEYMAP_COUNT (sizeof(kGroundItemKeys) / sizeof(kGroundItemKeys[0]))

static const RandoScriptedKeyEntry kScriptedKeys[] = {
    /* --- Town shop / Stockwell ------------------------------------------ */
    { "Town_Shop_80Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_80, 0, 0) },
    { "Town_Shop_300Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_300, 0, 0) },
    { "Town_Shop_600Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_600, 0, 0) },
    { "Town_Shop_Extra600Item",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_EXTRA_600, 0, 0) },
    { "Town_Shop_BehindCounterItem",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_DOGFOOD, 0, 0) },

    /* --- Goron merchant ------------------------------------------------- */
    { "Town_GoronMerchant_1_Left", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 0, 0) },
    { "Town_GoronMerchant_1_Middle", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 1, 0) },
    { "Town_GoronMerchant_1_Right", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 2, 0) },
    { "Town_GoronMerchant_2_Left", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 0, 0) },
    { "Town_GoronMerchant_2_Middle", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 1, 0) },
    { "Town_GoronMerchant_2_Right", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 2, 0) },
    { "Town_GoronMerchant_3_Left", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 0, 0) },
    { "Town_GoronMerchant_3_Middle", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 1, 0) },
    { "Town_GoronMerchant_3_Right", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 2, 0) },
    { "Town_GoronMerchant_4_Left", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 0, 0) },
    { "Town_GoronMerchant_4_Middle", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 1, 0) },
    { "Town_GoronMerchant_4_Right", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 2, 0) },
    { "Town_GoronMerchant_5_Left", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 0, 0) },
    { "Town_GoronMerchant_5_Middle", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 1, 0) },
    { "Town_GoronMerchant_5_Right", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 2, 0) },

    /* --- Dojo rewards (BladeBrothers_GetScroll timer index) ------------- */
    { "Town_Dojo_NPC1", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 0, 0, 0) },
    { "Town_Dojo_NPC2", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 1, 0, 0) },
    { "Town_Dojo_NPC3", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 2, 0, 0) },
    { "Town_Dojo_NPC4", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 3, 0, 0) },
    { "Crenel_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 4, 0, 0) },
    { "Castle_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 5, 0, 0) },
    { "Hylia_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 6, 0, 0) },
    { "Swamp_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 7, 0, 0) },
    { "Swamp_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 8, 0, 0) },
    { "FallsLower_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 9, 0, 0) },
    { "NorthField_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 10, 0, 0) },

    /* --- Cucco minigame rounds ------------------------------------------ */
    { "Town_Cuccos_Lv_1_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 0, 0, 0) },
    { "Town_Cuccos_Lv_2_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 1, 0, 0) },
    { "Town_Cuccos_Lv_3_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 2, 0, 0) },
    { "Town_Cuccos_Lv_4_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 3, 0, 0) },
    { "Town_Cuccos_Lv_5_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 4, 0, 0) },
    { "Town_Cuccos_Lv_6_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 5, 0, 0) },
    { "Town_Cuccos_Lv_7_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 6, 0, 0) },
    { "Town_Cuccos_Lv_8_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 7, 0, 0) },
    { "Town_Cuccos_Lv_9_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 8, 0, 0) },
    { "Town_Cuccos_Lv_10_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 9, 0, 0) },

    /* --- Unique scripted item grants ------------------------------------ */
    { "Town_Carlov_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CARLOV_MEDAL, 0, 0) },
    { "Hylia_DogNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DOG_BOTTLE, 0, 0) },
    { "MinishVillage_BarrelHouse_Item",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_JABBER_NUT, 0, 0) },
    { "Town_Jullieta_Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_RED_BOOK, 0, 0) },
    { "Town_DrLeft_AtticItem",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREEN_BOOK, 0, 0) },
    { "Hylia_MayorCabin_Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BLUE_BOOK, 0, 0) },
    { "Crenel_Melari_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MELARI, 0, 0) },
    { "Town_ShoeShop_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_SHOE_SHOP, 0, 0) },
    { "MinishWoods_BombMinish_NPC1",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_BAG, 0, 0) },
    { "MinishWoods_BombMinish_NPC2",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_REMOTES, 0, 0) },
    { "Minish_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MINISH_GREAT_FAIRY, 0, 0) },
    { "Crenel_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRENEL_GREAT_FAIRY, 0, 0) },
    { "Valley_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_VALLEY_GREAT_FAIRY, 0, 0) },
    { "Valley_DampeNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DAMPE, 0, 0) },
    { "MinishWoods_WitchHut_Item",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_WITCH_HUT, 0, 0) },
    { "Falls_Biggoron", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BIGGORON, 0, 0) },
    { "Town_Library_YellowMinish_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_LIBRARY_YELLOW_MINISH, 0, 0) },
    { "Deepwood_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DEEPWOOD_PRIZE, 0, 0) },
    { "CoF_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_COF_PRIZE, 0, 0) },
    { "Droplets_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DROPLETS_PRIZE, 0, 0) },
    { "Palace_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_PALACE_PRIZE, 0, 0) },
    { "Town_CafeLady_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CAFE_LADY, 0, 0) },
    { "Crypt_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRYPT_PRIZE, 0, 0) },
    { "WindTribe_2F_Gregal_NPC1",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_SHELLS, 0, 0) },
    { "WindTribe_2F_Gregal_NPC2",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_LIGHT_ARROW, 0, 0) },
    { "Trilby_Scrub_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_BOTTLE, 0, 0) },
    { "Crenel_Scrub_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_GRIP, 0, 0) },
    { "Fortress_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_FORTRESS_PRIZE, 0, 0) },
    { "Town_Bell_HP", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BELL_HP, 0, 0) },
    { "SouthField_Tingle_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_TINGLE_TROPHY, 0, 0) },
    { "DHC_B2_King", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DHC_KING, 0, 0) },
    { "Town_Simulation_Chest", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_SIMULATION, 0, 0) },
};

static unsigned BindGroundItemKeys(bool log_misses) {
    unsigned bound = 0;
    for (unsigned i = 0; i < (unsigned)RANDO_KEYMAP_COUNT; ++i) {
        const RandoKeymapEntry* e = &kGroundItemKeys[i];
        uint32_t key = ((uint32_t)e->area << 16) | ((uint32_t)e->room << 8) | (uint32_t)e->flag;
        if (RandoLogic_BindRuntimeKey(e->location, key)) {
            ++bound;
        } else if (log_misses) {
            fprintf(stderr, "[RANDO] keymap miss (ground): %s\n", e->location);
        }
    }
    return bound;
}

static unsigned BindScriptedKeys(bool log_misses) {
    unsigned bound = 0;
    for (unsigned i = 0; i < (unsigned)(sizeof(kScriptedKeys) / sizeof(kScriptedKeys[0])); ++i) {
        if (RandoLogic_BindRuntimeKey(kScriptedKeys[i].location, kScriptedKeys[i].key)) {
            ++bound;
        } else if (log_misses) {
            fprintf(stderr, "[RANDO] keymap miss (scripted): %s\n", kScriptedKeys[i].location);
        }
    }
    return bound;
}

#define RANDO_KEYMAP_COUNT (sizeof(kGroundItemKeys) / sizeof(kGroundItemKeys[0]))

void Rando_Keymap_Apply(void) {
    if (!RandoLogic_IsLoaded()) {
        return;
    }
    bool log_misses = getenv("TMC_RANDO_DEBUG") != NULL;
    unsigned ground_bound = BindGroundItemKeys(log_misses);
    unsigned scripted_bound = BindScriptedKeys(log_misses);
    fprintf(stderr, "[RANDO] keymap: bound %u/%u ground-item + %u/%u scripted locations\n",
            ground_bound, (unsigned)RANDO_KEYMAP_COUNT, scripted_bound,
            (unsigned)(sizeof(kScriptedKeys) / sizeof(kScriptedKeys[0])));
}
