/*
 * port_config.h — ROM region detection and offset configuration.
 *
 * Supports both USA (BZME) and EU (BZMP) ROMs with runtime auto-detection.
 * All region-specific ROM offsets are stored in RomOffsets and selected
 * at load time based on the game code at ROM offset 0xAC.
 */
#pragma once
#include "port_types.h"

/* ---- ROM region enum ---- */
typedef enum {
    ROM_REGION_UNKNOWN = 0,
    ROM_REGION_USA, /* Game code: BZME */
    ROM_REGION_EU,  /* Game code: BZMP */
} RomRegion;

/* ---- Region-specific ROM symbol offsets ---- */
typedef struct {
    /* Major data tables (absolute ROM offsets, i.e. GBA_addr - 0x08000000) */
    u32 gfxAndPalettes;   /* gGlobalGfxAndPalettes */
    u32 gfxGroups;        /* gGfxGroups pointer table */
    u32 paletteGroups;    /* gPaletteGroups pointer table */
    u32 objPalettes;      /* OBJ palette offset table */
    u32 frameObjLists;    /* gFrameObjLists sprite frame data */
    u32 fixedTypeGfx;     /* gFixedTypeGfxData */
    u32 spritePtrs;       /* gSpritePtrs */
    u32 translations;     /* gTranslations */
    u32 text09230;        /* font/text pointer table 1 */
    u32 text09244;        /* font/text raw data 1 */
    u32 text09248;        /* font/text glyph table */
    u32 text0926C;        /* font/text raw data 2 */
    u32 text092AC;        /* font/text border glyphs */
    u32 text092D4;        /* font/text raw data 3 */
    u32 text0942E;        /* font/text raw data 4 */
    u32 text094CE;        /* font/text raw data 5 */
    u32 uiData;           /* UI misc data */
    u32 fadeData;         /* brightness/fade tables */
    u32 overlaySizeTable; /* OBJ size/clipping table */
    u32 mapDataBase;      /* gAreaRoomMap_None — base of map/asset data section */

    /* Area data tables (pointer tables indexed by area ID) */
    u32 areaRoomHeaders;   /* gAreaRoomHeaders — pointer table to RoomHeader arrays (0x90 entries) */
    u32 areaTileSets;      /* gAreaTileSets — pointer table (ptr → ptr) (0x40 entries) */
    u32 areaTileSetsCount; /* number of entries in gAreaTileSets (0x40 for both regions) */
    u32 areaRoomMaps;      /* gAreaRoomMaps — pointer table (ptr → ptr) (0x90 entries) */
    u32 areaTable;         /* gAreaTable — pointer table (ptr → ptr) (0x90 entries) */
    u32 areaTiles;         /* gAreaTiles — pointer table (ptr) (0x90 entries) */
    u32 exitLists;         /* gExitLists — pointer table (ptr → ptr) (0x90 entries) */
    u32 bgAnimTable;       /* gUnk_080B755C — pointer table (ptr) */
    u32 localFlagBanks;    /* gLocalFlagBanks — u16 array (raw data) */

    /* Table counts (same for both regions, but kept per-region for safety) */
    u32 gfxGroupsCount;
    u32 paletteGroupsCount;
    u32 objPalettesCount;
    u32 frameObjListsSize;
    u32 fixedTypeGfxCount;
    u32 spritePtrsCount;

    /* ROM expected size */
    u32 expectedRomSize;

    /* Game code for verification (4 chars) */
    char gameCode[5];
} RomOffsets;

/* ---- Global state ---- */
extern RomRegion gRomRegion;
extern const RomOffsets* gRomOffsets;

/* ---- Predefined offset tables ---- */
extern const RomOffsets kRomOffsets_USA;
extern const RomOffsets kRomOffsets_EU;

/* Detect region from loaded ROM data and set gRomRegion + gRomOffsets.
 * Returns the detected region. */
RomRegion Port_DetectRomRegion(const u8* romData, u32 romSize);
